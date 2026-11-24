/* UI refresh chain scenarios. Times are illustrative. */
window.__RF_SCENARIOS__ = (function () {
  'use strict';
  var V = 16.667;      // APP VSYNC
  var D = 33.333;      // next VSYNC / UI deadline

  var NORMAL = {
    key: 'normal', label: '正常帧', total: D,
    vsyncs: [0, V, D],
    vsyncLabels: ['T0 · 请求刷新', 'APP VSYNC · doFrame', 'NEXT VSYNC · 预算截止'],
    appVsync: V, deadline: D, queueDone: 24.8, loopAt: D,
    present: 'syncAndDrawFrame · 24.8 ms',
    lanes: {
      UI: [
        { t0: 0, t1: 1.2, label: 'requestLayout / invalidate', callout: '① requestLayout / invalidate', hideLabel: true, stage: 1 },
        { t0: 1.2, t1: 1.6, label: 'scheduleTraversals', callout: '② scheduleTraversals + barrier', hideLabel: true, stage: 2 },
        { t0: 1.6, t1: V, label: '等待 APP VSYNC', dashed: true, stage: 3 },
        { t0: V, t1: 17.6, label: 'doFrame', callout: '④ doFrame 回调顺序', hideLabel: true, stage: 4 },
        { t0: 17.6, t1: 18.4, label: 'measureHierarchy', callout: '⑥ measureHierarchy（预测量）', hideLabel: true, stage: 6 },
        { t0: 18.4, t1: 19.4, label: 'relayout', title: 'relayoutWindow / WMS', hideLabel: true, stage: 5 },
        { t0: 19.4, t1: 20.8, label: 'measure', callout: '⑥ performMeasure', hideLabel: true, stage: 6 },
        { t0: 20.8, t1: 22.0, label: 'layout', callout: '⑥ performLayout（两趟上限）', hideLabel: true, stage: 6 },
        { t0: 22.0, t1: 24.0, label: 'draw / record', callout: '⑦ draw → DisplayList', hideLabel: true, stage: 7 },
        { t0: 24.0, t1: 24.8, label: 'syncAndDrawFrame', callout: '⑧ 交棒 RenderThread', hideLabel: true, stage: 8 },
        { t0: 24.8, t1: D, label: 'UI 阻塞：RT syncFrameState', dashed: true, stage: 8 }
      ],
      CHOREO: [
        { t0: 1.2, t1: 1.6, label: 'postVsyncCallback', callout: 'Choreographer.postVsyncCallback', hideLabel: true, stage: 2 },
        { t0: 1.6, t1: V, label: 'VSYNC 已申请 · 等事件', dashed: true, stage: 3 },
        { t0: V, t1: 17.6, label: 'doFrame callbacks', hideLabel: true, stage: 4 }
      ],
      WMS: [
        { t0: 18.4, t1: 19.4, label: 'relayoutWindow / WMS', callout: '⑤ relayoutWindow / WMS', hideLabel: true, stage: 5 }
      ],
      DL: [
        { t0: 22.0, t1: 24.0, label: 'record DisplayList / RenderNode', callout: 'record DisplayList / RenderNode', hideLabel: true, stage: 7 }
      ],
      RT: [
        { t0: 24.0, t1: 24.8, label: 'DrawFrameTask queued', callout: 'postAndWait：UI 开始阻塞', hideLabel: true, stage: 8 },
        { t0: 24.8, t1: D, label: 'RT 接管 · syncFrameState', dashed: true, stage: 8 }
      ]
    },
    pins: [
      { stage: 1, t: 0.0 }, { stage: 2, t: 1.2 }, { stage: 3, t: 1.25 },
      { stage: 4, t: V }, { stage: 5, t: 18.4 }, { stage: 6, t: 19.4 },
      { stage: 7, t: 22.0 }, { stage: 8, t: 24.0 }
    ],
    phases: [
      { t0: 0, t1: 1.2, stage: 1, title: '请求刷新：requestLayout / invalidate 写下脏标记',
        desc: 'View.requestLayout() 置 PFLAG_FORCE_LAYOUT|PFLAG_INVALIDATED，invalidate() 置 PFLAG_INVALIDATED；脏标记沿父链冒泡到 ViewRootImpl，被 mTraversalScheduled 合并成一次 traversal 预约。',
        refs: ['View.requestLayout()', 'View.invalidateInternal()', 'ViewRootImpl.requestLayout()'], lane: 'UI', hint: '触发只登记，不会立刻 measure/layout。' },
      { t0: 1.2, t1: 1.6, stage: 2, title: 'scheduleTraversals：同步屏障 + 预约 traversal',
        desc: 'ViewRootImpl.scheduleTraversals() 用 mTraversalScheduled 合并请求，postTraversalBarrier() 插入 MessageQueue 同步屏障，然后 mChoreographer.postVsyncCallback(CALLBACK_TRAVERSAL, mTraversalCallback) 预约 APP VSYNC。',
        refs: ['ViewRootImpl.scheduleTraversals()', 'ViewRootImpl.postTraversalBarrier()', 'Choreographer.postVsyncCallback()'], lane: 'CHOREO', hint: '同步屏障保证 traversal 优先于普通同步消息。' },
      { t0: 1.6, t1: V, stage: 3, title: '申请 VSYNC：Choreographer → DisplayEventReceiver',
        desc: 'Choreographer.scheduleFrameLocked/scheduleVsyncLocked 通过 DisplayEventReceiver.scheduleVsync → JNI → BitTube 向 SurfaceFlinger 的 EventThread 请求一次 APP VSYNC；VSyncDispatch 按 appOffset/workDuration 在相位点唤醒。',
        refs: ['Choreographer.scheduleVsyncLocked()', 'DisplayEventReceiver.scheduleVsync()', 'EventThread::requestNextVsync()'], lane: 'CHOREO', hint: '一次请求只发一次事件；错过就要等下一拍。' },
      { t0: V, t1: 17.6, stage: 4, title: 'APP VSYNC：doFrame 回调序列',
        desc: 'BitTube 唤醒 Looper，FrameDisplayEventReceiver.onVsync() 投递异步消息；Choreographer.doFrame() 按 INPUT → ANIMATION → INSETS_ANIMATION → TRAVERSAL → COMMIT 派发。traversal 回调最终进入 performTraversals()。',
        refs: ['Choreographer.doFrame()', 'FrameDisplayEventReceiver.onVsync()', 'TraversalCallback.onVsync()'], lane: 'CHOREO', hint: 'doFrame 在 UI 线程同步执行；回调越多，留给 traversal 的预算越少。' },
      { t0: 17.6, t1: 18.4, stage: 6, title: 'measureHierarchy：relayout 前的预测量',
        desc: 'performTraversals 在 relayoutWindow 之前可能先调用 measureHierarchy 做一次预测量，用 getRootMeasureSpec 把窗口约束翻译成根 MeasureSpec，判断窗口尺寸是否可能变化。',
        refs: ['ViewRootImpl.measureHierarchy()', 'ViewRootImpl.getRootMeasureSpec()'], lane: 'UI', hint: 'measureHierarchy 是可选的预测量，真正的 performMeasure 在 relayout 之后。' },
      { t0: 18.4, t1: 19.4, stage: 5, title: 'relayoutWindow：向 WMS 要窗口 frame / insets',
        desc: 'performTraversals() 调用 relayoutWindow() 通过 IWindowSession 与 system_server 的 WMS 往返，拿回窗口 frame、insets 与 SurfaceControl；尺寸变化时同步 relayout，稳定时可能走异步 relayout。',
        refs: ['ViewRootImpl.performTraversals()', 'ViewRootImpl.relayoutWindow()', 'IWindowSession.relayout()'], lane: 'WMS', hint: '窗口变化是 relayout 与额外 measure/layout 的常见来源。' },
      { t0: 19.4, t1: 20.8, stage: 6, title: 'performMeasure：MeasureSpec 与递归测量',
        desc: 'relayout 之后 performMeasure() 调 mView.measure()，ViewGroup.measureChildren 递归子 View；measure cache 决定是否复用上次结果，约束变化会让缓存失效。',
        refs: ['ViewRootImpl.performMeasure()', 'View.measure()', 'ViewGroup.measureChildren()'], lane: 'UI', hint: 'MeasureSpec 的 EXACTLY/AT_MOST/UNSPECIFIED 由父容器与 LayoutParams 共同决定。' },
      { t0: 20.8, t1: 22.0, stage: 6, title: 'performLayout：setFrame 与两趟布局',
        desc: 'performLayout() 调 mView.layout()，View.layout 通过 setFrame 确定位置并调用 onLayout，ViewGroup 递归 layout 子 View；layout 期间再次 requestLayout 会触发第二趟，超过上限则推迟到下一帧。',
        refs: ['ViewRootImpl.performLayout()', 'View.layout()', 'ViewRootImpl.requestLayoutDuringLayout()'], lane: 'UI', hint: 'layout 中再次 requestLayout 是两趟布局与慢帧的常见根因。' },
      { t0: 22.0, t1: 24.0, stage: 7, title: 'draw / record：把绘制指令录进 DisplayList',
        desc: 'performDraw() 走硬件路径：View.updateDisplayListIfDirty() 在 RecordingCanvas 上录制 DisplayList，RenderNode 树挂到根节点；dirty.setEmpty() 后真正的 damage 由 RT 在 prepareTree 中计算。',
        refs: ['ViewRootImpl.performDraw()', 'View.updateDisplayListIfDirty()', 'ThreadedRenderer.updateRootDisplayList()'], lane: 'DL', hint: 'draw 只是录制；GPU 回放发生在 RenderThread。' },
      { t0: 24.0, t1: D, stage: 8, title: '交棒 RenderThread：syncAndDrawFrame / postAndWait',
        desc: 'ThreadedRenderer.draw() 调 syncAndDrawFrame()，JNI 把 14 项 FrameInfo 拷到 native，RenderProxy 把 DrawFrameTask 投进 RenderThread 的 WorkQueue，UI 线程在 postAndWait 中阻塞，等 RT 的 syncFrameState 完成。页面在这里结束，RT 内部流程见 RenderThread 专题。',
        refs: ['ThreadedRenderer.syncAndDrawFrame()', 'DrawFrameTask::postAndWait()', 'RenderProxy::syncAndDrawFrame()'], lane: 'RT', hint: '交棒完成不等于 GPU 完成；UI 只阻塞到树同步结束。' }
    ],
    slots: [],
    slotNote: 'UI 刷新链不涉及 BufferQueue；交棒后由 RenderThread 接管。',
    misses: [],
    budgetBase: 0
  };

  var JANK = {
    key: 'jank', label: '慢帧', total: 40,
    vsyncs: [0, V, D, 40],
    vsyncLabels: ['T0 · 请求刷新', 'APP VSYNC · doFrame', 'NEXT VSYNC · 预算截止', 'HANDOFF LATE'],
    appVsync: V, deadline: D, queueDone: 35.0, loopAt: 40,
    present: 'syncAndDrawFrame · 35.0 ms（越过截止）',
    lanes: {
      UI: [
        { t0: 0, t1: 1.2, label: 'requestLayout / invalidate', callout: '① requestLayout / invalidate', hideLabel: true, stage: 1 },
        { t0: 1.2, t1: 1.6, label: 'scheduleTraversals', callout: '② scheduleTraversals + barrier', hideLabel: true, stage: 2 },
        { t0: 1.6, t1: V, label: '等待 APP VSYNC', dashed: true, stage: 3 },
        { t0: V, t1: 17.6, label: 'doFrame', callout: '④ doFrame', hideLabel: true, stage: 4 },
        { t0: 17.6, t1: 18.8, label: 'measureHierarchy（慢）', callout: '⑥ measureHierarchy（慢）', hideLabel: true, stage: 6, hot: true },
        { t0: 18.8, t1: 20.2, label: 'relayout（慢）', title: 'relayoutWindow / WMS', hideLabel: true, stage: 5, hot: true },
        { t0: 20.2, t1: 24.5, label: 'measure（慢）', callout: '⑥ performMeasure（慢）', hideLabel: true, stage: 6, hot: true },
        { t0: 24.5, t1: 28.0, label: 'layout（慢）', callout: '⑥ performLayout（慢）', hideLabel: true, stage: 6, hot: true },
        { t0: 28.0, t1: 34.0, label: 'draw / record（慢）', callout: '⑦ draw / record（慢）', hideLabel: true, stage: 7, hot: true },
        { t0: 34.0, t1: 35.0, label: 'syncAndDrawFrame', callout: '⑧ 交棒迟到', hideLabel: true, stage: 8, hot: true },
        { t0: 35.0, t1: 40, label: 'UI 阻塞', dashed: true, stage: 8 }
      ],
      CHOREO: [
        { t0: 1.2, t1: 1.6, label: 'postVsyncCallback', callout: 'Choreographer.postVsyncCallback', hideLabel: true, stage: 2 },
        { t0: 1.6, t1: V, label: 'VSYNC 已申请', dashed: true, stage: 3 },
        { t0: V, t1: 17.6, label: 'doFrame callbacks', hideLabel: true, stage: 4 }
      ],
      WMS: [
        { t0: 18.8, t1: 20.2, label: 'relayoutWindow / WMS', callout: '⑤ relayoutWindow（慢）', hideLabel: true, stage: 5, hot: true }
      ],
      DL: [
        { t0: 28.0, t1: 34.0, label: 'record DisplayList / RenderNode', callout: 'record DisplayList（慢）', hideLabel: true, stage: 7, hot: true }
      ],
      RT: [
        { t0: 34.0, t1: 35.0, label: 'DrawFrameTask queued', callout: 'postAndWait：迟到', hideLabel: true, stage: 8, hot: true },
        { t0: 35.0, t1: 40, label: 'RT 接管', dashed: true, stage: 8 }
      ]
    },
    pins: [
      { stage: 1, t: 0.0 }, { stage: 2, t: 1.2 }, { stage: 3, t: 1.25 },
      { stage: 4, t: V }, { stage: 5, t: 18.8 }, { stage: 6, t: 20.2 },
      { stage: 7, t: 28.0 }, { stage: 8, t: 34.0 }
    ],
    phases: [
      { t0: 0, t1: 1.2, stage: 1, title: '请求刷新：同一入口，更重的 View 树',
        desc: '触发路径不变：requestLayout / invalidate 置脏标记并冒泡到 ViewRootImpl。区别是这一帧的 View 层级更深、measure/layout/draw 更重。',
        refs: ['View.requestLayout()', 'ViewRootImpl.requestLayout()'], lane: 'UI', hint: '触发本身很便宜，成本在 traversal。' },
      { t0: 1.2, t1: 1.6, stage: 2, title: 'scheduleTraversals：仍然只预约一次',
        desc: 'traversal 仍然被 mTraversalScheduled 合并；同步屏障与 postVsyncCallback 不变。问题会在 doFrame 之后的 measure/layout/draw 暴露。',
        refs: ['ViewRootImpl.scheduleTraversals()', 'Choreographer.postVsyncCallback()'], lane: 'CHOREO', hint: '预约不会因为 View 树重而变慢。' },
      { t0: 1.6, t1: V, stage: 3, title: '等待 APP VSYNC',
        desc: 'VSYNC 是硬节拍，不会因为 UI 忙而提前；Choreographer 在 APP VSYNC 相位唤醒。',
        refs: ['Choreographer.scheduleVsyncLocked()', 'DisplayEventReceiver.scheduleVsync()'], lane: 'CHOREO', hint: '等待不是 CPU 忙等；UI 线程在 Looper 上睡眠。' },
      { t0: V, t1: 17.6, stage: 4, title: 'doFrame：回调链开始',
        desc: 'INPUT → ANIMATION → INSETS_ANIMATION → TRAVERSAL → COMMIT 依次执行；traversal 回调进入 performTraversals。',
        refs: ['Choreographer.doFrame()', 'TraversalCallback.onVsync()'], lane: 'CHOREO', hint: '回调越多，留给 measure/layout/draw 的预算越少。' },
      { t0: 17.6, t1: 18.8, stage: 6, title: 'measureHierarchy 变慢：预测量也吃预算',
        desc: '这一帧的 measureHierarchy 用了约 1.2ms；View 层级深/约束变化让预测量也不便宜。',
        refs: ['ViewRootImpl.measureHierarchy()', 'ViewRootImpl.getRootMeasureSpec()'], lane: 'UI', hot: true, hint: '預测量不是免费的；它是 measure 预算的一部分。' },
      { t0: 18.8, t1: 20.2, stage: 5, title: 'relayoutWindow 变慢：窗口在变化',
        desc: '这一帧 relayoutWindow 用了约 1.4ms，窗口尺寸/insets 变化让 WMS 计算与客户端 Transaction 更新更重。',
        refs: ['ViewRootImpl.relayoutWindow()', 'IWindowSession.relayout()'], lane: 'WMS', hot: true, hint: '窗口稳定后 relayout 会便宜很多；先确认是否在 resize/insets 动画中。' },
      { t0: 20.2, t1: 24.5, stage: 6, title: 'performMeasure 超预算：层级深 / 重复测量',
        desc: 'measure 递归耗时约 4.3ms：View 层级深、MeasureSpec 变化导致缓存失效，或者布局约束不稳定。',
        refs: ['ViewRootImpl.performMeasure()', 'View.measure()', 'ViewGroup.measureChildren()'], lane: 'UI', hot: true, hint: 'measure cache 命中率与 View 层级是首要观察点。' },
      { t0: 24.5, t1: 28.0, stage: 6, title: 'performLayout 超预算：两趟布局',
        desc: 'layout 耗时约 3.5ms；如果 layout 中再次 requestLayout，会进入第二趟布局，进一步挤压 draw 预算。',
        refs: ['ViewRootImpl.performLayout()', 'View.layout()', 'requestLayoutDuringLayout()'], lane: 'UI', hot: true, hint: '避免在 layout 中修改会触发 requestLayout 的属性。' },
      { t0: 28.0, t1: 34.0, stage: 7, title: 'draw / record 超预算：DisplayList 录制变慢',
        desc: 'View.updateDisplayListIfDirty() 录制耗时约 6ms：绘制指令多、RenderNode 树大、hardware layer 变化。',
        refs: ['ViewRootImpl.performDraw()', 'View.updateDisplayListIfDirty()'], lane: 'DL', hot: true, hint: '录制慢通常是过度绘制/复杂 View 树，而不是 GPU。' },
      { t0: 34.0, t1: 40, stage: 8, title: '交棒迟到：syncAndDrawFrame 越过截止',
        desc: 'syncAndDrawFrame 在 34.0ms 才开始，交棒完成 35.0ms，已经越过 33.3ms 的预算截止；这一帧对 RT/SF 的时间线已经晚了一拍。',
        refs: ['ThreadedRenderer.syncAndDrawFrame()', 'DrawFrameTask::postAndWait()'], lane: 'RT', hot: true, hint: '交棒越线后，RT 再快也无法追回这一拍。' }
    ],
    slots: [],
    slotNote: 'UI 刷新链不涉及 BufferQueue；慢帧会推迟交棒。',
    misses: [
      { t: D, lane: 'UI', label: '', title: 'DEADLINE 33.3ms：syncAndDrawFrame 未完成' }
    ],
    budgetBase: 0
  };

  return { normal: NORMAL, jank: JANK };
})();