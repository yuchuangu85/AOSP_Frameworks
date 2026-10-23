/* ============================================================
   Frame Pipeline Studio · costs, presets, VSYNC configuration
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';

  // Base cost (ms) of each piece of work at load 1×. Illustrative magnitudes, not measurements.
  FP.COST = { req: 1.2, doframe: 0.6, trav: 1.5, draw: 1.6, sync: 0.7, commit: 0.2,
              rtdraw: 1.3, queue: 0.6, gpu: 2.0, latch: 0.8, comp: 1.6, hwc: 0.7 };

  FP.PRESETS = [
    { key: 'relay', label: '同周期接力', fps: 60, dur: 'half', load: {}, frames: 3, cont: true, focus: 0,
      note: 'appWorkDuration ≈ sfWorkDuration ≈ P/2：APP 画完，SF 在同一周期内合成，下一个 HW VSYNC 上屏。这是一组合法配置，不是 AOSP 默认值。' },
    { key: 'aosp60', label: 'AOSP 默认 · 60Hz', fps: 60, dur: 'phase', load: {}, frames: 3, cont: true, focus: 0,
      note: '不设任何 sysprop 时用 PhaseOffsets（app = sf = 1ms）：appWorkDuration = P，sfWorkDuration = P − 1ms，APP 唤醒到上屏约 2 个周期。' },
    { key: 'aosp120', label: 'AOSP 默认 · 120Hz', fps: 120, dur: 'phase', load: {}, frames: 3, cont: true, focus: 0,
      note: '周期 < 15ms 时用 high_fps offset（app = 2ms，sf = 1ms）：appWorkDuration = 2P − 1ms，目标约 3 个周期后上屏。动画仍使用三个示意 slot，不代表真机容量。' },
    { key: 'ui', label: 'UI 超时', fps: 60, dur: 'half', load: { ui: 2.2 }, frames: 2, cont: false, focus: 1,
      note: 'F1 的 measure / layout / draw 变为 2.2×，queueBuffer 越过本帧 SF 唤醒点，顺延一个周期上屏，期间屏幕重复上一帧。' },
    { key: 'uicont', label: 'UI 超时 · 连续动画', fps: 60, dur: 'half', load: { ui: 2.2 }, frames: 3, cont: true, focus: 1,
      note: 'F1 迟到后，BLAST 默认开启的 layer backpressure 保留 F1；同轮的 F2 自动时间戳事务继续留队，而不是必然覆盖 F1。' },
    { key: 'drop', label: '关闭 layer 背压', fps: 60, dur: 'half', load: { ui: 2.2 }, frames: 3, cont: true, focus: 1, backpressure: false,
      note: '对照场景：显式关闭 layer backpressure，同轮可应用的 F2 覆盖 F1。此处的 dropped 不是普通 BLAST 窗口的默认结论。' },
    { key: 'gpu', label: 'GPU 延迟', fps: 60, dur: 'half', load: { gpu: 4 }, frames: 2, cont: false, focus: 1,
      note: '假定满足单层简单更新、首个事务和 late 配置等条件：GPU fence 虽晚于 SF 唤醒，仍可先 latch；能否按时还取决于呈现前 GPU 依赖是否完成。' },
    { key: 'gpuwait', label: 'GPU · 等完成再 latch', fps: 60, dur: 'half', load: { gpu: 4 }, frames: 2, cont: false, focus: 1, latchUnsignaled: false,
      note: '对照场景：不满足或禁用未完成 latch，SF 等待 acquire fence signal 后再接纳事务。本例因此顺延一次刷新；并非所有 GPU 延迟都如此。' },
    { key: 'sf', label: 'SF 超时', fps: 60, dur: 'half', load: { sf: 5 }, frames: 3, cont: true, focus: 1,
      note: 'F1 的 SF 工作量为 5×，越过目标显示节拍；后续提交还受 SF 主线程与本应完成的历史 present fence 约束。不是见到上一帧 fence pending 就一律停顿。' }
  ];
  FP.PRESET = {};
  FP.PRESETS.forEach(function (p) { FP.PRESET[p.key] = p; });

  // PhaseOffsets → durations, mirroring VsyncConfiguration.cpp (sfOffsetToDuration / appOffsetToDuration).
  function phaseOffsetDurations(P) {
    var appOff = P >= 15 ? 1 : 2, sfOff = 1;       // vsync_event_phase_offset_ns / high_fps_* defaults
    var sD = P - sfOff;
    var aD = P + (sfOff - appOff);
    if (aD < P) aD += P;
    return { aD: aD, sD: sD };
  }

  /* latchUnsignaled selects an eligible single-layer late-config scenario,
     not an unconditional promise made by debug.sf.auto_latch_unsignaled. */
  FP.resolveParams = function (p) {
    var fps = Number(p.fps) || 60;
    var P = 1000 / fps, aD, sD;
    if (p.dur === 'phase') { var d = phaseOffsetDurations(P); aD = d.aD; sD = d.sD; }
    else if (p.dur === 'custom') {
      aD = Math.min(Math.max(Number(p.aD) || P / 2, 0.5), 3 * P);
      sD = Math.min(Math.max(Number(p.sD) || P / 2, 0.5), 3 * P);
    } else { aD = P / 2; sD = P / 2; }
    var load = p.load || {};
    return {
      fps: fps, P: P, aD: aD, sD: sD, dur: p.dur || 'half',
      load: { ui: load.ui || 1, rt: load.rt || 1, gpu: load.gpu || 1, sf: load.sf || 1 },
      loadFrame: p.loadFrame == null ? 1 : p.loadFrame,
      frames: p.frames === 2 ? 2 : 3,
      cont: p.cont !== false,
      backpressure: p.backpressure !== false,
      latchUnsignaled: p.latchUnsignaled !== false
    };
  };

  FP.paramsFromPreset = function (key) {
    var pr = FP.PRESET[key] || FP.PRESETS[0];
    return { fps: pr.fps, dur: pr.dur, load: Object.assign({}, pr.load), frames: pr.frames, cont: pr.cont, loadFrame: 1,
      backpressure: pr.backpressure !== false, latchUnsignaled: pr.latchUnsignaled !== false };
  };
})(window.FP);
