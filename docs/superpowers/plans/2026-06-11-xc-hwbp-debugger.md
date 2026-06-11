# xc-hwbp-debugger 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 构建 `xc-hwbp-debugger` 初始双端工程骨架。

**架构：** 同仓库包含 Android agent、PC GUI 和 shared 协议层。agent 监听 TCP，PC GUI 使用 EUI-NEO，双方通过 JSON Lines 通信。

**技术栈：** C++17、CMake、Android NDK、EUI-NEO、GitHub Actions。

---

### 任务 1：初始化仓库骨架

**文件：**
- 创建：`CMakeLists.txt`
- 创建：`agent/CMakeLists.txt`
- 创建：`pc-client/CMakeLists.txt`
- 创建：`shared/include/xc/protocol.hpp`

- [x] **步骤 1：创建顶层 CMake**
- [x] **步骤 2：创建 agent target**
- [x] **步骤 3：创建 PC GUI target**
- [x] **步骤 4：创建 shared 协议头**

### 任务 2：创建最小可运行程序

**文件：**
- 创建：`agent/src/main.cpp`
- 创建：`pc-client/src/main.cpp`

- [x] **步骤 1：agent 实现 TCP 监听和 hello/status stub**
- [x] **步骤 2：PC GUI 显示标题、连接目标和骨架状态**

### 任务 3：创建 CI

**文件：**
- 创建：`.github/workflows/build.yml`

- [x] **步骤 1：构建 PC GUI**
- [x] **步骤 2：使用 Android NDK 构建 agent**
- [x] **步骤 3：上传 artifacts**
