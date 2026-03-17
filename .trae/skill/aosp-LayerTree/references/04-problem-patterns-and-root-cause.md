# 问题模式与根因
<!-- source: 27-77.md -->

# 7.7 第七步：给出根因与修复建议

根因必须具体到：

- 错误 parent
- 错误 z
- 错误 crop
- transaction 未生效
- leash 迁移未结束
- buffer 缺失
- output 过滤
- HWC/GPU 合成分支异常

------


<!-- source: 36-111.md -->

# 11.1 树结构类

### 模式 1：Layer 被挂到错误 parent

现象：

- 窗口存在但显示位置/层级完全不对
- 跟随了错误容器移动

排查：

- 检查 reparent 调用链
- 检查 transition leash 是否仍在生效

------

### 模式 2：Layer 被临时 leash 接管后未恢复

现象：

- 动画结束后窗口仍异常
- 截图层或过渡层残留

排查：

- `SurfaceAnimator`
- `Transition`
- `SurfaceFreezer`
- leash 销毁与 reparent 恢复

------

### 模式 3：Mirror Layer 显示源不对

现象：

- 镜像显示内容错乱
- 投屏/录屏内容不一致

排查：

- mirror root source
- output target mapping

------


<!-- source: 48-14-skill.md -->

# 14. 推荐联合 Skill

在复杂问题中，建议联合以下 Skill 使用：

- `aosp-SurfaceFlinger`
- `aosp-SurfaceControl`
- `aosp-surface`
- `aosp-BufferQueue`
- `aosp-Fence`
- `aosp-VSYNC`
- `aosp-animation`
- `aosp-wms`
- `aosp-input`
- `aosp-jank`

联动策略：

- **LayerTree 异常 + 窗口层级异常** → 联合 `aosp-wms`
- **LayerTree 异常 + buffer 不更新** → 联合 `aosp-BufferQueue`
- **LayerTree 正常但实显异常** → 联合 `aosp-SurfaceFlinger` / `aosp-Fence`
- **动画期间闪屏/错层** → 联合 `aosp-animation`
- **掉帧导致层级变化延迟** → 联合 `aosp-jank`

------
