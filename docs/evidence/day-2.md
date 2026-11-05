# D2 2026-09-04

- 需求编号：Task 2、Task 3、Task 4
- 主负责人 / 复核人：成员1 / 待成员2、成员3交叉复核
- 分支：`feature/member1-d1-d3`
- 测试先行：先加入帧、数据库、分析契约和真实 TCP 健康检查测试。
- 首次构建结果：按预期失败；编译器明确报告缺少 `shared/protocol/frame_codec.h` 与 `server/repositories/database.h`。
- 实现范围：4字节长度帧、固定错误码、11表SQLite迁移、幂等种子、`charge_event_v1`、`PING/PONG`。
- 完成验证命令：

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R frame_codec_test --output-on-failure
ctest --test-dir build -R "schema_test|analytics_export_contract_test" --output-on-failure
ctest --test-dir build -R server_health_test --output-on-failure
```

- 实际结果：Ubuntu Qt 6 环境配置、编译成功；5 项测试全部通过，0 项失败，总耗时约 0.08 秒。`server_health_test` 连续运行 10 次均通过。
- 数据库结果：11 张核心表齐全；固定种子重复执行后仍为 3 站、12 桩、20 用户、30 订单、3 告警；重复手机号、断裂外键和非法订单状态均被拒绝。
- 服务结果：两个同时连接分别建立工作线程，断开后线程数归零；`PING` 返回原请求号和 `PONG`；未知动作返回 `PARAM_INVALID` 后仍可继续服务。
- 已知提示：CMake 只提示未找到可选 Vulkan 头文件；本项目未使用 Vulkan，不影响构建和测试。
- 未解决问题和影响：Qt 两端、模拟器和 Web 大屏由成员2、3按冻结契约接入，不属于本次成员1代码范围。
- 次日依赖：9 月 5 日在主线复跑固定种子预检。
