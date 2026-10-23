/* ============================================================
   Frame Pipeline Studio · step catalogue
   One entry per causal hand-off of a single frame. Timing is NOT
   stored here; fp-model.js computes t0/t1 for every frame from the
   VSYNC parameters. `from` lists causal predecessors (drawn as arrows),
   `ipc` marks a process boundary (dashed arrow).
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';

  FP.LANES = [
    { key: 'UI',      name: 'APP · UI Thread',    sub: 'Choreographer / ViewRootImpl',  color: 'var(--lane-ui)' },
    { key: 'RT',      name: 'APP · RenderThread', sub: 'DrawFrameTask / CanvasContext', color: 'var(--lane-rt)' },
    { key: 'GPU',     name: 'GPU',                sub: '命令执行 → acquire fence',       color: 'var(--lane-gpu)' },
    { key: 'BQ',      name: 'BufferQueue slots',  sub: '3 个 slot 的所有权',             color: 'var(--lane-bq)' },
    { key: 'SCHED',   name: 'SF Scheduler',       sub: 'VSYNC-app / VSYNC-sf 唤醒点',    color: 'var(--lane-sched)' },
    { key: 'SF',      name: 'SurfaceFlinger',     sub: 'commit / composite / HWC',       color: 'var(--lane-sf)' },
    { key: 'DISPLAY', name: 'Display',            sub: 'HW VSYNC · 正在扫描的帧',        color: 'var(--lane-display)' }
  ];
  FP.LANE = {};
  FP.LANES.forEach(function (l) { FP.LANE[l.key] = l; });

  // Theme-aware: values live in fp-studio.css (dark) and theme-light.css.
  FP.FRAME_COLORS = ['var(--f0)', 'var(--f1)', 'var(--f2)', 'var(--f3)', 'var(--f4)'];

  FP.STEPS = [
    { id: 'req', lane: 'UI', where: 'App 进程 · UI 主线程', stage: 1, short: '请求刷新', title: '请求刷新：invalidate / requestLayout → scheduleTraversals',
      chain: ['View.requestLayout() / invalidate()', 'ViewRootImpl.scheduleTraversals()', 'ViewRootImpl.postTraversalBarrier()', 'Choreographer.postVsyncCallback(CALLBACK_TRAVERSAL)'],
      packet: 'mTraversalScheduled = true · 同步屏障 · TraversalCallback 入 CALLBACK_TRAVERSAL 队列',
      desc: '脏标记向上冒泡到 ViewRootImpl；scheduleTraversals() 用 mTraversalScheduled 合并本帧内的多次请求，插入同步屏障，并把 TraversalCallback 放进 CALLBACK_TRAVERSAL 队列。此刻还没有任何 measure / draw。',
      hint: '连续动画时，下一帧的请求通常就发生在上一帧的 ANIMATION 回调里。' },
    { id: 'vreq', lane: 'UI', where: 'App 进程 · UI 主线程 → binder', stage: 2, short: '申请VSYNC', from: ['req'], ipc: true, title: 'Choreographer 申请下一个 VSYNC-app',
      chain: ['Choreographer.scheduleFrameLocked()', 'Choreographer.scheduleVsyncLocked()', 'DisplayEventDispatcher::scheduleVsync()', 'DisplayEventReceiver::requestNextVsync()  // binder → EventThread'],
      packet: 'requestNextVsync()：EventThread 把该连接的 VSyncRequest 置为 Single',
      desc: '一次申请只换一次回调。EventThread 收到 requestNextVsync 后，通过 VSyncDispatchTimerQueue 按 wakeup = vsync − workDuration − readyDuration 预约唤醒。',
      hint: '不申请就不会有回调：静止界面不消耗 VSYNC。' },
    { id: 'vapp', lane: 'SCHED', where: 'SurfaceFlinger 进程 · EventThread（app）', stage: 2, short: 'VSYNC-app', from: ['vreq'], ipc: true, vsync: 'VSYNC-app', title: 'VSYNC-app 唤醒：EventThread 分发事件',
      chain: ['VSyncDispatchTimerQueue::timerCallback()', 'EventThread::onVsync()', 'EventThread::dispatchEvent()', 'DisplayEventReceiver::sendEvents()  // BitTube'],
      packet: 'VSYNC 事件：timestamp = 唤醒时刻 · vsyncId · expectedPresentationTime · deadline',
      desc: 'app 的唤醒点 = 预期上屏时刻 − appWorkDuration − sfWorkDuration。事件经 BitTube（socket）写给 app 进程；header.timestamp 是唤醒时刻，预测的上屏时刻放在 FrameTimeline 里。',
      hint: '相位差 = appWorkDuration，它就是 app 本帧的生产预算。' },
    { id: 'doframe', lane: 'UI', where: 'App 进程 · UI 主线程（Looper 异步消息）', stage: 3, short: 'doFrame', from: ['vapp'], title: 'Choreographer.doFrame：INPUT / ANIMATION / INSETS_ANIMATION',
      chain: ['DisplayEventDispatcher::handleEvent()', 'FrameDisplayEventReceiver.onVsync()  // 异步 Message，Runnable = this', 'FrameDisplayEventReceiver.run() → Choreographer.doFrame()', 'doCallbacks(INPUT → ANIMATION → INSETS_ANIMATION)'],
      packet: 'FrameData / FrameTimeline：vsyncId、frameTimeNanos、deadline',
      desc: 'onVsync() 投递一条异步消息（同步屏障让它越过普通消息），run() 里执行 doFrame()。若 UI 线程忙到错过一个周期以上，doFrame 会按 jitter 修正帧时间并打印 Skipped frames。',
      hint: 'UI 线程忙时，doFrame 只能晚于 VSYNC-app 开始，预算被直接吃掉。' },
    { id: 'trav', lane: 'UI', where: 'App 进程 · UI 主线程', stage: 4, short: 'measure/layout', from: ['doframe'], title: 'TRAVERSAL：relayout / measure / layout',
      chain: ['TraversalCallback.onVsync() → doTraversal()', 'ViewRootImpl.performTraversals()', 'relayoutWindow()  // 需要时，IWindowSession', 'performMeasure() → performLayout()'],
      packet: 'View 树的 MeasureSpec / frame；窗口变化时经 relayout 拿回 SurfaceControl',
      desc: '先移除同步屏障再进入 performTraversals()。窗口参数变化时 relayoutWindow()：满足 canRelayoutAsync 走 oneway relayoutAsync2，位置与尺寸同时变化才走同步 relayout2。',
      hint: '层级深、约束反复变化是 measure/layout 超时的常见原因。' },
    { id: 'draw', lane: 'UI', where: 'App 进程 · UI 主线程', stage: 5, short: 'draw 录制', from: ['trav'], title: 'draw：录制 DisplayList（不触碰 GPU）',
      chain: ['ViewRootImpl.performDraw()', 'ThreadedRenderer.draw()', 'updateRootDisplayList() → View.updateDisplayListIfDirty()', 'RecordingCanvas → RenderNode staging DisplayList'],
      packet: '每个 RenderNode 的 staging DisplayList + 属性变更',
      desc: '硬件加速下 draw 只把绘制指令录进 RenderNode 的 staging 字段，GPU 还没有参与。',
      hint: '录制慢通常是 onDraw 过重或 RenderNode 树过大。' },
    { id: 'sync', lane: 'UI', where: 'App 进程 · UI 主线程 ⇄ RenderThread', stage: 5, short: 'sync', from: ['draw'], title: 'syncAndDrawFrame：UI 阻塞，RenderThread 同步帧状态',
      chain: ['HardwareRenderer.syncAndDrawFrame()', 'DrawFrameTask::drawFrame() → postAndWait()  // mSignal.wait(mLock)', 'DrawFrameTask::run() → syncFrameState() → prepareTree()', 'unblockUiThread()'],
      packet: 'FrameInfo + staging → render 属性；UI 阻塞在 mSignal 上',
      desc: 'UI 线程把任务投给 RenderThread 后阻塞在条件变量上。RenderThread 必须先做完上一帧，才能开始本帧 syncFrameState；同步完成即 unblockUiThread()（canUnblockUiThread 为 false 时要等 draw 完）。',
      hint: 'RenderThread 还在画上一帧时，这一步会拖住 UI 线程。' },
    { id: 'commit', lane: 'UI', where: 'App 进程 · UI 主线程', stage: 3, short: 'COMMIT', from: ['sync'], title: 'CALLBACK_COMMIT：同一次 doFrame 的收尾',
      chain: ['Choreographer.doCallbacks(CALLBACK_COMMIT)'],
      packet: '本帧 doFrame 结束，UI 线程可以处理下一条消息',
      desc: 'COMMIT 在同一次 doFrame 里、TRAVERSAL 返回后立即执行，用于帧后处理（如 trim memory、修正动画起始时间）。它不等上屏。',
      hint: '到这里 UI 线程本帧的工作结束，可以处理下一条消息。' },
    { id: 'rtdraw', lane: 'RT', where: 'App 进程 · RenderThread', stage: 6, short: 'RT 绘制', from: ['sync'], title: 'RenderThread：dequeueBuffer → Skia 回放 → 提交 GPU 命令',
      chain: ['CanvasContext::draw()', 'GL：eglBeginFrame → dequeueBuffer / Vulkan：VulkanSurface::dequeueNativeBuffer', 'SkiaOpenGLPipeline::draw() / SkiaVulkanPipeline::draw()', 'skgpu::ganesh::FlushAndSubmit()'],
      packet: 'GPU 命令流；dequeue 得到一个 FREE slot',
      desc: 'dequeueBuffer 需要一个 FREE slot；3 个 slot 都被占用时会阻塞，直到上一帧 buffer 被 release。CPU 侧提交完命令就继续，不等 GPU 执行完。',
      hint: 'RT 上的“等 buffer release”是三缓冲被填满的直接信号。' },
    { id: 'queue', lane: 'RT', where: 'App 进程 · RenderThread → binder（oneway）', stage: 6, short: 'queue+BLAST', from: ['rtdraw'], ipc: true, title: 'queueBuffer + BLAST Transaction（oneway）',
      chain: ['EglManager::swapBuffers() / VulkanSurface 队列', 'BufferQueueProducer::queueBuffer()', 'BLASTBufferQueue::onFrameAvailable() → acquireNextBufferLocked()', 'Transaction::setBuffer().apply(false, true)  // oneway'],
      packet: 'GraphicBuffer + acquire fence + frameNumber + vsyncId（Transaction）',
      desc: 'queueBuffer 时 GPU 可能还没画完，buffer 带着 acquire fence 交出。BBQ 立刻 acquire 并用带 apply token 的 oneway Transaction 交给 SurfaceFlinger。',
      hint: 'queueBuffer 完成不等于画完：SF 还要等 acquire fence。' },
    { id: 'gpu', lane: 'GPU', where: 'GPU（驱动异步执行）', stage: 6, short: 'GPU', from: ['rtdraw'], title: 'GPU 执行命令，完成时 signal acquire fence',
      chain: ['GPU 驱动执行命令流', 'acquire fence signal'],
      packet: 'acquire fence：这个 buffer 的内容已经写完',
      desc: 'GPU 与 CPU 异步执行。fence 未 signal 时，SF 的 TransactionHandler 会把这条事务判为未就绪（latch-unsignaled 未开启时），留到下一次唤醒。',
      hint: 'GPU 超时的现象：queueBuffer 很早，但 SF 迟迟不 latch。' },
    { id: 'txn', lane: 'SF', where: 'SurfaceFlinger 进程 · binder 线程', stage: 7, short: '收到事务', from: ['queue'], ipc: true, title: 'SurfaceFlinger 收到事务',
      chain: ['SurfaceFlinger::setTransactionState()  // binder 线程', 'TransactionHandler::queueTransaction()', 'setTransactionFlags(eTransactionFlushNeeded)', 'scheduleCommit()'],
      packet: 'TransactionState 进入无锁队列',
      desc: '事务在 binder 线程入队，主线程不在这里处理它；真正的应用发生在下一次 SF 唤醒的 commit 里。',
      hint: '入队早于 SF 唤醒才有机会赶上本帧。' },
    { id: 'vsf', lane: 'SCHED', where: 'SurfaceFlinger 进程 · Scheduler 定时器 → 主线程', stage: 7, short: 'VSYNC-sf', from: ['txn'], vsync: 'VSYNC-sf', title: 'VSYNC-sf 唤醒 SurfaceFlinger 主线程',
      chain: ['VSyncDispatchTimerQueue::timerCallback()', 'Scheduler::onFrameSignal()', 'SurfaceFlinger::commit()'],
      packet: 'FrameTarget：vsyncId + expectedPresentTime',
      desc: 'SF 唤醒点 = 预期上屏时刻 − sfWorkDuration。frameIsEarly() 会拒绝“预测上屏时刻还没到”的事务，所以 buffer 来得再早也不会被提前一拍消费。',
      hint: '本帧 app 的截止线就是这个唤醒点。' },
    { id: 'latch', lane: 'SF', where: 'SurfaceFlinger 进程 · 主线程', stage: 7, short: 'commit/latch', from: ['vsf', 'gpu'], title: 'commit：过滤就绪事务，latch buffer',
      chain: ['SurfaceFlinger::commit()', 'updateLayerSnapshots() → TransactionHandler::flushTransactions()', 'transactionReadyTimelineCheck / transactionReadyBufferCheck', 'Layer::latchBufferImpl()'],
      packet: 'LayerSnapshot：新 buffer + acquire fence',
      desc: '同一 layer 有多个就绪 buffer 时只保留最新的，旧的直接 release 并记为 dropped（releasePreviousBuffer）。',
      hint: '迟到的 buffer 和下一帧同时就绪时会被跳过，这也是一种掉帧。' },
    { id: 'comp', lane: 'SF', where: 'SurfaceFlinger 进程 · 主线程', stage: 7, short: 'composite', from: ['latch'], title: 'composite：CompositionEngine 选择策略并合成',
      chain: ['SurfaceFlinger::composite()', 'CompositionEngine::present()', 'Output::prepare()  // rebuildLayerStacks', 'Output::present() → prepareFrame() → finishFrame()'],
      packet: 'HWC layer 列表 / client target（GPU 合成时）',
      desc: 'prepare 只重建 layer stack；合成策略（device / client / mixed）在 present → prepareFrame 中决定，可能是预测式异步。client composition 由 RenderEngine 在 finishFrame 里完成。',
      hint: 'SF 超时会让本帧错过预期的 HW VSYNC。' },
    { id: 'hwc', lane: 'SF', where: 'SF 主线程 → HWC HAL（composer 服务）', stage: 8, short: 'HWC present', from: ['comp'], ipc: true, title: 'HWC present：提交显示控制器',
      chain: ['Output::presentFrameAndReleaseLayers()', 'HWComposer::presentAndGetReleaseFences()', 'presentOrValidate / validate + present'],
      packet: 'present fence + 每个 layer 的 release fence',
      desc: 'present() 只是把这一帧交给显示控制器；真正开始扫描在下一个 HW VSYNC。',
      hint: '“present 调用完成”和“屏幕开始显示”是两件事。' },
    { id: 'present', lane: 'DISPLAY', where: '显示控制器 → present fence；SF 主线程记录', stage: 8, short: '上屏', from: ['hwc'], vsync: 'HW VSYNC', title: 'HW VSYNC：开始扫描本帧，present fence signal',
      chain: ['present fence signal', 'SurfaceFlinger::onCompositionPresented()', 'FrameTimeline::setSfPresent()  // jank 分类'],
      packet: 'actual present time → FrameTimeline / JankData',
      desc: 'FrameTimeline 用实际上屏时刻对比 vsyncId 预测的上屏时刻做 jank 分类。',
      hint: '“掉帧”的定义是实际上屏晚于预测，不是“画得慢”。' },
    { id: 'release', lane: 'BQ', where: 'SF → App 进程 binder 线程（BLASTBufferQueue 回调）', stage: 8, short: '释放buffer', from: ['present'], ipc: true, title: '本帧 buffer 被下一帧替换后才释放',
      chain: ['下一帧上屏 → LayerFE::setReleaseFence()', 'TransactionCallbackInvoker::sendCallbacks()  // previousReleaseFence', 'BLASTBufferQueue::releaseBufferCallback()', 'BufferQueueConsumer::releaseBuffer() → slot FREE'],
      packet: 'release fence 回到 app，slot 变回 FREE',
      desc: 'buffer N 在 N+1 替换它的那一帧上屏后才拿到 release fence；被跳过（dropped）的 buffer 在 latch 时立即释放。没有下一帧时，它会一直留在屏幕上。',
      hint: '释放时机取决于下一帧，所以连续掉帧会让 dequeue 阻塞。' }
  ];
  FP.STEP = {};
  FP.STEPS.forEach(function (s, i) { s.index = i; FP.STEP[s.id] = s; });
})(window.FP);
