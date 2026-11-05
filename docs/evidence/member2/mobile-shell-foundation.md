# 成员2：车主端 Mobile Shell 基础验证

- 日期：2026-09-06
- 范围：Shared UI、车主端窗口、底部五项导航、五个一级页面、UI测试。
- 主负责人：成员2；本记录由开发Agent执行，尚待成员1或成员3独立复跑签字。
- 未提交；不代表完整11页面或团队阶段验收完成。

## 实现与边界

车主端使用C++17、Qt Widgets，默认480×1040、最小360×780。MobilePageHeader、中央QStackedWidget和BottomNavigationBar由Layout组织；五页面初始化一次，Qt parent ownership负责释放。主操作和状态区位于滚动内容之外。

共享Theme及QSS采用规格色值；StatusBadge提供文字/符号和颜色；PageStateWidget支持Loading、Normal、Empty、Error、Offline、Reconnecting、Degraded。PageScaffold提供纵向卡片、滚动区、状态与可选主操作区。

首页、附近站点、活动订单、充电记录、账户均为模拟展示，带SIMULATED和固定数据时间。金额、电量为展示字符串，不进行协议解析、转换或计费。本轮没有ServiceClient、数据库访问、正式业务调用、运营端或二级页面。未实现动效、确认弹窗及Toast；后续在实际需要时补充。导航暂用Qt标准图标，源文件记录了统一SVG替换TODO。

本次提示词的“地图在上、列表在下”与最新规格§6.4不同，按提示词指定优先级执行最新规格：默认纵向列表、列表/地图模式切换，地图仅本地降级预览。

## 构建复现

环境：Qt 6.2.4、CMake 3.22.1、g++ 11.4.0、GNU Make。Ninja未安装；根CMake缺少客户端接入，且要求Charts/WebEngine开发组件。本轮不改根构建。以下临时入口直接加载正式子CMake，不复制源码或另建永久构建体系：

```bash
mkdir -p /tmp/member2-shell-validation
cat > /tmp/member2-shell-validation/CMakeLists.txt <<'CMAKE'
cmake_minimum_required(VERSION 3.22)
project(member2_shell_validation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Test)
include(CTest)
set(REPO /home/bit/projects/charge_platform_project)
include(${REPO}/cmake/ProjectOptions.cmake)
add_subdirectory(${REPO}/clients/shared/ui shared-ui)
add_subdirectory(${REPO}/clients/user user)
add_subdirectory(${REPO}/tests/ui ui-tests)
CMAKE
cmake -S /tmp/member2-shell-validation -B /tmp/member2-shell-build -G 'Unix Makefiles' -DBUILD_TESTING=ON
cmake --build /tmp/member2-shell-build -j2
ctest --test-dir /tmp/member2-shell-build --output-on-failure
```

构建成功，生成 `/tmp/member2-shell-build/user/charge_platform_user`。可在桌面终端直接运行。另已用 `QT_QPA_PLATFORM=offscreen` 独立启动该程序，事件循环运行1秒后主动终止，无错误输出；这不是人工桌面交互验收。

根构建检查：`cmake -S . -B /tmp/member2-root-build-check -G Ninja -DBUILD_TESTING=ON` 失败，报 `CMAKE_MAKE_PROGRAM is not set`，原因是Ninja缺失；未进入源码编译。配置最小UI时的可选Vulkan头文件提示未阻断构建。

## 测试过程与实际结果

1. 先编写导航测试；初次编译因尚无 `shell/main_window.h` 失败。
2. 加入空QMainWindow基线后测试可编译运行，5项断言失败：尺寸、页面栈/切换、两个目标尺寸、页面所有权尚未实现。确认不是环境错误后开始实现。
3. 实现中修复缺少QVariant直接包含的编译问题。
4. 首次完整测试发现无主操作页面也创建了隐藏按钮，尺寸可见性断言失败。改为仅在配置主操作时创建按钮，未弱化断言。
5. 最终CTest：2个程序通过，0失败。Qt Test合计16项通过（包含两程序各自的初始化/清理），12个实质测试用例，0失败、0跳过。

覆盖：默认/最小尺寸、五项点击切页、重复切换对象不变、窗口销毁释放页面、目标尺寸导航/主操作/状态边界、无水平滚动范围、七种状态、Badge语义、空/错误恢复、列表/地图切换、一级快捷入口、无效导航参数及预留返回按钮。

Offscreen测试中有 `This plugin does not support propagateSizeHints()` 提示，不影响断言；尚需实际桌面独立复跑。

截图可复现：

```bash
UI_SCREENSHOT_DIR=/tmp/member2-shell-screenshots ctest --test-dir /tmp/member2-shell-build --output-on-failure
```

已生成五页面各480×1040、360×780共10张截图；已查看首页两种尺寸及其余四页面的360×780截图。导航、顶部、主操作可见，正文可读；首页长内容垂直滚动。截图为临时渲染证据，不替代非作者签字。

## 接口缺口与跨成员依赖

- 当前协议未定义满足活动订单读取/恢复、个人历史查询、账户资料与余额查询需要的完整正式接口；本轮均保持Mock展示，不自行设计action。
- 执行文档中的“分/Wh”与当前v2契约冲突继续保留，成员1统一后才能实施正式字段映射。
- 根CMake及环境由成员1协调；建议在公共目标之后加入：

```cmake
add_subdirectory(clients/shared/ui)
add_subdirectory(clients/user)
if(BUILD_TESTING)
    add_subdirectory(tests/ui)
endif()
```

- 后续统一图标、动效与未完成业务能力不计入本轮完成项。
- 下一独立工作包建议：仅用Mock实现站点详情二级页面和返回导航，暂不接正式接口。

## 修改范围

编码前的全部Git跟踪文件均已用SHA-256核验未改变，包含4份原有未提交文档、根CMake、成员1/成员3代码。新增内容仅位于 `clients/shared/ui/`、`clients/user/`、`tests/ui/`、`docs/evidence/member2/`。没有提交、推送、合并或切换分支。
