# 大屏场景按钮的后端入口：只启动已构建的 pile_simulator，绝不 SQL 改业务表。
"""Launch pile_simulator by scenario name. FastAPI never writes the business SQLite."""  # 模块说明：按场景名拉起模拟器，FastAPI 不写业务库。

from __future__ import annotations  # 允许类型标注前向引用，保持 3.10 兼容。

import hashlib  # 用数据库路径哈希区分电表状态文件，避免两套库互相覆盖读数。
import os  # 读取 CHARGE_SERVER_HOST / PORT 和 Windows LOCALAPPDATA。
import socket  # 启动前探测成员1 TCP 端口，服务端不在就明确失败。
import sqlite3  # 只读查询该桩最新累计电量，用来校准模拟器状态文件。
import subprocess  # 真正启动子进程 pile_simulator --scenario。
import threading  # 按钮请求立即返回，场景在后台线程跑，避免卡住 HTTP。
import time  # 场景之间留间隔，离线场景额外等待 18 秒给服务端扫描。
from pathlib import Path  # 统一用路径对象查找可执行文件和状态文件。

from .sqlite_dashboard import PROJECT_DIR, configured_database_path  # 与大屏共用项目根和 CHARGE_PLATFORM_DB。

# 九个答辩场景：id 必须与模拟器 --scenario 完全一致。
SCENARIOS = (  # 九个答辩场景，id 必须与模拟器 --scenario 完全一致。
    {"id": "normal", "label": "正常在线", "expect": "心跳刷新，桩保持正常"},  # 心跳保活。
    {"id": "charging", "label": "正常充电", "expect": "功率与累计电量递增"},  # 电表向前走。
    {"id": "overheat", "label": "高温告警", "expect": "≥70℃ 故障并生成过热告警"},  # 温度越限。
    {"id": "offline", "label": "离线", "expect": "超过15秒无上报后标记离线"},  # 停报触发离线。
    {"id": "recover", "label": "恢复", "expect": "恢复合法上报并回到业务状态"},  # 从故障/离线回来。
    {"id": "duplicate", "label": "重复上报", "expect": "幂等确认，不重复计费"},  # 服务端去重。
    {"id": "time-backward", "label": "时间倒退", "expect": "服务端明确拒绝"},  # 非法时间。
    {"id": "energy-backward", "label": "电量倒退", "expect": "服务端明确拒绝"},  # 非法电表。
    {"id": "unknown-pile", "label": "未知电桩", "expect": "拒绝且不写脏数据"},  # 未登记桩。
)  # 九场景目录结束。
SCENARIO_IDS = {item["id"] for item in SCENARIOS}  # 快速校验按钮传来的名称。
DEMO_CYCLE = ("normal", "charging", "overheat", "offline", "recover")  # 答辩轮播顺序。

_lock = threading.Lock()  # 保护忙闲状态，防止连续点击启动多个模拟器。
_status: dict[str, object] = {  # 给大屏轮询的执行状态。
    "busy": False,  # True 时按钮应禁用。
    "scenario": None,  # 当前或刚执行完的场景名。
    "message": "待命，可从大屏切换场景",  # 给界面看的中文提示。
    "lastExitCode": None,  # 子进程退出码，供测试断言。
}  # 结束：给大屏轮询的执行状态。


def catalog() -> dict[str, object]:  # GET /simulator/scenarios 返回目录，并声明本模块不写业务库。
    return {
        "scenarios": list(SCENARIOS),  # 九个场景的 id/中文名/预期现象。
        "cycle": list(DEMO_CYCLE),  # 一键轮播用的五步顺序。
        "status": snapshot(),  # 当前忙闲，避免前端再打一次 status。
        "writesBusinessDatabase": False,  # 合同字段：本接口不 SQL 写业务表。
        "path": "dashboard button -> FastAPI -> pile_simulator --scenario -> TCP server",  # 调用链说明。
    }  # 场景目录 JSON，声明 writesBusinessDatabase=False。


def snapshot() -> dict[str, object]:  # 复制当前状态，避免调用方拿到可变内部字典。
    with _lock:  # 与后台线程互斥读。
        return dict(_status)  # 浅拷贝四字段。


def reset_for_tests() -> None:  # 单元测试之间清状态，避免上一个用例的 busy 污染下一个。
    with _lock:  # 与启动路径同一把锁。
        _status.update(  # 恢复待命。
            {
                "busy": False,  # 空闲。
                "scenario": None,  # 无当前场景。
                "message": "待命，可从大屏切换场景",  # 默认文案。
                "lastExitCode": None,  # 清退出码。
            }  # 结束：恢复待命。
        )  # 结束：结束：恢复待命。


def start_scenario(name: str, pile_id: int = 101) -> dict[str, object]:  # 校验名称后立刻置忙并起后台线程；HTTP 层只需拿到当前快照。
    if name != "cycle" and name not in SCENARIO_IDS:  # cycle 是合成名，其余必须在九场景里。
        raise ValueError(f"未知场景：{name}")  # 400 由 api.py 转换。
    with _lock:  # 检查并置忙必须原子，否则双击会起两个进程。
        if _status["busy"]:  # 已有任务。
            raise RuntimeError("已有场景正在执行，请等待完成后再切换")  # 409 冲突。
        _status.update(  # 先对外显示“正在启动”，再跑线程。
            {
                "busy": True,  # 按钮应禁用。
                "scenario": name,  # 记录请求名（可能是 cycle）。
                "message": f"正在启动场景 {name}",  # 中文进度。
                "lastExitCode": None,  # 新任务尚未结束。
            }  # 结束：先对外显示“正在启动”，再跑线程。
        )  # 结束：结束：先对外显示“正在启动”，再跑线程。
    worker = threading.Thread(target=_run_job, args=(name, pile_id), daemon=True)  # 守护线程，进程退出即停。
    worker.start()  # 立即返回 HTTP，不等 90 秒超时。
    return snapshot()  # 把刚写入的忙状态给前端。


def _run_job(name: str, pile_id: int) -> None:  # 后台真正跑场景：单场景一次，cycle 则按 DEMO_CYCLE 串行。
    exit_code = 1  # 默认失败，成功路径会覆盖。
    message = "场景执行失败"  # 默认文案，finally 写回 _status。
    try:  # 尝试：轮播展开为五步，否则单元素列表。
        names = list(DEMO_CYCLE) if name == "cycle" else [name]  # 轮播展开为五步，否则单元素列表。
        for index, current in enumerate(names):  # 按顺序执行。
            _set_status(current, f"正在执行 {current}")  # 更新大屏提示。
            if index > 0:  # 第一步不等待。
                time.sleep(2)  # 避免两场景同一秒时间戳冲突。
            exit_code = _run_one(current, pile_id)  # 拉起 pile_simulator。
            if current == "offline" and name == "cycle":  # 轮播里的离线步需要给扫描留时间。
                _set_status(current, "等待服务端离线扫描")  # 提示不要立刻点恢复。
                time.sleep(18)  # 服务端离线阈值是 15 秒，多留 3 秒余量。
            if current not in {"time-backward", "energy-backward", "unknown-pile"} and exit_code != 0:  # 合法场景非 0 才算失败。
                message = f"场景 {current} 执行失败，exit={exit_code}"  # 记下哪一步挂了。
                return  # 合法场景失败立即停，避免后面的恢复把现场搅乱。
        rejected = {"time-backward", "energy-backward", "unknown-pile"}  # 预期被服务端拒绝的三个。
        if name in rejected:  # 单步拒绝类。
            message = f"场景 {name} 已按预期被服务端拒绝"  # 成功口径：拒绝也是预期。
        elif name == "cycle":  # 五步跑完。
            message = "演示轮播完成：正常、充电、过热、离线、恢复"  # 答辩轮播结束语。
        else:  # 普通单场景成功。
            message = f"场景 {name} 已完成，大屏将在3秒内刷新"  # 配合大屏 3 秒轮询。
    except Exception as error:  # noqa: BLE001  把启动失败变成界面可读中文，不把堆栈丢给浏览器。
        message = str(error)  # FileNotFoundError / ConnectionError 的中文或默认 str。
        exit_code = 1  # 异常一律失败码。
    finally:  # 收尾：无论成败都放忙。
        with _lock:  # 无论成败都放忙。
            _status["busy"] = False  # 允许下一轮点击。
            _status["message"] = message  # 最终提示。
            _status["lastExitCode"] = exit_code  # 给测试读。


def _set_status(scenario: str, message: str) -> None:  # 更新当前场景名和提示，busy 位由 start/_run_job 单独维护。
    with _lock:  # 与 snapshot 互斥。
        _status["scenario"] = scenario  # 正在跑的子场景名。
        _status["message"] = message  # 进度句子。


def _run_one(name: str, pile_id: int) -> int:  # 组装 pile_simulator 命令：真实 TCP、校准电表文件、1 秒帧间隔。
    executable = _find_simulator()  # 在常见 build 目录找 exe。
    if executable is None:  # 没编过模拟器。
        raise FileNotFoundError("未找到 pile_simulator，请先构建 simulator")  # 给界面看。
    host = os.environ.get("CHARGE_SERVER_HOST", "127.0.0.1")  # 成员1 监听地址，默认本机。
    port = int(os.environ.get("CHARGE_SERVER_PORT", "48080"))  # 成员1 TCP 端口。
    _require_server(host, port)  # 连不上就失败，绝不伪造 ACK。
    time.sleep(2)  # 给上一帧入库留时间，降低时间戳冲突。
    state_file = _seed_meter_file(pile_id)  # 只写本机电表文件，不写 SQLite 业务表。
    env = os.environ.copy()  # 继承环境，额外可能改 PATH。
    mingw = _mingw_bin()  # Windows 上找 libstdc++。
    if mingw is not None:  # 找到 MinGW bin。
        env["PATH"] = str(mingw) + os.pathsep + env.get("PATH", "")  # Windows 需要 MinGW 运行库才能启动 exe。
    command = [  # 子进程参数列表，不用 shell。
        str(executable),  # pile_simulator 路径。
        "--pile",  # 桩号开关。
        str(pile_id),  # 默认 101。
        "--scenario",  # 场景开关。
        name,  # 与 C++ --scenario 枚举一致。
        "--connect",  # 连接成员1。
        f"{host}:{port}",  # host:port。
        "--state-file",  # 累计电表文件。
        str(state_file),  # 已用库内最新电量校准。
        "--interval-ms",  # 帧间隔。
        "1000",  # 1 秒一帧，方便大屏 3 秒刷新看到变化。
    ]  # 结束：子进程参数列表，不用 shell。
    completed = subprocess.run(  # 同步等结束，超时 90 秒。
        command,  # 可执行文件 + 参数。
        cwd=str(PROJECT_DIR),  # 工作目录在仓库根，相对资源一致。
        env=env,  # 可能带 MinGW PATH。
        capture_output=True,  # 吞掉 stdout/stderr，避免刷爆分析日志。
        text=True,  # 文本模式，便于以后读错误。
        timeout=90,  # 单场景上限。
        check=False,  # 拒绝类场景退出码非 0 是预期，不在这里抛异常。
    )  # 结束：同步等结束，超时 90 秒。
    return int(completed.returncode)  # 交给 _run_job 判断成败。


def _find_simulator() -> Path | None:  # 按本机常见构建目录查找，Windows 带 .exe，Ubuntu 无后缀。
    names = ("pile_simulator.exe", "pile_simulator")  # 先 exe 再无后缀。
    roots = (  # 成员3 文档里出现过的三个构建目录。
        PROJECT_DIR / "build-member3-simulator",  # 推荐目录。
        PROJECT_DIR / "b3req",  # 另一套本地构建。
        PROJECT_DIR / "build-simulator",  # 通用名。
    )  # 结束：成员3 文档里出现过的三个构建目录。
    for root in roots:  # 按优先级扫。
        for name in names:  # 每个目录试两种文件名。
            candidate = root / name  # 拼接路径。
            if candidate.is_file():  # 存在且是文件。
                return candidate  # 找到即返回，不继续。
    return None  # 三处都没有。


def _mingw_bin() -> Path | None:  # 先跟 g++ 同目录，再扫常见 Qt Tools 路径，解决缺 libstdc++-6.dll。
    compiler = _which("g++.exe") or _which("g++")  # PATH 上的编译器。
    if compiler is not None:  # 找到 g++。
        return compiler.parent  # bin 目录通常就有运行库 DLL。
    for tools in (Path(r"D:\Qt6\Tools"), Path(r"D:\Qt\Tools"), Path(r"C:\Qt\Tools")):  # 实验机常见 Qt 安装盘。
        if not tools.is_dir():  # 盘符不存在就跳过。
            continue  # 试下一处。
        for mingw in tools.glob("mingw*/bin"):  # mingw64 或 mingw81 等。
            if (mingw / "libstdc++-6.dll").is_file():  # 确认运行库在。
                return mingw  # 把该 bin 插到 PATH 最前。
    return None  # 找不到也不改 PATH，让启动错误自己暴露。


def _which(name: str) -> Path | None:  # 在系统 PATH 里查找 g++ 等可执行文件。
    from shutil import which  # 局部导入，避免模块顶层依赖顺序问题。

    found = which(name)  # 走系统 PATH。
    return Path(found) if found else None  # 没有则 None。


def _require_server(host: str, port: int) -> None:  # 连不上成员1服务端就失败，绝不伪造 ACK。
    probe = socket.socket()  # 临时 TCP 套接字。
    probe.settimeout(2)  # 两秒连不上算不可达。
    try:  # 尝试：三次握手成功即认为服务端在听。
        probe.connect((host, port))  # 三次握手成功即认为服务端在听。
    except OSError as error:  # 拒绝、超时、不可达。
        raise ConnectionError(f"成员1服务端不可达：{host}:{port}") from error  # 中文错误给大屏。
    finally:  # 收尾：探测套接字必须关。
        probe.close()  # 探测套接字必须关。


def _seed_meter_file(pile_id: int) -> Path:  # 状态文件路径必须与 run-member3-stack.ps1 一致，否则电表会从 100.00 重来被服务端拒绝。
    database = configured_database_path().resolve()  # 与大屏同一 CHARGE_PLATFORM_DB。
    digest = hashlib.sha256(str(database).lower().encode("utf-8")).hexdigest()[:12]  # 路径哈希，区分不同库。
    local = os.environ.get("LOCALAPPDATA")  # Windows 用户本地数据目录。
    state_root = Path(local) / "charge-platform-member3" if local else PROJECT_DIR / "runtime"  # 无 LOCALAPPDATA 时退回仓库 runtime。
    state_root.mkdir(parents=True, exist_ok=True)  # 确保目录。
    state_file = state_root / f"pile-{pile_id}-{digest}.energy"  # 每桩每库一个文件。
    meter = _latest_meter(database, pile_id)  # 只读库内最新累计电量。
    state_file.write_text(f"{meter}\n", encoding="ascii")  # 只写本机电表文件，不是业务 SQLite。
    return state_file  # 传给 --state-file。


def _latest_meter(database: Path, pile_id: int) -> str:  # 只读取该桩最新 energy_kwh；库不存在或没有遥测时从 100.00 起步。
    if not database.is_file():  # 库还没建。
        return "100.00"  # 与模拟器默认起点一致。
    connection = sqlite3.connect(f"file:{database}?mode=ro", uri=True)  # URI 只读，不能写订单/告警。
    try:  # 尝试：按自增 id 倒序取一条。
        row = connection.execute(  # 按自增 id 倒序取一条。
            "SELECT energy_kwh FROM pile_telemetry WHERE pile_id=? ORDER BY id DESC LIMIT 1",  # 累计电表，不是小时增量。
            (pile_id,),  # 绑定桩号。
        ).fetchone()  # 可能没有行。
    finally:  # 收尾：只读连接也要关。
        connection.close()  # 只读连接也要关。
    return str(row[0]) if row and row[0] else "100.00"  # 空值当 100.00。

# 【文件讲解·答辩用】
# 功能作用：大屏点场景按钮后，本模块在后台启动 pile_simulator --scenario，让成员1 TCP 服务端真正收帧、写库。
# 技术原理：HTTP 立刻返回；Lock 防连点；subprocess 调已构建 exe；电表状态文件用库路径哈希区分；SQLite 仅 mode=ro 读最新电量。
# 代码思路：catalog/snapshot 给轮询；start_scenario 置忙起线程；_run_one 探端口、校准电表、拼命令；拒绝类场景允许非 0 退出码。
# 与其他文件关系：api.py 的 /simulator/* 只转调这里；configured_database_path 来自 sqlite_dashboard；不调用 build_dashboard_payload 写任何业务 UPDATE。
# 答辩时可以怎么讲：成员3不改订单表，只“按按钮启动模拟器”；电量倒退等脏数据由服务端拒绝，大屏只读刷新就能看到结果。
