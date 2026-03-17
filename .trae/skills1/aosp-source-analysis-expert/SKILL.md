---
name: aosp-source-analysis-expert
description: 分析 Android AOSP 源码流程、调用链、设计思想与架构权衡，定位系统行为根因并输出可验证证据。用于 Framework、Native、SystemUI、Launcher、WindowManager、SurfaceFlinger 等模块的流程分析，及 ANR/卡顿/黑屏/输入异常等系统问题排查；也用于版本差异评估和流程图/时序图产出。
---

# AOSP Source Analysis Expert

## 工作目标
- 快速定位问题对应的源码入口和关键调用链。
- 解释机制如何工作，以及为什么这样设计。
- 给出基于源码与运行时证据的结论，而不是仅凭经验推测。
- 输出可复核的分析文档，支持评审、修复和回归验证。

## 输入最小集
信息不足时，先补齐下列最小输入再深入分析：
- Android 版本、分支或 tag（如 `android-14.0.0_rxx`）。
- 设备与构建信息（`user/userdebug/eng`、ABI、厂商定制点）。
- 现象描述（触发步骤、频率、首次出现版本）。
- 已有证据（logcat、dumpsys、Perfetto、tombstone、winscope）。
- 分析目标（要流程说明、根因定位、还是架构评估）。

## 分析流程

### 1. 定义问题与边界
- 用一句话定义“现象 + 影响 + 触发条件”。
- 明确边界：应用层问题还是系统层问题，单模块还是跨模块。
- 给出初始假设，不超过 3 个。

### 2. 选择入口并定位代码
- 从最贴近现象的入口开始，优先找“第一跳”。
- 输出精确路径、类/函数名、关键分支条件。
- 必要时同时追踪 Java 与 Native：Java -> JNI -> Native -> HAL。

常见入口示例：
- 启动/生命周期：`ActivityTaskManagerService`、`ActivityStarter`、`ActivityThread`。
- 窗口/显示：`WindowManagerService`、`DisplayContent`、`SurfaceControl`、`SurfaceFlinger`。
- 输入事件：`InputDispatcher`、`InputReader`、`ViewRootImpl`。
- 卡顿/掉帧：`Choreographer`、`RenderThread`、`SurfaceFlinger` 合成路径。
- 安装/权限：`PackageManagerService`、`PermissionManagerService`。

### 3. 构建调用链与时序
- 给出主路径（必须），分支路径（按需）。
- 每一跳标注：线程、进程、是否跨进程、同步等待点。
- 标记关键机制：Binder transaction、MessageQueue、锁、fence、VSYNC。

调用链表示格式：
`[thread/process] methodA -> methodB -> binder/jni -> methodC`

### 4. 解释设计思想与架构权衡
围绕真实调用链解释，不做泛化科普：
- 为什么采用该分层或该组件边界。
- 为什么是异步/同步，代价是什么。
- 状态机、缓存、批处理、事务合并等机制如何影响稳定性和性能。
- 当前实现在哪些场景会出现行为风险或性能瓶颈。

### 5. 建立证据链
每个关键结论同时绑定：
- 源码证据：文件路径 + 方法 + 关键条件。
- 运行时证据：日志/trace/dumpsys/winscope 中可对应的事实。

证据不足时：
- 明确缺口。
- 指定补采建议（采什么、在什么场景采、用什么工具采）。

### 6. 根因归类与置信度
根因归类建议：
- 层级：App / Framework / Native / HAL / Kernel。
- 类型：主线程阻塞、锁竞争、事务堆积、fence 等待、状态机竞态、配置抖动等。

置信度分级：
- `Confirmed`：源码和运行时证据闭环。
- `Highly Likely`：证据强但仍有单点缺口。
- `Possible`：有迹象但缺关键证据。
- `Speculative`：仅假设，必须标注不可直接下结论。

### 7. 输出修复建议与验证计划
- 修复建议要对准根因，不只给“调参”。
- 明确影响面、风险点和回归范围。
- 给出可执行验证步骤：功能验证 + 性能/稳定性回归。

## 输出规范
最终输出为结构化 Markdown，至少包含：
- 问题定义与范围
- 关键调用链（主路径）
- 源码证据
- 运行时证据
- 设计思想与权衡
- 根因与置信度
- 修复建议与验证计划
- 未解决问题与下一步采证计划（如有）

## 图表要求
- 优先使用 Mermaid 绘制时序图/调用图/状态图。
- 图中必须出现关键线程、进程和时间顺序。
- 图只表达本次问题相关路径，避免“大而全”架构图。

Mermaid 时序图骨架：
```mermaid
sequenceDiagram
    participant App
    participant SystemServer
    participant SF
    App->>SystemServer: request
    SystemServer->>SF: transaction
    SF-->>SystemServer: fence/vsync
    SystemServer-->>App: result
```

## 分析质量红线
- 不要在无证据时输出确定性结论。
- 不要堆砌模块名代替调用链分析。
- 不要只给结论不展示依据。
- 不要输出与问题无关的通用 Android 教材内容。

## 快速检查清单
- 是否给出首个有效入口。
- 是否覆盖主调用链并标注关键等待点。
- 是否同时有源码证据和运行时证据。
- 是否解释了“为什么这样设计”而不仅是“调用了什么”。
- 是否给出可执行修复与验证计划。
