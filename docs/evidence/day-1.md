# 第 1 天：工程构建基线

## 本次范围

- 建立 C++17 与 Qt 6.2+ 的统一 CMake 构建入口。
- 添加首个 Qt 自动冒烟测试。
- 添加不包含真实密钥的本地配置模板。

## 测试先行证据

在只有 `tests/unit/smoke_test.cpp`、尚无 `CMakeLists.txt` 时执行：

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
```

结果：按预期失败，CMake 明确提示项目根目录缺少 `CMakeLists.txt`。

## 构建与测试证据

加入最小构建配置后执行：

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

结果：配置和编译成功；`smoke_test` 通过，汇总为 `100% tests passed, 0 tests failed out of 1`。

## 已知提示

CMake 提示未找到可选的 Vulkan 头文件。当前工程不使用 Vulkan，Qt Core、Network、Sql、Test、Widgets、Charts 与 WebEngineWidgets 均已成功解析，因此不影响本阶段构建与测试。

## 当前结论

charge_platform_project 已具备可重复执行的最小 Qt/CMake 构建与自动测试入口。此结论只覆盖工程骨架，不代表业务功能已经实现。

## D1 补充交付

- 项目根目录已统一为 `/home/bit/projects/charge_platform_project`。
- 第一阶段内验/验收固定为 9 月 9 日、9 月 10 日；全量内验/验收固定为 9 月 16 日、9 月 17 日。
- 已补充 `docs/protocol.md`、`docs/interface-change-template.md` 与 `docs/task-board.md`，作为三端并行开发的冻结输入。
- D2 新测试先于实现写入，首次构建因缺少 `frame_codec.h` 与 `database.h` 按预期失败，证明测试会真实约束后续代码。
