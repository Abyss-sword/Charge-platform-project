add_library(charge_user_ui STATIC
    shell/main_window.cpp shell/main_window.h
    shell/mobile_page_header.cpp shell/mobile_page_header.h
    shell/bottom_navigation_bar.cpp shell/bottom_navigation_bar.h
    shell/user_application_flow.cpp shell/user_application_flow.h
    shell/primary_page.h shell/mobile_metrics.h
    adapters/mock_data_provider.cpp adapters/mock_data_provider.h
    adapters/mock_station_provider.cpp adapters/mock_station_provider.h
    adapters/station_provider.h
    adapters/user_login_provider.h
    adapters/mock_user_login_provider.cpp adapters/mock_user_login_provider.h
    adapters/remote_user_login_provider.cpp adapters/remote_user_login_provider.h
    adapters/user_account_provider.h
    adapters/remote_user_account_provider.cpp adapters/remote_user_account_provider.h
    adapters/remote_station_provider.cpp adapters/remote_station_provider.h
    adapters/navigation_provider.cpp adapters/navigation_provider.h
    adapters/user_order_provider.h
    adapters/remote_user_order_provider.cpp adapters/remote_user_order_provider.h
    models/user_context.h
    models/user_account.h
    models/station.h
    models/user_order.h
    pages/preview_page.cpp pages/preview_page.h
    pages/home_page.cpp pages/home_page.h
    pages/station_list_page.cpp pages/station_list_page.h
    pages/active_order_page.cpp pages/active_order_page.h
    pages/charging_history_page.cpp pages/charging_history_page.h
    pages/profile_page.cpp pages/profile_page.h
    pages/user_analysis_page.cpp pages/user_analysis_page.h
    pages/flow_pages.cpp pages/flow_pages.h
    pages/user_login_page.cpp pages/user_login_page.h
    adapters/user_mock_resources.qrc
)
set_target_properties(charge_user_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(charge_user_ui PUBLIC cxx_std_17)
target_include_directories(charge_user_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_user_ui PUBLIC charge_shared_ui charge_service_client charge_forecast_ui)
if(TARGET Qt6::WebEngineWidgets)
    target_compile_definitions(charge_user_ui PUBLIC CHARGE_HAS_WEBENGINE=1)
    target_link_libraries(charge_user_ui PUBLIC Qt6::WebEngineWidgets)
endif()
charge_platform_apply_project_options(charge_user_ui)
qt_add_executable(charge_platform_user main.cpp)
target_link_libraries(charge_platform_user PRIVATE charge_user_ui)
charge_platform_apply_project_options(charge_platform_user)
