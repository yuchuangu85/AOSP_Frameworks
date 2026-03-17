# 输出模板与检查清单
<!-- source: 21-95-hwc-drm-panel.md -->

# 9.5 HWC → DRM → Panel 关键链路

```
SurfaceFlinger
  → Composer HAL
  → HWC2 validate / accept changes / present
  → vendor display pipeline
  → DRM/KMS atomic commit
  → crtc / plane / connector / encoder 配置
  → pageflip
  → panel 扫描输出
```

------


<!-- source: 70-175-hwc-drm-panel.md -->

# 17.5 HWC / DRM / Panel

- 排查 overlay capability
- 优化 validate/present 路径
- 减少 mode switch 抖动
- 优化 atomic commit
- 排查 present fence 与 panel timing

------

# 18. 输出模板
