# Android 一帧的旅程

入口：[index.html](index.html)。使用 Chrome、Safari 等现代浏览器直接打开即可，无需服务端、构建、网络或第三方运行时。页面上方首先展示**进程 / 线程 / 操作动画**，区分应用进程、SurfaceFlinger 进程、GPU / Composer HAL / 显示设备；下方再用八站流程故事和逐事件源码讲解。原跨线程时序图按需展开，用于核对并发与示意耗时。

```sh
open docs/android-frame-pipeline/index.html
```

## 展示范围

以**已附着、可见、启用硬件加速且已有稳定 Surface 的普通 View 窗口**为主线，默认展开 45 个事件：

- `invalidate` 与 `requestLayout`、合帧、消息同步屏障。
- Choreographer 请求下一次 VSync、native FD / JNI 回调、异步消息与 FrameTimeline。
- 当前源码的五类 callback：Input、Animation、Insets Animation、Traversal、Commit。
- 按需 measure / layout、pre-draw、DisplayList / RenderNode 录制与复用。
- UI → JNI → RenderProxy → DrawFrameTask、RenderThread 场景树同步与提前放行 UI。
- GraphicBuffer dequeue、SkiaGL / SkiaVulkan、GPU 异步写入、queue 与 acquire fence。
- 客户端 BLAST 消费 BufferQueue、SurfaceControl buffer transaction、SF 接收事务。
- SF 软件 VSync、Scheduler、commit、更新层级快照、latch、composite。
- HWC 策略协商、可选 RenderEngine CLIENT 合成、DEVICE / CLIENT / 混合模式。
- Present、显示控制器、面板扫描示意、旧 buffer 的 release callback / fence 与复用。

提供完整刷新、仅 invalidate、UI 超时、应用 GPU 超时、SF 合成超时共 5 个场景；可组合 3 种合成策略与 60 / 90 / 120Hz。

## 操作

- 默认暂停。先看顶部动画的三个边界：**应用进程**中的 UI 主线程、RenderThread 和 BLAST；**SurfaceFlinger 进程**中的 Scheduler / EventThread 与合成主线；**设备 / HAL** 中的 GPU、Composer HAL / DPU 和显示面板。各节点显示当前或最近一次源码操作，底部显示 APP VSync、RenderNode、GraphicBuffer、acquire fence、SurfaceControl 事务和 Present / scanout 的交接状态。高亮边界与节点标记当前讲解处，虚框标记同时运行。点击节点可跳到该执行者的代表事件。GPU / HAL 区域是设备侧概念分组，**不是同一个进程**；路线不表示像素作为单个包裹跨进程移动。
- 动画下方是“请求 → 节拍 → 回调 → 录制 → 渲染 → 交接 → 合成 → 显示”八站学习路线；点击站点可跳到该阶段，**直接在流程下方的“源码过程”逐事件阅读**。选中事件后会同步展示函数路线、代码行为、因果交接、易错点和源码链接；点击依赖 / 下游可跨站追踪。概念路线不是跨线程的严格串行调用栈。
- **逐事件讲解**每事件约停留 2.8 秒；**连续时序**观察并发关系。速度可调。
- 点击时间条、阶段或完整调用链目录跳转。滑块可定位任意时刻。
- 原时序图收在“展开跨线程精确时序”中，供需要核对并发和示意时间时使用；它保持横向滚动、图下方“横向浏览”滑块、缩放和跟随播放。事件标签完整显示，多行错开放置以避免相互遮挡；顶边细线才对应示意持续时间。
- 下方 View 树、BQ 所有权、acquire / release fence、屏幕扫描与 callback 队列同步变化。
- `Space` 播放 / 暂停，`← / →` 步进，`Home / End` 首尾，`?` 帮助，`Esc` 关闭资料对话框。
- 右上角“源码与资料”列出本地源码路径 / 行号、同版本上游入口和实际查阅过的官方文档；每事件也有来源链接。

## 证据、版本与建模边界

- 版本名来自当前仓库 README：`android16-qpr2-release`，**不等价于已验证完整 repo manifest**。
- 创建时仓库 HEAD：`e4b38f4859aa7256724c668b8415c8185e35face`；文案以实际工作树源码核对，保留原有脏改动。
- 2026-10-21 查阅 Android 官方图形架构 / VSync / BufferQueue / 同步 / Choreographer 资料。官方 `.com` 连接超时时使用官方 `.google.cn` 镜像；HTML 内保留完整链接及历史实现警告。
- Android 9 的“四类 callback”仅作为历史对照。Android 10 已有 Insets Animation，本地 Android 16 采用五类。
- 本页只新增教学文档与可选验证脚本，**不修改 `base/`、`native/` 或既有文档**。
- **所有场景的时长、相位、槽位数量均为教学参数，不是真机测量。**刷新率切换只按比例缩放时轴，不预测固定工作负载在不同 Hz 下的真实性能。
- 时间条表示展开的教学子步骤，不是函数调用栈完整范围。`doFrame` / `TRAVERSAL` / `SF.commit` / `composite` 等父函数包含后续展示的内部动作。
- GPU 迟到场景明确选择保守不采用未 signal buffer 的分支，不能据此断言系统禁止 `latch-unsignaled`。
- 混合模式假设被跟踪的 View 窗口为 DEVICE、其它示意层为 CLIENT。全 CLIENT 的旧源 buffer release 与扫描 client target 区分，回交时依赖已 signal。
- BQ `FREE` 是槽位所有权状态，不等于任意时刻可无条件覆盖像素；dequeue 的 fence 仍需满足。
- present 调用返回、present fence 时间点、scanout、像素响应不是同一时刻。面板部分是通用硬件示意，厂商驱动 / 面板实现在本 checkout 中不完整。
- 未按每帧固定步骤演示窗口创建 / WMS relayout，也未完整展开软件 Canvas、SurfaceView / TextureView、视频、受保护内容、多屏、VRR、厂商内部驱动等分支。相关限制在页面中明确说明。

## 可复现浏览器验证

可选 QA 依赖仅安装在 `/tmp`；HTML 本身无任何依赖，也不新增仓库 `package.json` 或锁文件。

```sh
npm install --prefix /tmp/android-frame-pipeline-test --no-save playwright@1.58.2
NODE_PATH=/tmp/android-frame-pipeline-test/node_modules \
  node docs/android-frame-pipeline/verify.cjs
```

默认使用本机 `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome`。可用 `CHROME_PATH` 覆盖浏览器路径、`ARTIFACT_DIR` 指定截图 / 报告目录，默认输出到系统临时目录下的 `android-frame-pipeline-qa`。

验证包含 45 种场景组合、上方三边界动画与线程操作 / 交接状态、下方八站流程与源码过程导航、可选精确时序、5 callback、全部事件跳转、滑块、UI / RT 同步边界、buffer / fence 状态、搜索、源码路径、对话框、键盘、两种播放、缩放依赖箭头、390 / 768 / 1600px 布局和 reduced-motion。它证明浏览器教学模型行为，不证明 Android 真机帧时序或面板电气行为。
