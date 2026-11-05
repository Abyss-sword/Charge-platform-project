add_library(charge_admin_ui STATIC
    admin_provider.h
    remote_admin_provider.cpp remote_admin_provider.h
    admin_mock_data_provider.cpp admin_mock_data_provider.h
    admin_pages.cpp admin_pages.h
    admin_analysis_page.cpp admin_analysis_page.h
    admin_main_window.cpp admin_main_window.h
    admin_mock_resources.qrc
)
set_target_properties(charge_admin_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(charge_admin_ui PUBLIC cxx_std_17)
target_include_directories(charge_admin_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_admin_ui PUBLIC
    charge_shared_ui
    charge_forecast_ui
    charge_service_client
    Qt6::Network
    Qt6::Charts
)
target_compile_definitions(charge_admin_ui PUBLIC ADMIN_HAS_CHARTS=1)
charge_platform_apply_project_options(charge_admin_ui)

qt_add_executable(charge_platform_admin main.cpp)
target_link_libraries(charge_platform_admin PRIVATE charge_admin_ui)
charge_platform_apply_project_options(charge_platform_admin)
