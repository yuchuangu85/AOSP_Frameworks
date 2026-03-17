# 工具与证据
<!-- source: 02-1.md -->

# 1. 目标

本 Skill 用于对 **AOSP Camera 子系统**进行系统化源码分析与问题定位，输出内容必须建立在**源码、日志、trace、dumpsys、配置文件、运行时行为**等可验证证据之上，而不是经验性猜测。

适用目标包括：

1. 理解 Camera 架构设计思想与模块边界。
2. 建立从 App 到 HAL 再到 Buffer / Surface / 编码链路的完整调用链。
3. 还原拍照、预览、录像、切换摄像头、Session 重建等关键时序。
4. 定位 Camera 打不开、预览不出图、首帧慢、拍照慢、录像异常、黑屏、花屏、卡住、流配置失败、metadata 异常等问题。
5. 分析 Camera 性能瓶颈，包括 open latency、configureStreams latency、first frame latency、capture latency、repeating request 抖动、buffer 堵塞等。
6. 分析 Camera HAL / Provider / Service / App 多层交互关系。
7. 输出架构图、时序图、调用链、证据链、根因和修复建议。

---


<!-- source: 07-51.md -->

# 5.1 理想输入

用户最好提供以下一种或多种材料：

- AOSP 代码路径 / 类名 / 函数名
- logcat
- bugreport
- dumpsys media.camera
- dumpsys SurfaceFlinger
- perfetto / systrace
- 复现步骤
- 问题现象描述
- 机型 / Android 版本 / camera id / 场景（预览 / 拍照 / 录像 / 切换）
- 错误码 / exception / tombstone / HAL 日志


<!-- source: 09-6.md -->

# 6. 输出要求

输出必须尽量包含以下内容：

1. **问题摘要**
2. **结论先行**
3. **完整调用链**
4. **关键模块职责**
5. **核心源码路径**
6. **关键时序图**
7. **状态机 / 生命周期分析**
8. **日志 / trace / dumpsys 证据**
9. **根因判断**
10. **修复建议**
11. **风险评估**
12. **可回归验证点**

---


<!-- source: 59-21-dumpsys-log-trace.md -->

# 21. dumpsys / log / trace 分析规范


<!-- source: 76-6.md -->

# 6. 证据链

- log：
- dumpsys：
- trace：
- metadata / stream：
