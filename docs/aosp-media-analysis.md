# Media 源码分析

## 一、问题定义与范围
- 范围：当前仓库缺少 MediaCodec/Stagefright 主实现，无法在本仓库内闭环 media framework。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- App media API -> MediaCodec/Stagefright gap -> Surface/Audio path
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- Media 依赖 frameworks/av 大量 native 实现；当前仓库只保留零散接口，不足以闭环。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    App --> MediaAPI
    MediaAPI --> CodecGap
    CodecGap --> Output
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant App
    participant MediaAPI
    participant CodecGap
    participant Output
    App->>MediaAPI: 进入主链
    MediaAPI->>CodecGap: 状态推进
    CodecGap->>Output: 结果提交
```

## 六、关键代码详细分析
- 用户态 API 入口与 codec 管线关键实现均缺失。
- 因此本仓库无法独立解释编解码调度、NuPlayer、AudioFlinger 等核心机制。
- 如需 media 深挖，应补齐 frameworks/av 与相关 service 目录。

## 七、证据链（源码 + 运行时）
- 源码证据：`media/java/android/media/MediaCodec.java`
- 源码证据：`media/libstagefright/MediaCodec.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-media` 的覆盖状态为 `GAP`。
- 置信度：`Speculative`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
