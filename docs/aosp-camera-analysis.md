# Camera 源码分析

## 一、问题定义与范围
- 范围：当前仓库只含 camera2 Java 入口，不含 CameraService/HAL 主体，属于部分覆盖。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- CameraManager -> camera2 API -> CameraService gap -> HAL gap
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- 相机主链高度依赖 frameworks/av 与 HAL；当前仓库只能分析 Framework API 面。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    App --> CameraManager
    CameraManager --> CameraServiceGap
    CameraServiceGap --> HALGap
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant App
    participant CameraManager
    participant CameraServiceGap
    participant HALGap
    App->>CameraManager: 进入主链
    CameraManager->>CameraServiceGap: 状态推进
    CameraServiceGap->>HALGap: 结果提交
```

## 六、关键代码详细分析
- CameraManager 负责服务发现与 openCamera 前的 Framework 侧封装。
- 真正的流配置、RequestThread、设备状态机在缺失的 CameraService/HAL 中。
- 因此这里只能给出 API 入口与依赖关系，不能给拍照黑屏类闭环结论。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/hardware/camera2/CameraManager.java`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-camera` 的覆盖状态为 `PARTIAL`。
- 置信度：`Possible`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
