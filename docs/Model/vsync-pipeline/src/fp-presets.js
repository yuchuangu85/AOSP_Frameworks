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
      note: '周期 < 15ms 时用 high_fps offset（app = 2ms，sf = 1ms）：appWorkDuration = 2P − 1ms，约 3 个周期上屏，三个 slot 同时在用。' },
    { key: 'ui', label: 'UI 超时', fps: 60, dur: 'half', load: { ui: 2.2 }, frames: 2, cont: false, focus: 1,
      note: 'F1 的 measure / layout / draw 变为 2.2×，queueBuffer 越过本帧 SF 唤醒点，顺延一个周期上屏，期间屏幕重复上一帧。' },
    { key: 'uicont', label: 'UI 超时 · 连续动画', fps: 60, dur: 'half', load: { ui: 2.2 }, frames: 3, cont: true, focus: 1,
      note: '同样的 F1 超时，但动画还在继续：F2 按时就绪，SF 在同一次唤醒里只保留最新 buffer，迟到的 F1 被跳过（dropped）。' },
    { key: 'gpu', label: 'GPU 超时', fps: 60, dur: 'half', load: { gpu: 4 }, frames: 2, cont: false, focus: 1,
      note: 'F1 的 queueBuffer 按时完成，但 GPU 执行了 4×，acquire fence 晚于 SF 唤醒点，事务未就绪，顺延一个周期。' },
    { key: 'sf', label: 'SF 超时', fps: 60, dur: 'half', load: { sf: 5 }, frames: 3, cont: true, focus: 1,
      note: 'F1 按时交付，但 SF 的 latch + composite + HWC 变为 5×，越过了预期的 HW VSYNC，本帧晚一个周期上屏；F1 的 present fence 未 signal 时 SF 会 backpressure 跳过 commit，按时交付的 F2 也被推迟。' }
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

  /* params: { fps, dur: 'half'|'phase'|'custom', aD, sD, load:{ui,rt,gpu,sf}, frames, cont, loadFrame } */
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
      cont: p.cont !== false
    };
  };

  FP.paramsFromPreset = function (key) {
    var pr = FP.PRESET[key] || FP.PRESETS[0];
    return { fps: pr.fps, dur: pr.dur, load: Object.assign({}, pr.load), frames: pr.frames, cont: pr.cont, loadFrame: 1 };
  };
})(window.FP);
