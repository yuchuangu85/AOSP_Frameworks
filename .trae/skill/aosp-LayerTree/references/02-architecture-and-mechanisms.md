# 架构与核心机制
<!-- source: 06-41-surfaceflinger.md -->

# 4.1 SurfaceFlinger 主体

重点关注：

- `frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp`
- `frameworks/native/services/surfaceflinger/SurfaceFlinger.h`

分析重点：

- 事务处理入口
- Layer 创建与销毁
- commit / composite 主循环
- display / output 绑定
- mirror / clone / screenshot 相关流程

---


<!-- source: 07-42-layer.md -->

# 4.2 Layer 抽象与实现

重点关注：

- `frameworks/native/services/surfaceflinger/Layer.h`
- `frameworks/native/services/surfaceflinger/Layer.cpp`
- `frameworks/native/services/surfaceflinger/ContainerLayer.*`
- `frameworks/native/services/surfaceflinger/BufferStateLayer.*`
- `frameworks/native/services/surfaceflinger/BufferQueueLayer.*`
- `frameworks/native/services/surfaceflinger/EffectLayer.*`
- `frameworks/native/services/surfaceflinger/LayerVector.*`

分析重点：

- Layer 基础属性
- 父子关系与遍历
- Buffer Layer 与 Container Layer 差异
- Relative Z / LayerStack / Transform / Crop / Alpha / Visibility
- DrawingState / RequestedState
- 输入信息、触摸区域、遮挡区域

---


<!-- source: 08-43-frontend-layer.md -->

# 4.3 FrontEnd / Layer 生命周期相关

重点关注：

- `frameworks/native/services/surfaceflinger/FrontEnd/`
- `LayerLifecycleManager`
- `LayerSnapshotBuilder`
- `RequestedLayerState`
- `LayerHierarchy`

分析重点：

- 新版 SF 中前端状态如何收集
- LayerSnapshot 如何构建
- 树快照与合成快照的关系
- 生命周期变更如何影响最终可见输出

---


<!-- source: 09-44-composition-output-display.md -->

# 4.4 Composition / Output / Display

重点关注：

- `frameworks/native/services/surfaceflinger/CompositionEngine/`
- `Output.cpp`
- `OutputLayer.cpp`
- `DisplayDevice.cpp`
- `DisplayHardware/HWC2.*`

分析重点：

- Layer 如何映射到 OutputLayer
- 多 display / layerStack / mirror display 的关系
- 可见区域、裁剪、最终显示区域
- HWC/GPU 合成路径

---


<!-- source: 10-45-transaction-surfacecontrol.md -->

# 4.5 Transaction 与 SurfaceControl

重点关注：

- `frameworks/native/libs/gui/SurfaceComposerClient.cpp`
- `frameworks/native/libs/gui/SurfaceControl.cpp`
- `frameworks/native/services/surfaceflinger/TransactionCallbackInvoker.*`
- `frameworks/native/services/surfaceflinger/TransactionHandler.*`

分析重点：

- setLayer / setRelativeLayer / reparent / show / hide / setAlpha / setMatrix / setCrop / setBuffer 的落点
- Transaction merge / apply / listener callback
- BLAST / sync transaction 的行为

---


<!-- source: 11-46-window-shell-transition.md -->

# 4.6 Window / Shell / Transition 关联模块

重点关注：

- `frameworks/base/services/core/java/com/android/server/wm/`
- `WindowContainer`
- `SurfaceAnimator`
- `SurfaceFreezer`
- `BLASTSyncEngine`
- `Transition`
- `ShellTransition`

分析重点：

- WindowContainer 到 SurfaceControl 的组织方式
- Leash 的引入如何改变 LayerTree
- 动画 / 过渡过程中 parent 与 reparent 的变化
- 冻结层、截图层、过渡层对最终 LayerTree 的影响

---


<!-- source: 12-5-layertree.md -->

# 5. LayerTree 分析核心模型


<!-- source: 14-52-layer.md -->

# 5.2 Layer 显示判定模型

判断某个 Layer 是否“应该显示”，至少检查：

```
Layer存在
  ∧ parent链存在
  ∧ 已挂到正确display/output
  ∧ 未被hide
  ∧ alpha > 0 或效果层允许
  ∧ 有效buffer或可渲染内容
  ∧ crop / transform 后仍有可见区域
  ∧ 未被更高层完全遮挡（必要时）
  ∧ Z序正确
  ∧ 事务已生效到drawing/composition阶段
```

如果任何条件不成立，都应明确指出是哪一项失败。

------


<!-- source: 16-6.md -->

# 6. 必须输出的架构图与图示


<!-- source: 17-61-layertree.md -->

# 6.1 LayerTree 架构图

```
App / WMS / Shell
   │
   ├─ SurfaceControl / Transaction
   │        │
   │        └─ SurfaceFlinger
   │              │
   │              ├─ Layer (BufferStateLayer / BufferQueueLayer / ContainerLayer)
   │              │       └─ Parent / Child / Relative Z / Crop / Transform
   │              │
   │              ├─ LayerHierarchy / LayerSnapshot
   │              │
   │              ├─ Output / OutputLayer / DisplayDevice
   │              │
   │              └─ HWC / GPU Composition
   │
   └─ Screen Output
```

------


<!-- source: 21-71.md -->

# 7.1 第一步：明确分析对象

先回答：

- 问题发生在哪个窗口 / Layer / Display
- 现象是持续存在还是瞬时闪现
- 是截图可见还是仅肉眼可见
- 问题发生在：
  - 应用启动
  - 切换
  - 旋转
  - 分屏
  - 息屏亮屏
  - 动画过渡
  - SurfaceView 更新
  - 壁纸切换
  - 多显示器 / 投屏

------


<!-- source: 24-74.md -->

# 7.4 第四步：还原属性生效链路

必须回答属性变更链路：

```
谁调用了什么 API
→ Transaction 写了哪些字段
→ SF 何时收到
→ Layer 哪个 state 被修改
→ 何时进入 drawing / snapshot
→ 何时参与 output composition
→ 为什么最终显示结果是这样
```

------


<!-- source: 25-75-buffer.md -->

# 7.5 第五步：分析 buffer 与可见区域

重点检查：

- 当前是否有 buffer
- buffer 尺寸与 Layer 尺寸是否匹配
- transform/crop 后还有无可见区域
- parent crop 是否裁没了
- alpha 是否使其不可见
- 是否被 higher-z layer 完全遮挡

------


<!-- source: 26-76-display-output.md -->

# 7.6 第六步：分析 display/output 映射

必须检查：

- Layer 属于哪个 layerStack
- DisplayDevice / Output 是否接受它
- 是否是 virtual display / mirror display
- 截图抓取路径与实显路径是否相同

------


<!-- source: 35-11-layertree.md -->

# 11. LayerTree 专项异常模式库

以下模式需要优先匹配。


<!-- source: 37-112-z.md -->

# 11.2 Z 序类

### 模式 4：setLayer 生效但 relative z 覆盖了绝对 z

现象：

- 以为调高层级了，但仍被挡住

排查：

- 是否使用 `setRelativeLayer`
- relative parent 是否变动

------

### 模式 5：兄弟层顺序正确，但 parent 层级错误

现象：

- 同组内顺序正常，整体仍被别组压住

排查：

- 先看 parent z，再看 child z

------


<!-- source: 38-113.md -->

# 11.3 可见性类

### 模式 6：Layer 存在，但 hidden/alpha/crop 使其不可见

现象：

- dumpsys 能看到层
- 屏幕完全看不到

排查：

- hidden
- alpha
- crop
- parentAlpha
- zero-size region

------

### 模式 7：parent crop 把 child 完全裁掉

现象：

- 子层有 buffer，但就是不显示

排查：

- parent crop / window crop / final crop

------

### 模式 8：transform 后区域跑出屏幕

现象：

- 旋转、缩放、平移后消失

排查：

- matrix
- transformToDisplayInverse
- display transform

------


<!-- source: 39-114-buffer.md -->

# 11.4 Buffer 类

### 模式 9：Layer 树正确，但没有有效 buffer

现象：

- 层在树里
- 尺寸位置正常
- 实际为黑块或透明

排查：

- latch 时机
- acquire fence
- producer 提交
- BLAST 同步

------

### 模式 10：buffer 尺寸与 crop/transform 组合后变成空区域

现象：

- 某些分辨率或旋转角度下消失

排查：

- buffer size
- source crop
- destination frame
- transform hint

------


<!-- source: 42-117.md -->

# 11.7 动画与过渡类

### 模式 16：Transition 期间双层并存导致闪屏

现象：

- 新旧界面短暂叠加
- 闪一下旧画面

排查：

- transition leash
- snapshot layer
- old/new parent 切换时机

------

### 模式 17：冻结层未及时移除

现象：

- 画面停住
- 操作有效但界面不更新

排查：

- `SurfaceFreezer`
- screenshot/freeze layer 生命周期

------


<!-- source: 43-118-surfaceview-surface.md -->

# 11.8 SurfaceView / 子 Surface 类

### 模式 18：主窗口与 SurfaceView 层级关系异常

现象：

- SurfaceView 压住按钮
- 或被不该遮挡的 View 挡住

排查：

- 独立 Layer
- Z 序
- punch hole / alpha / parent-child 非同树关系

------

### 模式 19：子 Surface reparent 到错误容器

现象：

- 子画面漂移
- 跟随错误窗口动画

排查：

- child SurfaceControl parent
- leash 迁移链路

------


<!-- source: 51-17.md -->

# 17. 最终交付标准

一次合格的 LayerTree 分析，最终必须达到：

- 能说清楚 **Layer 从哪里来**
- 能说清楚 **为什么挂在这里**
- 能说清楚 **为什么在这个 Z 序**
- 能说清楚 **为什么它显示 / 不显示**
- 能说清楚 **事务何时生效**
- 能说清楚 **为什么截图与实显可能不同**
- 能说清楚 **根因在上层、SF、buffer、合成还是 display 输出**
- 能给出 **可验证、可复现、可修复** 的结论
