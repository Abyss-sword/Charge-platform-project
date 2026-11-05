add_library(charge_service_client STATIC
    ../network/service_client.cpp ../network/service_client.h
    ../config/user_runtime_config.cpp ../config/user_runtime_config.h
)
set_target_properties(charge_service_client PROPERTIES AUTOMOC ON)
target_compile_features(charge_service_client PUBLIC cxx_std_17)
target_include_directories(charge_service_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(charge_service_client PUBLIC charge_protocol Qt6::Network)
charge_platform_apply_project_options(charge_service_client)

add_library(charge_forecast_contract STATIC
    ../analysis/forecast_model.cpp ../analysis/forecast_model.h
    ../analysis/forecast_parser.cpp ../analysis/forecast_parser.h
)
target_compile_features(charge_forecast_contract PUBLIC cxx_std_17)
target_include_directories(charge_forecast_contract PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(charge_forecast_contract PUBLIC Qt6::Core)
charge_platform_apply_project_options(charge_forecast_contract)

add_library(charge_shared_ui STATIC
    theme.cpp theme.h ui.qrc
    widgets/status_badge.cpp widgets/status_badge.h
    widgets/page_state_widget.cpp widgets/page_state_widget.h
    widgets/page_scaffold.cpp widgets/page_scaffold.h
    widgets/confirm_dialog.cpp widgets/confirm_dialog.h
    widgets/toast.cpp widgets/toast.h
    widgets/enum_labels.cpp widgets/enum_labels.h
    widgets/metric_card.cpp widgets/metric_card.h
    widgets/analysis_state_presenter.cpp widgets/analysis_state_presenter.h
    widgets/motion.h
)
set_target_properties(charge_shared_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(charge_shared_ui PUBLIC cxx_std_17)
target_include_directories(charge_shared_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_shared_ui PUBLIC
    charge_forecast_contract Qt6::Core Qt6::Widgets)
charge_platform_apply_project_options(charge_shared_ui)

add_library(charge_analysis_http STATIC
    ../analysis/analysis_http_client.cpp ../analysis/analysis_http_client.h)
set_target_properties(charge_analysis_http PROPERTIES AUTOMOC ON)
target_compile_features(charge_analysis_http PUBLIC cxx_std_17)
target_include_directories(charge_analysis_http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(charge_analysis_http PUBLIC charge_forecast_contract Qt6::Network)
charge_platform_apply_project_options(charge_analysis_http)

add_library(charge_forecast_ui STATIC
    widgets/forecast_panel.cpp widgets/forecast_panel.h
    widgets/peak_panel.cpp widgets/peak_panel.h
    widgets/recommendation_panel.cpp widgets/recommendation_panel.h)
set_target_properties(charge_forecast_ui PROPERTIES AUTOMOC ON)
target_compile_features(charge_forecast_ui PUBLIC cxx_std_17)
target_include_directories(charge_forecast_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(charge_forecast_ui PUBLIC charge_shared_ui charge_analysis_http Qt6::Charts)
charge_platform_apply_project_options(charge_forecast_ui)
