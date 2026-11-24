/* SurfaceFlinger one-cycle scenarios (custom WorkDuration: app=sf=8.3ms). Times illustrative. */
window.__SF_SCENARIOS__ = (function () {
  'use strict';
  var APP = 0;          // APP VSYNC
  var SFV = 8.333;      // SF VSYNC = APP + appWorkDuration
  var D = 16.667;       // target HW VSYNC = SFV + sfWorkDuration
  var END = 34.0;       // scanout window

  var NORMAL = {
    key: 'normal', label: '正常帧', total: END,
    vsyncs: [APP, SFV, D, 33.333],
    vsyncLabels: ['APP VSYNC', 'SF VSYNC', 'HW VSYNC · 扫描输出', 'RETIRE / release'],
    appVsync: SFV, deadline: D, queueDone: 12.933, loopAt: END,
    present: 'present · 12.9 ms',
    lanes: {
      APP: [
        { t0: APP, t1: SFV, label: 'APP 绘制 + queueBuffer', callout: 'APP VSYNC：8.3ms 生产窗口', dashed: true, stage: 1 },
        { t0: SFV, t1: 8.933, label: 'BLAST tx + buffer', callout: '① Transaction / BufferData 到达 SF', hideLabel: true, stage: 1 },
        { t0: 8.933, t1: D, label: 'buffer 已排队 · 等 SF 消费', dashed: true, stage: 1 }
      ],
      SCHED: [
        { t0: SFV, t1: 8.533, label: 'vsyncCallback', callout: 'SF VSYNC +8.3ms → onFrameSignal', hideLabel: true, stage: 2 },
        { t0: 8.533, t1: 9.333, label: 'commit', title: 'commitTransactions / snapshots', hideLabel: true, stage: 3 },
        { t0: 9.333, t1: 12.933, label: 'composite()', stage: 7 },
        { t0: 12.933, t1: D, label: 'postComposition · 等 HW VSYNC', dashed: true, stage: 9 }
      ],
      LAYER: [
        { t0: 8.533, t1: 9.133, label: 'apply tx', title: 'apply transactions / updateLayerSnapshots', callout: 'updateLayerSnapshots / apply tx → latch', hideLabel: true, stage: 3 },
        { t0: 9.133, t1: 10.133, label: 'latch', title: 'latch buffer / acquire fence', hideLabel: true, stage: 4 },
        { t0: 10.133, t1: 10.733, label: 'snapshot ready', hideLabel: true, stage: 4 }
      ],
      COMP: [
        { t0: 10.133, t1: 10.733, label: 'choose / validate', callout: 'chooseCompositionStrategy → validate', hideLabel: true, stage: 5 },
        { t0: 10.733, t1: 12.133, label: 'GPU', title: 'RenderEngine client composition', callout: 'RenderEngine client composition', hideLabel: true, stage: 6 },
        { t0: 12.133, t1: 12.933, label: 'presentDisplay', callout: 'presentDisplay → postComposition / FrameTimeline', hideLabel: true, stage: 7 },
        { t0: 12.933, t1: 13.733, label: 'postComposition', hideLabel: true, stage: 9 }
      ],
      DISP: [
        { t0: APP, t1: D, label: '上一帧扫描输出', dashed: true, stage: 8 },
        { t0: D, t1: 33.333, label: '本帧扫描输出', stage: 8 },
        { t0: 33.333, t1: END, label: 'retire / release', hideLabel: true, stage: 9 }
      ]
    },
    pins: [
      { stage: 1, t: APP }, { stage: 2, t: SFV }, { stage: 3, t: 8.533 },
      { stage: 4, t: 9.133 }, { stage: 5, t: 10.133 }, { stage: 6, t: 10.733 },
      { stage: 7, t: 12.133 }, { stage: 8, t: D }, { stage: 9, t: 12.933 }
    ],
    phases: [
      { t0: APP, t1: SFV, stage: 1, title: 'APP VSYNC → SF VSYNC：8.3ms app 生产窗口',
        desc: '自定义 WorkDuration 下 appWorkDuration = 8.3ms、sfWorkDuration = 8.3ms。APP 在 0.0ms 醒来，完成 traversal/RT 绘制并把 buffer 以 BLAST Transaction 提交；SF 要到 +8.3ms 才进入自己的 VSYNC 相位。',
        refs: ['Choreographer APP VSYNC', 'appWorkDuration = 8.3ms', 'BLAST Transaction'], lane: 'APP', hint: '本页从 APP VSYNC 起画；APP 内部绘制细节见刷新链专题。' },
      { t0: SFV, t1: 8.533, stage: 2, title: 'SF VSYNC +8.3ms：MessageQueue → onFrameSignal',
        desc: 'Scheduler 在 SF 的 VSYNC 相位唤醒：MessageQueue::vsyncCallback 生成 VsyncId，把帧投到 SF 主线程；Scheduler::onFrameSignal 进入 commit / composite 两段式。从此刻到目标 HW VSYNC 只剩 sfWorkDuration = 8.3ms。',
        refs: ['MessageQueue::vsyncCallback()', 'Scheduler::onFrameSignal()', 'ICompositor::commit() / composite()'], lane: 'SCHED', hint: 'APP 与 SF 相差 appWorkDuration = 8.3ms；SF 预算只有 sfWorkDuration = 8.3ms。' },
      { t0: 8.533, t1: 9.333, stage: 3, title: 'commit：应用事务、生成 LayerSnapshot',
        desc: 'commit 阶段调用 updateLayerSnapshots：把 BLAST Transaction 应用到 mCurrentState，执行 commitTransactions/doCommitTransactions，生成只读 LayerSnapshot，并在后半段执行 latch。',
        refs: ['SurfaceFlinger::commit()', 'updateLayerSnapshots()', 'commitTransactionsLocked()'], lane: 'LAYER', hint: 'commit 不负责画，它负责把“这一帧要合成的状态”固定下来。' },
      { t0: 9.333, t1: 10.133, stage: 4, title: 'latch：buffer 与 acquire fence',
        desc: 'Layer::latchBufferImpl 检查 buffer 的 acquire fence；fence 未 signal 时不会阻塞等待，而是 onLayerUpdate 并跳过这一帧。latch 成功后 SurfaceFrame 标记 Presented，buffer 进入可合成状态。',
        refs: ['Layer::latchBufferImpl()', 'fenceHasSignaled()', 'BufferData'], lane: 'LAYER', hint: 'latch 失败不是“等一下”，而是整帧不消费并请求下一次合成。' },
      { t0: 10.133, t1: 10.733, stage: 5, title: '合成决策：CompositionEngine + HWC validate',
        desc: 'CompositionEngine::present 组织 OutputLayer，调用 chooseCompositionStrategy 让 HWC 选择 device/client composition；HWComposer::getDeviceCompositionChanges 可能合并或跳过 validate，最终由 HWC2::Display::validate 得到 composition type。',
        refs: ['CompositionEngine::present()', 'Display::chooseCompositionStrategy()', 'HWComposer::getDeviceCompositionChanges()'], lane: 'COMP', hint: 'validate 的结果决定后面要不要 RenderEngine 做 GPU 合成。' },
      { t0: 10.733, t1: 12.133, stage: 6, title: 'GPU client composition：RenderEngine 合成图层',
        desc: '如果 HWC 要求 client composition，CompositionEngine 让 RenderEngine 先把图层画到 client target；完成后 setClientTarget 给 HWC。GPU 合成耗时直接加在 present 之前——在 8.3ms 的 SF 预算里尤其危险。',
        refs: ['Output::composeSurfaces()', 'RenderEngine::drawLayers()', 'HWComposer::setClientTarget()'], lane: 'COMP', hint: 'client composition 是最常见的 SF jank 来源之一。' },
      { t0: 12.133, t1: 12.933, stage: 7, title: 'present：HWComposer::presentDisplay',
        desc: 'presentDisplay 把 validate 后的图层集合提交给 HWC；present 是阻塞调用，返回时 present/release fence 已经就绪。真正上屏还要等 HWC/DRM 在 16.7ms 的 HW VSYNC 扫描输出。',
        refs: ['HWComposer::presentAndGetReleaseFences()', 'HWC2::Display::present()', 'Display::presentFrame()'], lane: 'COMP', hint: 'present 在 12.9ms 完成，距 HW VSYNC 还有约 3.7ms 余量。' },
      { t0: 12.933, t1: D, stage: 9, title: 'postComposition、FrameTimeline 与等待 HW VSYNC',
        desc: 'present 之后，SF 调用 onCompositionPresented、setSfPresent，把 present fence 与时间线交给 FrameTimeline；随后等待 16.7ms 的 HW VSYNC。retire fence 和 buffer release 在扫描输出完成后异步回传。',
        refs: ['SurfaceFlinger::onCompositionPresented()', 'FrameTimeline::setSfPresent()', 'BufferReleaseChannel'], lane: 'DISP', hint: 'SF 的 CPU 工作在 12.9ms 已完成，但显示链路还没有结束。' },
      { t0: D, t1: END, stage: 8, title: '扫描输出：HWC / DRM / panel 与 retire',
        desc: 'HW VSYNC 到来后，HWC/DRM 把本帧内容扫描到面板；扫描完成后 retire fence 触发，buffer 才能 release 回 producer。扫描窗口持续到下一个 HW VSYNC。',
        refs: ['HWComposer::presentAndGetReleaseFences()', 'Display::presentFrame()', 'Layer::callReleaseBufferCallback()'], lane: 'DISP', hint: 'retire fence 是“扫描完成”的信号，不是 present 调用的返回值。' }
    ],
    slots: [
      { name: 'BUFFER 0 · 上一帧', ranges: [{ from: 0, to: D, state: 'DISPLAY', owner: 'Display' }, { from: D, to: END, state: 'FREE', owner: '—' }] },
      { name: 'BUFFER 1 · 本帧', ranges: [
        { from: 0, to: 9.133, state: 'QUEUED', owner: 'app / BufferQueue' },
        { from: 9.133, to: D, state: 'LATCHED', owner: 'SurfaceFlinger' },
        { from: D, to: 33.333, state: 'DISPLAY', owner: 'Display' },
        { from: 33.333, to: END, state: 'FREE', owner: '—' }
      ] },
      { name: 'BUFFER 2 · 空闲', ranges: [{ from: 0, to: END, state: 'FREE', owner: '—' }] }
    ],
    slotNote: '自定义 WorkDuration：APP 0.0ms 醒来，SF 8.3ms 醒来，HW VSYNC 16.7ms。buffer 随 BLAST Transaction 到达 SF；latch 需要 acquire fence，扫描完成后才 release。',
    misses: [],
    budgetBase: D
  };

  var JANK = {
    key: 'jank', label: '慢帧 / GPU 合成', total: 50.5,
    vsyncs: [APP, SFV, D, 33.333, 50],
    vsyncLabels: ['APP VSYNC', 'SF VSYNC', 'HW VSYNC · MISS', 'HW VSYNC · 本帧上屏', 'RETIRE / release'],
    appVsync: SFV, deadline: D, queueDone: 18.2, loopAt: 50.5,
    present: 'present · 18.2 ms（越过 HW VSYNC）',
    lanes: {
      APP: [
        { t0: 0, t1: SFV, label: 'APP 绘制 + queueBuffer', callout: 'APP VSYNC：8.3ms 生产窗口', dashed: true, stage: 1 },
        { t0: SFV, t1: 8.933, label: 'BLAST tx + buffer', callout: '① Transaction / BufferData 到达 SF', hideLabel: true, stage: 1 },
        { t0: 8.933, t1: D, label: 'buffer 已排队', dashed: true, stage: 1 }
      ],
      SCHED: [
        { t0: SFV, t1: 8.533, label: 'vsyncCallback', callout: 'SF VSYNC +8.3ms', hideLabel: true, stage: 2 },
        { t0: 8.533, t1: 9.1, label: 'commit', title: 'commitTransactions / snapshots', hideLabel: true, stage: 3 },
        { t0: 9.1, t1: 18.2, label: 'composite()（超 8.3ms 预算）', stage: 7, hot: true },
        { t0: 18.2, t1: 33.333, label: '等 HW VSYNC · 这一拍已错过', dashed: true, stage: 9 }
      ],
      LAYER: [
        { t0: 8.5, t1: 9.1, label: 'apply tx', callout: 'apply tx → latch（acquire fence 晚）→ snapshot', hideLabel: true, stage: 3 },
        { t0: 9.1, t1: 10.5, label: 'latch（晚）', hideLabel: true, stage: 4, hot: true },
        { t0: 10.5, t1: 11.1, label: 'snapshot', hideLabel: true, stage: 4 }
      ],
      COMP: [
        { t0: 10.5, t1: 11.1, label: 'choose / validate', callout: 'composition type: client', hideLabel: true, stage: 5 },
        { t0: 11.1, t1: 16.9, label: 'GPU comp（慢）', callout: 'RenderEngine client composition（慢）', hideLabel: true, stage: 6, hot: true },
        { t0: 16.9, t1: 18.2, label: 'presentDisplay（越拍）', callout: 'presentDisplay：越过 HW VSYNC', hideLabel: true, stage: 7, hot: true },
        { t0: 18.2, t1: 19.2, label: 'postComposition', hideLabel: true, stage: 9 }
      ],
      DISP: [
        { t0: 0, t1: 33.333, label: '上一帧持续扫描 · HW VSYNC 错过', dashed: true, stage: 8, hot: true },
        { t0: 33.333, t1: 50, label: '本帧扫描输出', stage: 8 },
        { t0: 50, t1: 50.5, label: 'retire / release', hideLabel: true, stage: 9 }
      ]
    },
    pins: [
      { stage: 1, t: APP }, { stage: 2, t: SFV }, { stage: 3, t: 8.533 },
      { stage: 4, t: 9.1 }, { stage: 5, t: 10.5 }, { stage: 6, t: 11.1 },
      { stage: 7, t: 16.9 }, { stage: 8, t: 33.333 }, { stage: 9, t: 18.2 }
    ],
    phases: [
      { t0: 0, t1: SFV, stage: 1, title: 'APP VSYNC → SF VSYNC：同一 8.3ms 生产窗口',
        desc: 'APP 在 0.0ms 醒来并完成绘制/提交；SF 在 +8.3ms 才醒来。自定义 WorkDuration 把这段相位差固定为 appWorkDuration = 8.3ms。',
        refs: ['Choreographer APP VSYNC', 'appWorkDuration = 8.3ms'], lane: 'APP', hint: '这一帧的瓶颈不在 app 侧，而是 SF 的 8.3ms 合成预算。' },
      { t0: SFV, t1: 8.5, stage: 2, title: 'SF VSYNC +8.3ms：更紧的 8.3ms 预算',
        desc: 'Scheduler 仍在 SF VSYNC 唤醒；但 sfWorkDuration = 8.3ms，意味着 commit + latch + 合成 + present 必须在 16.667ms 前完成。',
        refs: ['Scheduler::onFrameSignal()', 'sfWorkDuration = 8.3ms'], lane: 'SCHED', hot: true, hint: '同一帧的 SF 工作被压缩到 8.3ms。' },
      { t0: 8.5, t1: 9.1, stage: 3, title: 'commit：事务与快照（不变）',
        desc: 'commit 仍应用事务、更新快照并进入 latch；这一帧的瓶颈不在 commit，而是之后的 latch fence 与 GPU 合成。',
        refs: ['SurfaceFlinger::commit()', 'updateLayerSnapshots()'], lane: 'LAYER', hint: '先区分 commit 慢、latch 慢还是 composite 慢。' },
      { t0: 9.1, t1: 10.5, stage: 4, title: 'latch 变慢：acquire fence 迟到',
        desc: 'buffer 的 acquire fence 到 10.5ms 才 signal，latch 被推迟；SF 不会忙等，但这一帧的合成起点被推后。',
        refs: ['Layer::latchBufferImpl()', 'fenceHasSignaled()'], lane: 'LAYER', hot: true, hint: 'latch 推迟直接吃掉 8.3ms 预算。' },
      { t0: 10.5, t1: 11.1, stage: 5, title: 'HWC 选择 client composition',
        desc: 'validateDisplay 之后，HWC 认为部分图层不能直接 overlay，composition type 变成 client；下一步必须 RenderEngine 先合成。',
        refs: ['chooseCompositionStrategy()', 'HWComposer::getDeviceCompositionChanges()'], lane: 'COMP', hint: 'client composition 一旦触发，就会多出一段 GPU 绘制。' },
      { t0: 11.1, t1: 16.9, stage: 6, title: 'RenderEngine 合成越过 8.3ms 预算',
        desc: 'RenderEngine 画 client target 耗时约 5.8ms，SF 的 8.3ms 预算已被 latch 与 GPU 合成吃掉；present 只能在 16.9ms 开始。',
        refs: ['Output::composeSurfaces()', 'RenderEngine::drawLayers()'], lane: 'COMP', hot: true, hint: '在自定义 WorkDuration 下，GPU 合成余量比默认 15.7ms 小得多。' },
      { t0: 16.9, t1: 18.2, stage: 7, title: 'present 越过 16.7ms HW VSYNC',
        desc: 'presentDisplay 到 18.2ms 才返回，已经越过 16.7ms 的 HW VSYNC；这一帧无法在本拍扫描，只能等下一次 HW VSYNC。',
        refs: ['HWComposer::presentAndGetReleaseFences()', 'Display::presentFrame()'], lane: 'COMP', hot: true, hint: 'present 一旦过拍，显示链路只能顺延。' },
      { t0: 18.2, t1: 33.333, stage: 9, title: '错过 HW VSYNC：等下一拍',
        desc: 'present 完成但已过拍，SF 只能等下一个 HW VSYNC 扫描输出；这一期间面板继续显示上一帧。',
        refs: ['SurfaceFlinger::onCompositionPresented()', 'FrameTimeline::setSfPresent()'], lane: 'DISP', hot: true, hint: '“present 返回”与“像素可见”之间还隔着一个 HW VSYNC。' },
      { t0: 33.333, t1: 50.5, stage: 8, title: '下一拍扫描输出与 retire/release',
        desc: '33.3ms 的 HW VSYNC 才扫描本帧；扫描完成后 retire/release 回 producer。用户看到的是晚一拍的更新。',
        refs: ['HWComposer::presentAndGetReleaseFences()', 'Layer::callReleaseBufferCallback()'], lane: 'DISP', hot: true, hint: 'present 越拍，端到端延迟整体增加一个刷新周期。' }
    ],
    slots: [
      { name: 'BUFFER 0 · 上一帧', ranges: [{ from: 0, to: 33.333, state: 'DISPLAY', owner: 'Display（重复）' }, { from: 33.333, to: 50.5, state: 'FREE', owner: '—' }] },
      { name: 'BUFFER 1 · 本帧', ranges: [
        { from: 0, to: 10.5, state: 'QUEUED', owner: 'app / BufferQueue' },
        { from: 10.5, to: 33.333, state: 'LATCHED', owner: 'SurfaceFlinger' },
        { from: 33.333, to: 50, state: 'DISPLAY', owner: 'Display' },
        { from: 50, to: 50.5, state: 'FREE', owner: '—' }
      ] },
      { name: 'BUFFER 2 · 下一帧', ranges: [{ from: 0, to: 33.333, state: 'FREE', owner: '—' }, { from: 33.333, to: 50.5, state: 'QUEUED', owner: 'app（示意）' }] }
    ],
    slotNote: '自定义 WorkDuration 下 SF 预算只有 8.3ms；GPU 合成慢会迅速越过 16.7ms 的 HW VSYNC，buffer 在 LATCHED 停留更久。',
    misses: [
      { t: D, lane: 'COMP', label: '', title: 'HW VSYNC 16.7ms：present 未完成' },
      { t: 18.2, lane: 'COMP', label: '', title: 'present 迟到 1.5ms（越过 HW VSYNC）' }
    ],
    budgetBase: D
  };

  return { normal: NORMAL, jank: JANK };
})();
