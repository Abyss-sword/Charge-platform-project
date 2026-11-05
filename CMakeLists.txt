# 成员1 · 第一阶段
# 构建脚本（成员1 · 第一阶段）：C++17 + Qt6，AUTOMOC；静态库分层
# charge_protocol（协议）/ charge_repository（数据访问）/ charge_domain（公共算法）/
# charge_business（业务服务）/ charge_server_network（网络层），
# 可执行文件 charge_platform_server；BUILD_TESTING 下注册 20 项 CTest 测试目标。

cmake_minimum_required(VERSION 3.22)

project(charge_platform_project VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(
    Qt6 6.2 REQUIRED
    COMPONENTS Core Network Sql Test Widgets Charts
)
find_path(
    BOOST_MULTIPRECISION_INCLUDE_DIR
    NAMES boost/multiprecision/cpp_dec_float.hpp
    HINTS ${BOOST_ROOT} ENV BOOST_ROOT
    PATH_SUFFIXES include
)
if(NOT BOOST_MULTIPRECISION_INCLUDE_DIR)
    message(FATAL_ERROR "Boost.Multiprecision headers were not found. Set BOOST_ROOT to the Boost installation directory.")
endif()

option(BUILD_TESTING "Build the automated test suite" ON)
if(BUILD_TESTING)
    enable_testing()
endif()
include(cmake/ProjectOptions.cmake)

add_library(charge_protocol STATIC
    shared/protocol/frame_codec.cpp
    shared/protocol/frame_codec.h
    shared/protocol/error_code.h
    shared/protocol/message.h
)
target_include_directories(charge_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_protocol PUBLIC Qt6::Core)
charge_platform_apply_project_options(charge_protocol)

add_library(charge_repository STATIC
    server/repositories/database.cpp
    server/repositories/database.h
)
target_include_directories(charge_repository PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_repository PUBLIC Qt6::Core Qt6::Sql)
charge_platform_apply_project_options(charge_repository)

add_library(charge_domain STATIC
    shared/domain/decimal_value.cpp
    shared/domain/decimal_value.h
    shared/domain/geo.cpp
    shared/domain/geo.h
    shared/domain/order_state.cpp
    shared/domain/order_state.h
)
target_include_directories(charge_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BOOST_MULTIPRECISION_INCLUDE_DIR}
)
target_link_libraries(charge_domain PUBLIC Qt6::Core)
charge_platform_apply_project_options(charge_domain)

add_library(charge_business STATIC
    server/repositories/alarm_repository.cpp
    server/repositories/alarm_repository.h
    server/repositories/order_repository.cpp
    server/repositories/order_repository.h
    server/repositories/user_repository.cpp
    server/repositories/user_repository.h
    server/repositories/wallet_repository.cpp
    server/repositories/wallet_repository.h
    server/repositories/telemetry_repository.cpp
    server/repositories/telemetry_repository.h
    server/services/alarm_service.cpp
    server/services/alarm_service.h
    server/services/admin_service.cpp
    server/services/admin_service.h
    server/services/order_service.cpp
    server/services/order_service.h
    server/services/station_service.cpp
    server/services/station_service.h
    server/services/telemetry_service.cpp
    server/services/telemetry_service.h
    server/services/user_session_service.cpp
    server/services/user_session_service.h
    server/services/user_service.cpp
    server/services/user_service.h
    server/services/wallet_service.cpp
    server/services/wallet_service.h
    server/services/linkage.h
    server/services/linkage.cpp
)
target_include_directories(charge_business PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_business PUBLIC charge_domain charge_repository Qt6::Core Qt6::Sql)
charge_platform_apply_project_options(charge_business)

add_library(charge_server_network STATIC
    server/network/request_router.cpp
    server/network/request_router.h
    server/network/tcp_server.cpp
    server/network/tcp_server.h
)
target_include_directories(charge_server_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_server_network PUBLIC
    charge_protocol charge_business Qt6::Core Qt6::Network Qt6::Sql
)
charge_platform_apply_project_options(charge_server_network)

qt_add_executable(charge_platform_server
    server/app/main.cpp
)
target_compile_definitions(charge_platform_server PRIVATE
    CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(charge_platform_server PRIVATE
    charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql
)
charge_platform_apply_project_options(charge_platform_server)

# Member 2 UI targets are part of the complete product build.  Keep the
# dependency order explicit: both desktop clients depend on charge_shared_ui.
add_subdirectory(clients/shared/ui)
add_subdirectory(clients/user)
add_subdirectory(clients/admin)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)

    qt_add_executable(smoke_test
        tests/unit/smoke_test.cpp
    )
    target_link_libraries(smoke_test PRIVATE Qt6::Core Qt6::Test)
    charge_platform_apply_project_options(smoke_test)

    add_test(NAME smoke_test COMMAND smoke_test)

    qt_add_executable(frame_codec_test
        tests/unit/frame_codec_test.cpp
    )
    target_include_directories(frame_codec_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(frame_codec_test PRIVATE charge_protocol Qt6::Core Qt6::Test)
    charge_platform_apply_project_options(frame_codec_test)
    add_test(NAME frame_codec_test COMMAND frame_codec_test)

    if(WIN32 AND EXISTS "D:/Git/bin/bash.exe")
        set(BASH_PROGRAM "D:/Git/bin/bash.exe")
    elseif(WIN32 AND EXISTS "C:/Program Files/Git/bin/bash.exe")
        set(BASH_PROGRAM "C:/Program Files/Git/bin/bash.exe")
    endif()
    if(NOT BASH_PROGRAM)
        find_program(BASH_PROGRAM NAMES bash bash.exe REQUIRED)
    endif()
    file(TO_CMAKE_PATH "${BASH_PROGRAM}" BASH_PROGRAM_CMAKE_PATH)

    qt_add_executable(schema_test
        tests/integration/schema_test.cpp
    )
    target_include_directories(schema_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(schema_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        CHARGE_PLATFORM_BASH_PROGRAM="${BASH_PROGRAM_CMAKE_PATH}"
    )
    target_link_libraries(schema_test PRIVATE charge_repository Qt6::Core Qt6::Sql Qt6::Test)
    charge_platform_apply_project_options(schema_test)
    add_test(NAME schema_test COMMAND schema_test)

    qt_add_executable(analytics_export_contract_test
        tests/integration/analytics_export_contract_test.cpp
    )
    target_compile_definitions(analytics_export_contract_test PRIVATE CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(analytics_export_contract_test PRIVATE Qt6::Core Qt6::Test)
    charge_platform_apply_project_options(analytics_export_contract_test)
    add_test(NAME analytics_export_contract_test COMMAND analytics_export_contract_test)

    qt_add_executable(server_health_test
        tests/integration/server_health_test.cpp
    )
    target_include_directories(server_health_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(server_health_test PRIVATE charge_server_network Qt6::Core Qt6::Network Qt6::Sql Qt6::Test)
    charge_platform_apply_project_options(server_health_test)
    add_test(NAME server_health_test COMMAND server_health_test)

    function(add_stage1_business_test target source)
        qt_add_executable(${target} ${source} ${ARGN})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${target} PRIVATE
            CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(${target} PRIVATE
            charge_repository Qt6::Core Qt6::Sql Qt6::Test
        )
        charge_platform_apply_project_options(${target})
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    add_stage1_business_test(user_service_test
        tests/unit/user_service_test.cpp
    )
    target_link_libraries(user_service_test PRIVATE charge_business Threads::Threads)

    add_stage1_business_test(geo_test tests/unit/geo_test.cpp)
    target_link_libraries(geo_test PRIVATE charge_domain)

    add_stage1_business_test(decimal_value_test
        tests/unit/decimal_value_test.cpp
    )
    target_link_libraries(decimal_value_test PRIVATE charge_domain)

    add_stage1_business_test(station_service_test
        tests/unit/station_service_test.cpp
    )
    target_link_libraries(station_service_test PRIVATE charge_business)

    add_stage1_business_test(order_state_test
        tests/unit/order_state_test.cpp
    )
    target_link_libraries(order_state_test PRIVATE charge_domain)

    add_stage1_business_test(reservation_race_test
        tests/integration/reservation_race_test.cpp
    )
    target_link_libraries(reservation_race_test PRIVATE charge_business Threads::Threads)

    add_stage1_business_test(payment_idempotency_test
        tests/integration/payment_idempotency_test.cpp
    )
    target_link_libraries(payment_idempotency_test PRIVATE charge_business)

    add_stage1_business_test(admin_service_test
        tests/integration/admin_service_test.cpp
    )
    target_link_libraries(admin_service_test PRIVATE charge_business)

    add_stage1_business_test(telemetry_service_test
        tests/unit/telemetry_service_test.cpp
    )
    target_link_libraries(telemetry_service_test PRIVATE charge_business)

    add_stage1_business_test(alarm_ticket_test
        tests/integration/alarm_ticket_test.cpp
    )
    target_link_libraries(alarm_ticket_test PRIVATE charge_business)

    qt_add_executable(telemetry_protocol_test
        tests/integration/telemetry_protocol_test.cpp
    )
    target_include_directories(telemetry_protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(telemetry_protocol_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(telemetry_protocol_test PRIVATE
        charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql Qt6::Test
    )
    charge_platform_apply_project_options(telemetry_protocol_test)
    add_test(NAME telemetry_protocol_test COMMAND telemetry_protocol_test)

    qt_add_executable(client_query_protocol_test
        tests/integration/client_query_protocol_test.cpp
    )
    target_include_directories(client_query_protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(client_query_protocol_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(client_query_protocol_test PRIVATE
        charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql Qt6::Test
    )
    charge_platform_apply_project_options(client_query_protocol_test)
    add_test(NAME client_query_protocol_test COMMAND client_query_protocol_test)

    qt_add_executable(user_authorization_protocol_test
        tests/integration/user_authorization_protocol_test.cpp
    )
    target_include_directories(user_authorization_protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(user_authorization_protocol_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(user_authorization_protocol_test PRIVATE
        charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql Qt6::Test
    )
    charge_platform_apply_project_options(user_authorization_protocol_test)
    add_test(NAME user_authorization_protocol_test COMMAND user_authorization_protocol_test)

    qt_add_executable(admin_operations_protocol_test
        tests/integration/admin_operations_protocol_test.cpp
    )
    target_include_directories(admin_operations_protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(admin_operations_protocol_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(admin_operations_protocol_test PRIVATE
        charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql Qt6::Test
    )
    charge_platform_apply_project_options(admin_operations_protocol_test)
    add_test(NAME admin_operations_protocol_test COMMAND admin_operations_protocol_test)

    qt_add_executable(linkage_contract_test
        tests/integration/linkage_contract_test.cpp
    )
    target_include_directories(linkage_contract_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(linkage_contract_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(linkage_contract_test PRIVATE
        charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql Qt6::Test
    )
    charge_platform_apply_project_options(linkage_contract_test)
    add_test(NAME linkage_contract_test COMMAND linkage_contract_test)

    add_test(NAME reset_demo_database_test
        COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/reset_demo_database_test.sh
    )

    qt_add_executable(member1_flow_test tests/e2e/member1_flow.cpp)
    target_include_directories(member1_flow_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(member1_flow_test PRIVATE
        CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(member1_flow_test PRIVATE
        charge_server_network charge_business Qt6::Core Qt6::Network Qt6::Sql
    )
    charge_platform_apply_project_options(member1_flow_test)
    add_test(NAME member1_flow_test COMMAND member1_flow_test)

    # M1-01：补充协议检查与启动配置测试注册进 CTest。
    # 解释器用 find_package 结果，服务端路径用生成器表达式，不写死任何本机路径；
    # 两个脚本都自行以 --port 0 + 临时库运行，不占用 48080，可与历史测试共存。
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME stage1_supplement_check
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/stage1_supplement_check.py
                --server $<TARGET_FILE:charge_platform_server>
    )
    add_test(NAME server_launch_config_test
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/server_launch_config_test.py
                --server $<TARGET_FILE:charge_platform_server>
    )
    # launch 测试读取当前构建目录的真实产物，不依赖 build/build-stage1 等目录名。

    add_subdirectory(tests/ui)
endif()
