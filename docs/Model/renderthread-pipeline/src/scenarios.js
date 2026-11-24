/* RenderThread one-frame scenarios (APP budget = 8.3ms). Times illustrative. */
window.__RT_SCENARIOS__ = (function () {
  'use strict';
  var APP = 0;       // APP VSYNC
  var D = 8.333;     // SF VSYNC / app deadline
  var END = 10.0;

  var NORMAL = {
    key: 'normal', label: '正常帧', total: END,
    vsyncs: [APP, D],
    vsyncLabels: ['APP VSYNC · UI 开始', 'SF VSYNC · APP DEADLINE'],
    appVsync: APP, deadline: D, queueDone: 8.0, loopAt: END,
    present: 'queueBuffer · 8.0 ms',
    lanes: {
      UI: [
        { t0: 0, t1: 2.6, label: 'traversal / measure / layout / draw 录制', callout: 'APP VSYNC → UI 交棒：2.6ms 应用绘制', hideLabel: true, stage: 2 },
        { t0: 2.6, t1: 3.0, label: 'postAndWait', callout: '① UI 交棒（阻塞）', hideLabel: true, stage: 2 },
        { t0: 3.0, t1: 4.2, label: 'UI 等待 sync', title: 'UI 阻塞等待 syncFrameState', dashed: true, stage: 3 },
        { t0: 4.2, t1: D, label: 'UI 已解阻 · 可继续下一帧', dashed: true, stage: 3 },
        { t0: D, t1: END, label: '下一 APP VSYNC', dashed: true, stage: 9 }
      ],
      RT: [
        { t0: 2.9, t1: 3.0, label: 'threadLoop', title: 'threadLoop → DrawFrameTask::run', hideLabel: true, stage: 1 },
        { t0: 3.0, t1: 4.2, label: 'syncFrameState', callout: 'syncFrameState：树 / 纹理同步', hideLabel: true, stage: 3 },
        { t0: 4.2, t1: 5.6, label: 'prepareTree', stage: 4 },
        { t0: 5.6, t1: 6.4, label: 'draw', callout: 'CanvasContext::draw', hideLabel: true, stage: 5 },
        { t0: 6.4, t1: 7.2, label: 'Skia / flush', stage: 6 },
        { t0: 7.2, t1: 7.6, label: 'eglSwap', callout: 'eglSwapBuffers + BLAST', hideLabel: true, stage: 7 },
        { t0: 7.6, t1: 8.0, label: 'BLAST txn', title: 'BLAST Transaction', hideLabel: true, stage: 8 },
        { t0: 8.0, t1: D, label: 'callbacks', callout: 'commit / timeline callback', hideLabel: true, stage: 9 },
        { t0: D, t1: END, label: 'RT 空闲 · 等下一 APP VSYNC', dashed: true, stage: 9 }
      ],
      GPU: [
        { t0: 6.4, t1: 7.2, label: 'GPU 执行命令', stage: 6 },
        { t0: 7.2, t1: 7.6, label: 'fence', stage: 7 }
      ],
      BQ: [
        { t: 5.6, label: 'dequeue', pos: 'above', stage: 5 },
        { t: 8.0, label: 'queueBuffer', pos: 'above', stage: 7 },
        { t: 8.1, label: 'txn apply', pos: 'below', stage: 8 },
        { t: 9.2, label: 'release', pos: 'above', stage: 9 }
      ],
      FENCE: [
        { t: 8.0, label: 'acquire fence', pos: 'above', stage: 7 },
        { t: 8.1, label: 'txn applied', pos: 'below', stage: 8 },
        { t: 8.2, label: 'commit cb', pos: 'above', stage: 9 },
        { t: 9.2, label: 'release → FREE', pos: 'below', stage: 9 }
      ]
    },
    pins: [
      { stage: 1, t: 0.0 }, { stage: 2, t: 2.6 }, { stage: 3, t: 3.0 },
      { stage: 4, t: 4.2 }, { stage: 5, t: 5.6 }, { stage: 6, t: 6.4 },
      { stage: 7, t: 7.2 }, { stage: 8, t: 7.6 }, { stage: 9, t: 8.0 }
    ],
    phases: [
      { t0: 0, t1: 2.6, stage: 2, title: 'APP VSYNC → UI 交棒：2.6ms 应用绘制',
        desc: 'APP 在 0.0ms 醒来，UI 线程先完成 traversal、measure/layout 与 DisplayList 录制，约 2.6ms 后进入 syncAndDrawFrame。加上约 0.4ms 的交棒，8.3ms 的 APP 预算已用掉约 3.0ms，RenderThread 只剩约 5.3ms。',
        refs: ['Choreographer APP VSYNC', 'ViewRootImpl.performTraversals()', 'ThreadedRenderer.syncAndDrawFrame()'], lane: 'UI', hint: 'RT 不是从 0 开始；它必须在 UI 交棒后、SF VSYNC 前完成。' },
      { t0: 2.6, t1: 3.0, stage: 2, title: 'UI 交棒：syncAndDrawFrame → DrawFrameTask',
        desc: 'UI 线程在 syncAndDrawFrame() 中准备 FrameInfo，把 DrawFrameTask 投进 RenderThread 的 WorkQueue，然后阻塞等待。RT 被唤醒后执行 DrawFrameTask::run()，从这里开始真正接管这一帧。',
        refs: ['HardwareRenderer.syncAndDrawFrame()', 'DrawFrameTask::postAndWait()', 'DrawFrameTask::run()'], lane: 'UI', hint: 'UI 只在 postAndWait 期间阻塞；它不是自己执行 GPU 绘制。' },
      { t0: 3.0, t1: 4.2, stage: 3, title: 'syncFrameState：同步 RenderNode 树、动画与纹理',
        desc: 'syncFrameState() 把 FrameInfo 的 vsync 时间灌给 TimeLord，绑定 EGL context、apply DeferredLayerUpdater，再调用 CanvasContext::prepareTree 同步树；纹理上传完成后即可 unblockUiThread()。',
        refs: ['DrawFrameTask::syncFrameState()', 'TimeLord::vsyncReceived()', 'DrawFrameTask::unblockUiThread()'], lane: 'RT', hint: 'UI 解阻发生在 prepareTree 之后、GPU 回放之前。' },
      { t0: 4.2, t1: 5.6, stage: 4, title: 'prepareTree：属性、动画、damage 与 layer 预取',
        desc: 'CanvasContext::prepareTree 遍历 mRenderNodes，对每个 RenderNode 调 prepareTree：更新 RenderProperties、运行动画、累积 damage、预取 layer；最后 mNativeSurface->reserveNext() 预占下一个 buffer。',
        refs: ['CanvasContext::prepareTree()', 'RenderNode::prepareTree()', 'TreeInfo'], lane: 'RT', hint: 'prepareTree 是 UI 阻塞窗口的最后一段；damage 也在这里产生。' },
      { t0: 5.6, t1: 6.4, stage: 5, title: 'CanvasContext::draw：取 Frame、算脏区、进入绘制',
        desc: 'draw() 先 finish 合并 damage，判断空帧/绘制关闭等跳过条件；getFrame() 通过 EglManager::beginFrame 绑定 EGL surface；computeDirtyRect 处理 buffer age 与窗口脏区，然后调用 RenderPipeline::draw。',
        refs: ['CanvasContext::draw()', 'CanvasContext::getFrame()', 'EglManager::beginFrame()'], lane: 'RT', hint: '空帧跳过也会回调 frame commit listener，避免 callbacks 永远挂着。' },
      { t0: 6.4, t1: 7.2, stage: 6, title: 'Skia 回放 DisplayList，提交 GPU 命令',
        desc: 'SkiaOpenGLPipeline::draw 在 FBO 0 / EGL surface 上回放 RenderNode 树：SkiaRecordingCanvas 录制的 DisplayList 被转换成 GPU 命令，SkiaPipeline::renderFrameImpl 处理 layer、clip 与 damage，flushAndSubmit 把命令提交给 GPU。',
        refs: ['SkiaOpenGLPipeline::draw()', 'SkiaPipeline::renderFrameImpl()', 'GrDirectContext::flushAndSubmit()'], lane: 'RT', hint: 'flushAndSubmit 返回只代表命令提交，不代表 GPU 执行完成。' },
      { t0: 7.2, t1: 7.6, stage: 7, title: 'eglSwapBuffers：触发 dequeue、queueBuffer 与 fences',
        desc: 'SkiaOpenGLPipeline::swapBuffers 调 EglManager::swapBuffers；eglSwapBuffersWithDamageKHR 驱动 EGL 完成 dequeueBuffer 与 queueBuffer，Surface::hook_queueBuffer 进入 BufferQueueProducer::queueBuffer，slot 从 DEQUEUED 变成 QUEUED。',
        refs: ['SkiaOpenGLPipeline::swapBuffers()', 'EglManager::swapBuffers()', 'BufferQueueProducer::queueBuffer()'], lane: 'RT', hint: 'GL 路径没有显式 dequeue/queue 调用点；由 EGL 驱动完成。' },
      { t0: 7.6, t1: 8.0, stage: 8, title: 'BLAST：buffer 随 Transaction 直接提交给 SF',
        desc: 'BufferQueueProducer 的 onFrameAvailable 回调到 BLASTBufferQueue::acquireNextBufferLocked：立刻 acquire buffer、setBuffer 到 Transaction、setApplyToken 并 apply 给 SurfaceFlinger；buffer 不再留在 BufferQueue 等 SF latch。',
        refs: ['BLASTBufferQueue::onFrameAvailable()', 'BLASTBufferQueue::acquireNextBufferLocked()', 'Transaction::apply()'], lane: 'BQ', hint: 'BLAST 把 buffer 的交接从“队列等待”变成“Transaction 提交”。' },
      { t0: 8.0, t1: END, stage: 9, title: '异步回调、fence 与 buffer 回收',
        desc: 'RT 不等待上屏：它登记 frame commit / transaction completed 回调，把 acquire/present/release fence 交给系统。Transaction applied、GPU completion、present 与 buffer release 在后续时刻异步回传；RT 可以继续下一帧。',
        refs: ['CanvasContext::addFrameCommitListener()', 'TransactionCompletedListener', 'SurfaceFrame / FrameTimeline'], lane: 'FENCE', hint: '看到 queueBuffer 只说明 buffer 交出去了，不代表已上屏。' }
    ],
    slots: [
      { name: 'SLOT 0 · 上一帧', ranges: [{ from: 0, to: D, state: 'DISPLAY', owner: 'Display' }, { from: D, to: END, state: 'FREE', owner: '—' }] },
      { name: 'SLOT 1 · 本帧 F', ranges: [
        { from: 0, to: 3.0, state: 'FREE', owner: '—' },
        { from: 3.0, to: 8.0, state: 'DEQUEUED', owner: 'RenderThread' },
        { from: 8.0, to: D, state: 'QUEUED', owner: 'BufferQueue' },
        { from: D, to: END, state: 'LATCHED', owner: 'SurfaceFlinger' }
      ] },
      { name: 'SLOT 2 · 空闲', ranges: [{ from: 0, to: END, state: 'FREE', owner: '—' }] }
    ],
    slotNote: 'APP 预算 8.3ms：UI 用约 3.0ms，RT 只剩约 5.3ms。RT 在 draw 前 reserveNext 预占 buffer，eglSwapBuffers 后 slot 进入 QUEUED；BLAST 交给 SF，SF VSYNC 后 latch。',
    misses: [],
    budgetBase: D
  };

  var JANK = {
    key: 'jank', label: '慢帧', total: 15.0,
    vsyncs: [APP, D],
    vsyncLabels: ['APP VSYNC · UI 开始', 'SF VSYNC · APP DEADLINE MISS'],
    appVsync: APP, deadline: D, queueDone: 13.2, loopAt: 15,
    present: 'queueBuffer · 13.2 ms（越过 SF VSYNC）',
    lanes: {
      UI: [
        { t0: 0, t1: 2.6, label: 'traversal / draw 录制', callout: 'APP VSYNC → UI 交棒：2.6ms', hideLabel: true, stage: 2 },
        { t0: 2.6, t1: 3.0, label: 'postAndWait', callout: '① UI 交棒', hideLabel: true, stage: 2 },
        { t0: 3.0, t1: 5.8, label: 'UI 等待 sync', title: 'UI 阻塞等待 syncFrameState', dashed: true, stage: 3, hot: true },
        { t0: 5.8, t1: 15, label: 'UI 解阻 · 下一帧已被推迟', dashed: true, stage: 3 }
      ],
      RT: [
        { t0: 2.9, t1: 3.0, label: 'threadLoop', callout: 'threadLoop → run', hideLabel: true, stage: 1 },
        { t0: 3.0, t1: 5.8, label: 'syncFrameState（慢）', callout: 'syncFrameState 超预算', hideLabel: true, stage: 3, hot: true },
        { t0: 5.8, t1: 8.0, label: 'prepareTree（慢）', stage: 4, hot: true },
        { t0: 8.0, t1: 8.8, label: 'draw', callout: 'CanvasContext::draw', hideLabel: true, stage: 5 },
        { t0: 8.8, t1: 12.2, label: 'Skia 回放 + GPU 慢', stage: 6, hot: true },
        { t0: 12.2, t1: 13.2, label: 'eglSwap', title: 'eglSwapBuffers', hideLabel: true, stage: 7, hot: true },
        { t0: 13.2, t1: 13.4, label: 'queueBuffer', title: 'queueBuffer 迟到', hideLabel: true, stage: 7, hot: true },
        { t0: 13.4, t1: 14.2, label: 'BLAST txn', title: 'BLAST Transaction', hideLabel: true, stage: 8, hot: true },
        { t0: 14.2, t1: 15, label: 'callbacks', callout: '迟到的 callback', hideLabel: true, stage: 9 }
      ],
      GPU: [
        { t0: 8.8, t1: 12.2, label: 'GPU 执行命令（慢）', stage: 6, hot: true },
        { t0: 12.2, t1: 13.2, label: 'GPU 完成 / fence', stage: 7 }
      ],
      BQ: [
        { t: 8.0, label: 'dequeue', pos: 'above', stage: 5 },
        { t: 13.2, label: 'queueBuffer', pos: 'above', stage: 7, hot: true },
        { t: 13.4, label: 'txn apply', pos: 'below', stage: 8 },
        { t: 14.4, label: 'release', pos: 'above', stage: 9 }
      ],
      FENCE: [
        { t: 13.2, label: 'acquire fence', pos: 'above', stage: 7 },
        { t: 13.4, label: 'txn applied', pos: 'below', stage: 8 },
        { t: 13.8, label: 'commit cb', pos: 'above', stage: 9 },
        { t: 14.4, label: 'release → FREE', pos: 'below', stage: 9 }
      ]
    },
    pins: [
      { stage: 1, t: 0.0 }, { stage: 2, t: 2.6 }, { stage: 3, t: 3.0 },
      { stage: 4, t: 5.8 }, { stage: 5, t: 8.0 }, { stage: 6, t: 8.8 },
      { stage: 7, t: 12.2 }, { stage: 8, t: 13.2 }, { stage: 9, t: 13.4 }
    ],
    phases: [
      { t0: 0, t1: 2.6, stage: 2, title: 'APP VSYNC → UI 交棒：2.6ms 已花掉',
        desc: 'UI 仍然先用约 2.6ms 做 traversal 与 DisplayList 录制，加上约 0.4ms 交棒；8.3ms 的 APP 预算只剩约 5.3ms 给 RenderThread。',
        refs: ['Choreographer APP VSYNC', 'ViewRootImpl.performTraversals()'], lane: 'UI', hint: '慢帧不是 RT 一个人造成的；先看 UI 交棒时间。' },
      { t0: 2.6, t1: 3.0, stage: 2, title: 'UI 交棒：同一入口，更重的负载',
        desc: '入口不变：UI 线程仍然通过 syncAndDrawFrame 把 DrawFrameTask 投进 RT 队列并阻塞。区别是这一帧的树同步、资源准备或 GPU 工作更重。',
        refs: ['DrawFrameTask::postAndWait()'], lane: 'UI', hint: 'postAndWait 本身很便宜，成本在 syncFrameState 与后续阶段。' },
      { t0: 3.0, t1: 5.8, stage: 3, title: 'syncFrameState 变慢：UI 阻塞窗口被拉长',
        desc: 'syncFrameState 用了约 2.4ms，UI 一直阻塞到 5.8ms；此时距 8.3ms 的 SF VSYNC 只剩 2.5ms。',
        refs: ['DrawFrameTask::syncFrameState()', 'TimeLord::vsyncReceived()'], lane: 'RT', hot: true, hint: 'sync 慢会同时推迟 UI 解阻与后续 draw。' },
      { t0: 5.8, t1: 8.0, stage: 4, title: 'prepareTree 变慢：属性/动画/damage 成本',
        desc: 'prepareTree 用了约 2.2ms；等它结束时已经是 8.0ms，APP 预算只剩 0.3ms，却还要做 draw、GPU 提交与 swap。',
        refs: ['CanvasContext::prepareTree()', 'RenderNode::prepareTree()'], lane: 'RT', hot: true, hint: 'prepareTree 是 UI 阻塞窗口的最后一段，也是 RT 预算的大头。' },
      { t0: 8.0, t1: 8.8, stage: 5, title: 'CanvasContext::draw：已经越过 SF VSYNC',
        desc: 'draw 在 8.0ms 才开始，8.3ms 的 SF VSYNC 已经到达；这一帧的 queueBuffer 注定迟到。',
        refs: ['CanvasContext::draw()', 'CanvasContext::getFrame()'], lane: 'RT', hot: true, hint: 'draw 开始时间已经晚于 deadline，后面只能尽量缩短迟到。' },
      { t0: 8.8, t1: 12.2, stage: 6, title: 'Skia 回放 + GPU 提交慢：RT 只剩的预算被吃光',
        desc: 'DisplayList 回放与 flush/submit 用了约 3.4ms；这段只是提交命令，GPU 执行还要更久。',
        refs: ['SkiaOpenGLPipeline::draw()', 'GrDirectContext::flushAndSubmit()'], lane: 'RT', hot: true, hint: 'GPU 命令提交慢会直接推迟 eglSwapBuffers 与 queueBuffer。' },
      { t0: 12.2, t1: 13.2, stage: 7, title: 'eglSwapBuffers / queueBuffer 迟到',
        desc: 'eglSwapBuffers 到 12.2ms 才开始，queueBuffer 到 13.2ms 才完成；已经比 SF VSYNC 晚约 4.9ms，SF 只能等下一拍。',
        refs: ['EglManager::swapBuffers()', 'BufferQueueProducer::queueBuffer()'], lane: 'RT', hot: true, hint: 'queueBuffer 晚于 SF VSYNC，等于这一帧错过本拍合成。' },
      { t0: 13.2, t1: 13.4, stage: 8, title: 'BLAST 提交：事务在 queueBuffer 之后',
        desc: 'BLAST 在 queueBuffer 后把 buffer 放进 Transaction 提交给 SF；SF 已经在 8.3ms 的相位检查过这一帧，只能等下一拍。',
        refs: ['BLASTBufferQueue::onFrameAvailable()', 'BLASTBufferQueue::acquireNextBufferLocked()'], lane: 'BQ', hot: true, hint: 'BLAST 很快，但前提是 buffer 已经 queue 出来。' },
      { t0: 13.4, t1: 15, stage: 9, title: '回调、fence 与 buffer 回收',
        desc: 'Transaction applied、GPU completion、present 与 release 在后续异步回传；RT 不等待上屏，可以开始下一帧。',
        refs: ['CanvasContext::addFrameCommitListener()', 'TransactionCompletedListener'], lane: 'FENCE', hot: true, hint: '迟到不代表丢弃；它只是错过了本拍 SF 合成。' }
    ],
    slots: [
      { name: 'SLOT 0 · 上一帧', ranges: [{ from: 0, to: D, state: 'DISPLAY', owner: 'Display（重复）' }, { from: D, to: 15, state: 'FREE', owner: '—' }] },
      { name: 'SLOT 1 · 本帧 F', ranges: [
        { from: 0, to: 3.0, state: 'FREE', owner: '—' },
        { from: 3.0, to: 13.2, state: 'DEQUEUED', owner: 'RenderThread' },
        { from: 13.2, to: 15, state: 'QUEUED', owner: 'BufferQueue' }
      ] },
      { name: 'SLOT 2 · 空闲', ranges: [{ from: 0, to: 15, state: 'FREE', owner: '—' }] }
    ],
    slotNote: 'APP 预算只有 8.3ms；UI 先用 3.0ms，RT 的 sync/prepare/GPU 变慢后 queueBuffer 到 13.2ms，错过 SF VSYNC。',
    misses: [
      { t: D, lane: 'RT', label: '', title: 'SF VSYNC 8.3ms：queueBuffer 未完成' }
    ],
    budgetBase: D
  };

  return { normal: NORMAL, jank: JANK };
})();