# xc-hwbp-debugger 设计规格

## 目标

构建一个 PC GUI + Android agent 的硬件断点远程调试器。手机端 agent 控制 `lsdriver.ko`，PC 端 GUI 通过局域网 TCP 直连 agent，实现断点设置、命中记录查看和寄存器修改。

## 架构

- `agent/xc-hwbp-agent`：Android 端 C++17 可执行文件，监听 TCP 端口，后续接入 lsdriver 共享内存。
- `pc-client/xc-hwbp-debugger`：PC 端 C++17 + EUI-NEO GUI。
- `shared/`：共享命令名、协议版本、端口和寄存器定义。

## 连接

采用 IDA remote server 风格。手机端监听端口，PC 端输入 `IP:端口` 直连。不实现 token、证书或登录。

## 协议

第一版使用 TCP + JSON Lines。每行一个 JSON 请求或响应。

基础命令：

- `hello`
- `driver.status`
- `breakpoint.info`
- `breakpoint.set`
- `breakpoint.remove`
- `records.get`
- `register.write`
- `register.readonly`

## 第一阶段范围

创建新仓库、CMake 工程、Android agent target、PC GUI target、GitHub Actions。骨架需要可编译，并提供 hello/status stub。
