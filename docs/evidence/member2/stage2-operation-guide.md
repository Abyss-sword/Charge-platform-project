# Stage2 Member2 操作指南

更新日期：2026-09-15

编写基线：`57a6afe`（N7 文档提交后，请用 `git rev-parse HEAD` 记录最终候选提交）

适用分支：`feature/member2-stage2-ui`

本指南覆盖 Qt 车主端、Qt 运营端和 Vue3/DataV Web 大屏。2026-09-16 已接 Qt 正式 forecast 正常路径；推荐等剩余缺口见第8节。不得使用旧 FastAPI、临时 URL、fixture 或固定数值代替正式预测。

## 1. 环境要求

| 项目 | 要求 | 检查命令 |
|---|---|---|
| 操作系统 | Ubuntu；桌面操作需要可用的 X11/Wayland 会话 | `uname -a` |
| CMake | 3.22 或更高 | `cmake --version` |
| C++ | C++17 编译器 | `c++ --version` |
| Qt | Qt 6.2+；Core、Network、Sql、Test、Widgets、Charts | `qtpaths6 --qt-version`；若命令不存在则 `TO_VERIFY` 本机 Qt 安装方式 |
| Boost | Boost.Multiprecision 头文件 | CMake configure 会做强制检查 |
| Node.js | 23 或更高；仓库 `.nvmrc` 当前为 24.20.0 | `node --version` |
| npm | 与 Node 环境配套 | `npm --version` |
| 构建目录 | 仓库根目录下 `build-stage2` | `test -d build-stage2` |

自动化 QtTest 使用 `QT_QPA_PLATFORM=offscreen`，部分网络测试会绑定 localhost 临时端口，不访问外网或 Member3 服务。人工界面验收不要设置 `QT_QPA_PLATFORM=offscreen`。

## 2. Git 信息与交付范围

```bash
cd /home/bit/projects/charge_platform_project
git branch --show-current
git rev-parse HEAD
git status --short
git log --oneline -10
```

期望分支是 `feature/member2-stage2-ui`，开始复跑前工作树应为空。N7 编写前的 Member2 关键提交如下：

| 提交 | 内容 |
|---|---|
| `86900fb` | 车主端用户会话令牌链路 |
| `e163f17` | forecast_v1 共享契约模型和解析器 |
| `a1e9aca` | 六类分析状态展示基础 |
| `e05f6d2` | 两端分析页面骨架 |
| `0367c6b` | 分析页面交互验收 |
| `7ecf403` | Stage1 回归、边界、导航和响应式测试 |
| `3a8a8c2` | Stage2 测试与验收编号映射 |
| `57a6afe` | Web 大屏合规基础 |

Member2 修改范围是 `clients/user/`、`clients/admin/`、客户端共享 UI/适配、`tests/ui/`、`dashboard/src/` 及本证据目录。服务端、数据库、协议、分析和模拟器不属于本指南的修改范围。

## 3. 构建和测试 Qt

从仓库根目录执行：

```bash
cmake -S . -B build-stage2 -DBUILD_TESTING=ON
cmake --build build-stage2 --parallel 2

# Member2 全部 Qt/UI/网络测试
ctest --test-dir build-stage2 -L '^member2$' --output-on-failure

# 根测试；非 Member2 失败必须单独记录，不能为了变绿越界修改
ctest --test-dir build-stage2 --output-on-failure
```

最近一次已记录结果见 [stage2-test-matrix.md](stage2-test-matrix.md)：Member2 17/17、User 11/11、Admin 4/4、Analysis 3/3、Responsive 5/5 均通过。该结果对应文档内记录的 2026-09-15 执行；候选发布前仍须在最终 commit 重跑，不能直接沿用为最终签收。

## 4. 运行前配置与业务服务

Qt 客户端默认连接 `127.0.0.1:48080`。程序依次查找可执行目录或当前目录下的 `app.ini`、`config/app.ini`；没有配置时仍使用该默认地址。

```bash
cd /home/bit/projects/charge_platform_project
test -f config/app.ini || cp config/app.example.ini config/app.ini
```

Remote 业务闭环需要 Member1 的 TCP 服务和已准备的验收数据库。服务端可执行文件已确认位于 `build-stage2/charge_platform_server`，但以下内容必须由集成负责人提供并在复跑记录中填写：

- 验收数据库路径和备份：`TO_VERIFY`
- 数据库是否已包含站点、电桩和演示用户：`TO_VERIFY`
- 本次允许使用的手机号、管理员账号和密码：`TO_VERIFY`
- 是否允许新建或迁移该数据库：`TO_VERIFY`

不要擅自对未知数据库执行初始化。负责人授权独立验收数据库后，服务启动命令格式为：

```bash
./build-stage2/charge_platform_server \
  --host 127.0.0.1 \
  --port 48080 \
  --db /ABSOLUTE/PATH/TO/ACCEPTANCE.sqlite
```

终端必须出现监听成功信息。`/ABSOLUTE/PATH/TO/ACCEPTANCE.sqlite` 不得照抄，必须替换为已授权路径。

## 5. 启动车主端

### 5.1 Remote 正式业务模式

```bash
cd /home/bit/projects/charge_platform_project
unset OFFLINE_DEMO
./build-stage2/clients/user/charge_platform_user
```

操作顺序：

1. 在登录页输入集成负责人提供的 11 位手机号并登录。
2. 首页核对脱敏手机号、昵称、余额和账户状态。
3. 进入“我的”，修改资料或充值；实际金额及是否允许操作按验收数据库授权执行。
4. 底部进入“找桩”，检查列表/地图切换并打开站点详情。
5. 从站点详情进入导航或选择电桩进行预约。
6. 通过“订单”恢复活动订单，依次检查开始充电、停止和结算支付。
7. 进入“记录”检查历史订单。
8. 从“找桩”页点击“智能推荐与预测”，切换 1/6/24 小时期限；到达时刻暂不参与 API 请求，正式模式下禁用并显示说明。
9. 配置 `CHARGE_ANALYSIS_BASE_URL` 指向已部署的正式 Flask 服务。分析页展示发布窗口的负荷预测；无服务时明确显示错误，推荐保持不可用，“返回手动查找站点”仍可进入找桩。不能把历史分析站点自动等同当前业务站点。
10. 在“我的”退出登录，确认回到登录页且旧用户状态未带入下一账号。

Remote 请求失败时只能显示 offline/error/stale，不得自动出现演示数据。

### 5.2 OFFLINE_DEMO 界面演示模式

```bash
cd /home/bit/projects/charge_platform_project
OFFLINE_DEMO=1 ./build-stage2/clients/user/charge_platform_user
```

窗口标题和登录页必须出现 `OFFLINE_DEMO`；主窗口还会显示 `SIMULATED`。此模式只用于离线界面、导航和状态演示，不能作为真实持久化、支付、预测或三端一致性证据。

## 6. 启动运营端

### 6.1 Remote 正式业务模式

```bash
cd /home/bit/projects/charge_platform_project
unset OFFLINE_DEMO
./build-stage2/clients/admin/charge_platform_admin
```

使用集成负责人提供的验收管理员凭据。不要把凭据写进截图、录屏或仓库。登录后按侧栏顺序操作：

1. “总览”：检查业务摘要和跳转入口。
2. “营收”：检查汇总和 Revenue Trend 轴标签。
3. “站点资产”：查询并按授权执行站点更新。
4. “充电桩”：查看状态；仅在授权目标上执行状态变更或重启。
5. “订单”：检查订单列表和状态。
6. “用户”：检查用户列表；仅对验收账号执行冻结/解冻。
7. “告警工单”：区分告警和工单流转。
8. “日志”：核对刚才的管理动作。
9. “预测预警”：切换未来负荷、空闲桩、Top3 高峰和预测预警区域。

“未来负荷”已接正式 forecast，显示服务端发布窗口、版本和数据点；未部署服务时显示错误。空闲桩、Top3 与预警仍保持不可用，不展示固定数值；“预测风险”不得描述成“已发生设备故障”。

### 6.2 OFFLINE_DEMO 界面演示模式

```bash
cd /home/bit/projects/charge_platform_project
OFFLINE_DEMO=1 ./build-stage2/clients/admin/charge_platform_admin
```

点击“登录（模拟）”即可进入。标题、身份和连接区域必须显示 `OFFLINE_DEMO`/`SIMULATED`。该模式不是正式管理员权限、数据库写入或预测结果证据。

## 7. 启动 Web 大屏

项目真实脚本来自 `dashboard/package.json`：

```bash
cd /home/bit/projects/charge_platform_project/dashboard
node --version
npm --version

# 候选验收优先使用 lockfile 的可复现安装
npm ci

# 非交互测试
npm run test:run
npm run build

# 人工浏览器验收
npm run dev
```

浏览器打开 `http://127.0.0.1:4173/`。若 4173 已占用，Vite 是否改用其他端口以终端实际输出为准并记录；验收材料不能预先假定替代端口。

当前正式页面应看到：在线业务区、历史分析区、质量与批次区，六类图表空容器，DataV 边框，数据来源/版本/时间位置以及动效开关。页面不会发起未冻结的分析请求。

开发环境首次准备可以使用 `npm install`，但正式候选复跑使用 `npm ci`。本项目没有 `npm run lint` 脚本，不要编造该命令。

## 8. 当前 BLOCKED 功能

| 能力 | 当前状态 | 解除条件 |
|---|---|---|
| 正式 `/forecast` 正常路径 | READY | 已接共享 HTTP 和 Qt 两端，配置见下方链接；异常 HTTP 单独显示错误 |
| 正式 `/recommend` | BLOCKED BY MEMBER3 | recommendation schema、动态候选和理由冻结 |
| 车主发布窗口预测 | READY | 1/6/24h 已接；到达时间控件禁用，推荐继续阻塞 |
| 运营未来负荷 | READY | 1/6/24h 已接服务端发布产物，不声称历史原点是当前时刻 |
| 空闲桩、Top3、预测预警 | BLOCKED BY MEMBER3 | 容量标签、正式排序/阈值、预警字段仍有缺口 |
| Web forecast | PARTIAL | 正常 forecast API 可消费；Web 绑定仍待 Member2 实现 |
| 三端同站/原点/datasetVersion 一致性 | PARTIAL | 窗口 ICR、Web 绑定和真实三端签收待完成 |
| HTTP 超时、拒绝连接、过期和晚到响应 | PARTIAL | Qt 客户端及 local HTTP 自动测试已完成；物理断网与真实服务过期仍需人工 |

2026-09-16 更新：共享 HTTP 基础提交 `a98c4ec`，正式配置变量为 `CHARGE_ANALYSIS_BASE_URL`，
默认 `http://127.0.0.1:8001`。详见 [forecast 接入与边界说明](stage2-forecast-integration.md)。
旧演示脚本中的全量阻塞假设不再适用于 Qt 正常负荷预测；真实展示前仍须在已部署的 Member3 环境联调。

六状态模型、解析器、页面状态、Qt/Web 骨架和自动测试已完成，但不能代替上述真实数据验收。
