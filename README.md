# XC HWBP Debugger

LS/XC 硬件断点远程调试器。手机端运行 agent 监听 TCP 端口，PC 端 GUI 通过 `IP:端口` 直连，协议为 JSON Lines。

## 技术栈

- 手机端 agent：C++17 + Android NDK
- PC 端 GUI：C++17 + Qt Widgets
- 通信：TCP + JSON Lines
- 构建：CMake + GitHub Actions
- 编码：源码统一使用 UTF-8；Windows/MSVC 构建使用 `/utf-8`，保证中文 UI 字符串稳定

## 目录

- `agent/`：Android 端 `xc-hwbp-agent`
- `pc-client/`：Windows/Linux PC 端 `xc-hwbp-debugger`
- `shared/`：两端共享协议定义
- `tests/`：协议、agent 和 UI contract tests
- `docs/`：规格、计划和协议文档

## 本地构建 PC 客户端

```sh
cmake -S . -B build -DXC_BUILD_AGENT=OFF -DXC_BUILD_PC_CLIENT=ON
cmake --build build --parallel
```

## 本地运行测试

```sh
cmake -S . -B build-tests -DXC_BUILD_AGENT=OFF -DXC_BUILD_PC_CLIENT=OFF -DXC_BUILD_TESTS=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

## Android Agent 构建

通过 GitHub Actions 使用 Android NDK 交叉编译。

当前 agent 启动时会读取 `/proc/modules` 检查 `lsdriver` 是否已加载，并在控制台输出 `[driver]` 连接日志。`driver.status` 会返回 `module_loaded`、`proc_modules_readable`、`kernel_release` 和状态信息。

## 产物

- `xc-hwbp-agent-android-arm64`：Android arm64 手机端 agent
- `xc-hwbp-debugger-windows-exe`：Windows PC GUI exe
- `xc-hwbp-debugger-linux`：Linux PC GUI
