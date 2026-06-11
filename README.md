# XC HWBP Debugger

LS/XC 硬件断点远程调试器。项目采用 IDA remote server 风格：手机端运行 agent 监听 TCP 端口，PC 端 GUI 通过 `IP:端口` 直连。

## 技术栈

- 手机端 agent：C++17 + Android NDK
- PC 端 GUI：C++17 + EUI-NEO
- 通信：TCP + JSON Lines
- 构建：CMake + GitHub Actions
- 鉴权：不做，保持 IDA 风格直连

## 目录

- `agent/`：Android 端 `xc-hwbp-agent`
- `pc-client/`：PC 端 `xc-hwbp-debugger`
- `shared/`：两端共享协议定义
- `pc-client/` 通过 CMake FetchContent 拉取 EUI-NEO
- `docs/`：规格、计划和协议文档

## 本地构建 PC 客户端

```sh
cmake -S . -B build -DXC_BUILD_AGENT=OFF -DXC_BUILD_PC_CLIENT=ON -DGLFW_BUILD_WAYLAND=OFF
cmake --build build --parallel
```

## Android Agent 构建

通过 GitHub Actions 使用 Android NDK 交叉编译。

当前 agent 启动时会读取 `/proc/modules` 检查 `lsdriver` 是否已加载，并在控制台输出 `[driver]` 连接日志。`driver.status` 会返回 `module_loaded`、`proc_modules_readable`、`kernel_release` 和状态信息。后续再接入完整 lsdriver 共享内存协议。

## 产物

- `xc-hwbp-agent-android-arm64`：Android arm64 手机端 agent。
- `xc-hwbp-debugger-windows-exe`：Windows PC GUI exe。
- `xc-hwbp-debugger-linux`：Linux PC GUI。
