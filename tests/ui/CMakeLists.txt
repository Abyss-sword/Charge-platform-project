qt_add_executable(navigation_smoke_test navigation_smoke_test.cpp)
target_link_libraries(navigation_smoke_test PRIVATE charge_user_ui Qt6::Test)
charge_platform_apply_project_options(navigation_smoke_test)
add_test(NAME navigation_smoke_test COMMAND navigation_smoke_test)
set_tests_properties(navigation_smoke_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(navigation_provider_test navigation_provider_test.cpp)
target_link_libraries(navigation_provider_test PRIVATE charge_user_ui Qt6::Test)
charge_platform_apply_project_options(navigation_provider_test)
add_test(NAME navigation_provider_test COMMAND navigation_provider_test)
set_tests_properties(navigation_provider_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(shared_ui_test shared_ui_test.cpp)
target_link_libraries(shared_ui_test PRIVATE charge_user_ui Qt6::Test)
charge_platform_apply_project_options(shared_ui_test)
add_test(NAME shared_ui_test COMMAND shared_ui_test)
set_tests_properties(shared_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(admin_smoke_test admin_smoke_test.cpp)
target_link_libraries(admin_smoke_test PRIVATE charge_admin_ui Qt6::Test)
charge_platform_apply_project_options(admin_smoke_test)
add_test(NAME admin_smoke_test COMMAND admin_smoke_test)
set_tests_properties(admin_smoke_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(mock_data_provider_test mock_data_provider_test.cpp)
target_link_libraries(mock_data_provider_test PRIVATE charge_user_ui charge_admin_ui Qt6::Test)
charge_platform_apply_project_options(mock_data_provider_test)
add_test(NAME mock_data_provider_test COMMAND mock_data_provider_test)
set_tests_properties(mock_data_provider_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(user_login_test user_login_test.cpp)
target_link_libraries(user_login_test PRIVATE charge_user_ui Qt6::Test)
charge_platform_apply_project_options(user_login_test)
add_test(NAME user_login_test COMMAND user_login_test)
set_tests_properties(user_login_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(service_client_test service_client_test.cpp fake_tcp_server.h)
target_link_libraries(service_client_test PRIVATE
    charge_user_ui charge_protocol Qt6::Test Qt6::Network)
charge_platform_apply_project_options(service_client_test)
add_test(NAME service_client_test COMMAND service_client_test)

qt_add_executable(remote_user_login_provider_test
    remote_user_login_provider_test.cpp fake_tcp_server.h)
target_link_libraries(remote_user_login_provider_test PRIVATE
    charge_user_ui charge_protocol Qt6::Test Qt6::Network)
charge_platform_apply_project_options(remote_user_login_provider_test)
add_test(NAME remote_user_login_provider_test COMMAND remote_user_login_provider_test)

qt_add_executable(user_runtime_config_test user_runtime_config_test.cpp)
target_link_libraries(user_runtime_config_test PRIVATE charge_user_ui Qt6::Test)
charge_platform_apply_project_options(user_runtime_config_test)
add_test(NAME user_runtime_config_test COMMAND user_runtime_config_test)

qt_add_executable(station_query_test station_query_test.cpp fake_tcp_server.h)
target_link_libraries(station_query_test PRIVATE
    charge_user_ui charge_protocol Qt6::Test Qt6::Network)
charge_platform_apply_project_options(station_query_test)
add_test(NAME station_query_test COMMAND station_query_test)
set_tests_properties(station_query_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(user_order_provider_test
    user_order_provider_test.cpp fake_tcp_server.h)
target_link_libraries(user_order_provider_test PRIVATE
    charge_user_ui charge_protocol Qt6::Test Qt6::Network)
charge_platform_apply_project_options(user_order_provider_test)
add_test(NAME user_order_provider_test COMMAND user_order_provider_test)
set_tests_properties(user_order_provider_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(user_query_provider_test
    user_query_provider_test.cpp fake_tcp_server.h)
target_link_libraries(user_query_provider_test PRIVATE
    charge_user_ui charge_protocol Qt6::Test Qt6::Network)
charge_platform_apply_project_options(user_query_provider_test)
add_test(NAME user_query_provider_test COMMAND user_query_provider_test)
set_tests_properties(user_query_provider_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(remote_admin_provider_test
    remote_admin_provider_test.cpp fake_tcp_server.h)
target_link_libraries(remote_admin_provider_test PRIVATE
    charge_admin_ui charge_protocol Qt6::Test Qt6::Network)
charge_platform_apply_project_options(remote_admin_provider_test)
add_test(NAME remote_admin_provider_test COMMAND remote_admin_provider_test)
set_tests_properties(remote_admin_provider_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(user_polling_test user_polling_test.cpp)
target_link_libraries(user_polling_test PRIVATE charge_user_ui Qt6::Test)
charge_platform_apply_project_options(user_polling_test)
add_test(NAME user_polling_test COMMAND user_polling_test)
set_tests_properties(user_polling_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(forecast_contract_test forecast_contract_test.cpp)
target_compile_definitions(forecast_contract_test PRIVATE
    CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(forecast_contract_test PRIVATE charge_forecast_contract Qt6::Test)
charge_platform_apply_project_options(forecast_contract_test)
add_test(NAME forecast_contract_test COMMAND forecast_contract_test)

qt_add_executable(analysis_state_presenter_test analysis_state_presenter_test.cpp)
target_link_libraries(analysis_state_presenter_test PRIVATE
    charge_shared_ui charge_forecast_contract Qt6::Test)
charge_platform_apply_project_options(analysis_state_presenter_test)
add_test(NAME analysis_state_presenter_test COMMAND analysis_state_presenter_test)
set_tests_properties(analysis_state_presenter_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(analysis_pages_test analysis_pages_test.cpp)
target_link_libraries(analysis_pages_test PRIVATE
    charge_user_ui charge_admin_ui charge_forecast_contract Qt6::Test Qt6::Network)
charge_platform_apply_project_options(analysis_pages_test)
add_test(NAME analysis_pages_test COMMAND analysis_pages_test)
set_tests_properties(analysis_pages_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Stable Stage2 acceptance groups. Keep test executable names unchanged so older
qt_add_executable(analysis_http_client_test analysis_http_client_test.cpp)
target_link_libraries(analysis_http_client_test PRIVATE charge_analysis_http Qt6::Test)
target_compile_definitions(analysis_http_client_test PRIVATE CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
charge_platform_apply_project_options(analysis_http_client_test)
add_test(NAME analysis_http_client_test COMMAND analysis_http_client_test)
set_tests_properties(analysis_http_client_test PROPERTIES LABELS "member2;shared;analysis;network")

qt_add_executable(forecast_pages_http_test forecast_pages_http_test.cpp)
target_link_libraries(forecast_pages_http_test PRIVATE charge_user_ui charge_admin_ui Qt6::Test)
target_compile_definitions(forecast_pages_http_test PRIVATE CHARGE_PLATFORM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
charge_platform_apply_project_options(forecast_pages_http_test)
add_test(NAME forecast_pages_http_test COMMAND forecast_pages_http_test)
set_tests_properties(forecast_pages_http_test PROPERTIES
    LABELS "member2;user;admin;analysis;network;lifecycle;boundary;responsive"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# scripts remain valid; labels provide reproducible Member2 and feature subsets.
set_tests_properties(
    navigation_smoke_test
    PROPERTIES LABELS "member2;user;navigation;lifecycle;boundary;responsive")
set_tests_properties(
    navigation_provider_test
    PROPERTIES LABELS "member2;user;navigation")
set_tests_properties(
    shared_ui_test
    PROPERTIES LABELS "member2;shared")
set_tests_properties(
    admin_smoke_test
    PROPERTIES LABELS "member2;admin;navigation;lifecycle;boundary;responsive")
set_tests_properties(
    mock_data_provider_test
    PROPERTIES LABELS "member2;user;admin;boundary")
set_tests_properties(
    user_login_test
    PROPERTIES LABELS "member2;user;lifecycle;boundary;responsive")
set_tests_properties(
    service_client_test
    PROPERTIES LABELS "member2;shared;network")
set_tests_properties(
    remote_user_login_provider_test
    PROPERTIES LABELS "member2;user;network")
set_tests_properties(
    user_runtime_config_test
    PROPERTIES LABELS "member2;user;network;boundary")
set_tests_properties(
    station_query_test
    PROPERTIES LABELS "member2;user;network;lifecycle")
set_tests_properties(
    user_order_provider_test
    PROPERTIES LABELS "member2;user;network;lifecycle")
set_tests_properties(
    user_query_provider_test
    PROPERTIES LABELS "member2;user;network;lifecycle;responsive")
set_tests_properties(
    remote_admin_provider_test
    PROPERTIES LABELS "member2;admin;network;lifecycle")
set_tests_properties(
    user_polling_test
    PROPERTIES LABELS "member2;user;lifecycle")
set_tests_properties(
    forecast_contract_test
    PROPERTIES LABELS "member2;shared;analysis")
set_tests_properties(
    analysis_state_presenter_test
    PROPERTIES LABELS "member2;shared;analysis")
set_tests_properties(
    analysis_pages_test
    PROPERTIES LABELS "member2;user;admin;analysis;navigation;lifecycle;boundary;responsive")
