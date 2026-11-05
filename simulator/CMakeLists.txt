cmake_minimum_required(VERSION 3.16)  # 与课程 Ubuntu 环境一致，不依赖更新的 CMake 语法。
project(member3_pile_simulator LANGUAGES CXX)  # 成员3模拟器独立工程，不改根 CMakeLists。

set(CMAKE_CXX_STANDARD 17)           # 协议和文件系统 API 使用 C++17。
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 编译器不够时直接失败。
set(CMAKE_CXX_EXTENSIONS OFF)        # 不用 GNU 扩展，保证 MinGW / g++ 行为一致。

add_library(pile_core pile/scenario.cpp pile/pile_simulator.cpp pile/protocol_frame.cpp)  # 场景、JSON、TCP 核心库。
target_include_directories(pile_core PUBLIC pile)  # 让入口和测试能直接 #include "scenario.h" 等头文件。
if(WIN32)  # Windows 下 TCP 走 Winsock，必须额外链 ws2_32。
  target_link_libraries(pile_core PUBLIC ws2_32)  # Windows 套接字库；Linux 不需要。
endif()  # Linux/macOS 用 POSIX socket，到此结束平台分支。

add_executable(pile_simulator pile/main.cpp)  # 命令行入口：pile_simulator --scenario charging --connect 127.0.0.1:48080
target_link_libraries(pile_simulator PRIVATE pile_core)  # 入口只负责参数和电表文件，组帧与发 TCP 都在库里。

enable_testing()  # 打开 CTest，便于 `ctest` 跑场景断言。
add_executable(scenario_test tests/scenario_test.cpp)  # 九个稳定场景名和固定数值的断言。
target_link_libraries(scenario_test PRIVATE pile_core)  # 测试直接调用 parse_scenario / json_line / encode_frame。
add_test(NAME scenario_test COMMAND scenario_test)  # 把可执行测试登记为 CTest 项 scenario_test。

# 【文件讲解·答辩用】
# 功能作用：这是成员3充电桩模拟器的独立 CMake 工程。它编译三块东西：静态/普通库 pile_core（场景帧、JSON 遥测、长度头 TCP）、
#           命令行程序 pile_simulator（答辩演示入口），以及 scenario_test（九场景固定数值断言）。不改仓库根目录 CMakeLists，
#           避免和成员1服务端工程搅在一起。
# 技术原理：C++17 标准库即可覆盖文件系统、chrono、socket；Windows 必须链接 ws2_32，Linux 用 POSIX socket 不用加库。
#           PUBLIC 头文件目录指向 pile/，这样测试和入口都能写 #include "protocol_frame.h"。CTest 把 scenario_test 登记成可重复验收项。
# 代码思路：先锁死 C++17 且关闭 GNU 扩展 → 把三份核心 .cpp 打成 pile_core → Windows 补 Winsock → 再分别链接出模拟器可执行文件和单元测试。
# 与其他文件关系：编译 pile/scenario.cpp、pile_simulator.cpp、protocol_frame.cpp 和 pile/main.cpp；测试源是 tests/scenario_test.cpp。
#           启动脚本、analytics 大屏按钮最终都调用这里编出来的 pile_simulator。tcp_loopback_test.py 不走 CTest，由脚本另调。
# 答辩时可以怎么讲：先说“模拟器是独立小工程，Windows/Linux 同一份 CMake”；再指着 add_executable(pile_simulator ...) 说演示命令
#           `pile_simulator --scenario charging --connect 127.0.0.1:48080` 就是这个目标编出来的；最后补一句场景数值由 scenario_test 锁死，改场景名会立刻红。
