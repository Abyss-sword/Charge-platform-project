# 成员1 · 第一阶段
# 编译选项：为所有目标统一应用告警等级——MSVC 用 /W4，其他编译器 -Wall -Wextra -Wpedantic，
# 编译期暴露可疑代码，提高可维护性。

function(charge_platform_apply_project_options target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
