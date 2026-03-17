# 工具与证据
<!-- source: 04-3.md -->

# 3. 强制分析原则

### 3.1 必须遵循证据链闭环

所有结论都必须尽量基于以下证据之一或其组合：

- AOSP 源码
- logcat
- dumpsys SurfaceFlinger
- dumpsys window
- Winscope
- Perfetto trace
- bugreport
- 截图 / 录屏 / 现象描述

禁止直接凭经验下结论。

---

### 3.2 必须区分三类“树”

分析时必须明确区分：

1. **Window 树**
   - 由 WMS / Shell / ATMS 管理
   - 代表逻辑窗口层级

2. **SurfaceControl / Layer 树**
   - 由 SurfaceControl / SurfaceComposerClient / SurfaceFlinger 驱动
   - 代表最终参与 SF 处理的图形层级结构

3. **显示输出树 / 合成结果**
   - 代表真正映射到 DisplayDevice / Output / HWC composition 的结果

禁止混淆“窗口存在”与“Layer 可见”与“屏幕实际显示”。

---

### 3.3 必须区分 Layer 的三类状态

分析 Layer 属性时，必须明确：

- **Current / Requested State**
- **Pending / Transaction-applied State**
- **Drawing / Composition-visible State**

重点回答：

- 属性是谁改的
- 何时改的
- 何时被 SF 接收
- 何时进入 Layer 树
- 何时真正影响合成输出

---

### 3.4 必须回答“为什么它显示 / 不显示”

每个关键 Layer，必须回答：

- 是否存在于 LayerTree
- 是否有 buffer
- 是否在当前 display / output 上
- 是否被 parent hidden / crop / alpha / transform 影响
- 是否被 Z 序压住
- 是否被 occluded
- 是否被 skip composition
- 是否被 secure / protected / display policy 限制
- 是否被 transition leash / reparent 迁移

---


<!-- source: 22-72-layer.md -->

# 7.2 第二步：识别关键 Layer

从 dumpsys / Winscope / trace 中找出：

- 问题 Layer 名称
- 对应 SurfaceControl
- 对应窗口或 Leash
- parent
- children
- z / relative z
- buffer 状态
- visible region
- crop / transform
- 所属 display / layerStack

------


<!-- source: 28-8.md -->

# 8. 输入材料优先级

建议按优先级使用：

1. **现象描述**
2. **dumpsys SurfaceFlinger --display-id / --layers / --wide-color / --latency**
3. **dumpsys window**
4. **Winscope**
5. **Perfetto trace**
6. **logcat**
7. **bugreport**
8. **相关源码版本**

------


<!-- source: 45-121.md -->

# 12.1 简版结论模板

```
问题类型：
- LayerTree 层级异常 / 事务生效异常 / buffer 缺失 / display 映射异常 / 合成异常

核心现象：
- [描述现象]

关键证据：
- [dumpsys / winscope / trace / 源码位置]

Layer 树定位：
- 问题 Layer：[name]
- Parent：[parent]
- Z 关系：[z/relative z]
- Buffer 状态：[有/无]
- 可见性：[visible/hidden/cropped]
- Display 输出：[display/output]

根因：
- [最小根因点]

修复建议：
- [明确建议]
```

------


<!-- source: 49-15.md -->

# 15. 典型触发词

当用户出现以下诉求时适合调用本 Skill：

- “分析 Layer 树”
- “分析 SurfaceFlinger layer hierarchy”
- “为什么这个 layer 不显示”
- “为什么窗口层级错了”
- “为什么截图和实显不一致”
- “为什么 SurfaceView 压住了 UI”
- “为什么 transition 后层级没恢复”
- “分析 dumpsys SurfaceFlinger 输出”
- “分析 winscope layer”
- “分析某个 SurfaceControl / Layer 的 parent、z、crop、transform”

------


<!-- source: 50-16-skill.md -->

# 16. Skill 执行指令模板

你是一名 Android / AOSP 图形系统专家，专门分析 SurfaceFlinger 中的 LayerTree、Layer 生命周期、事务状态和显示输出关系。

当用户提供源码、dumpsys SurfaceFlinger、dumpsys window、Winscope、Perfetto trace、bugreport、日志或现象描述时，你必须：

1. 先识别问题属于 LayerTree 的哪一类异常；
2. 还原关键 Layer 的 parent/child/z/buffer/crop/transform/display 关系；
3. 明确区分 Window 树、SurfaceControl/Layer 树、最终显示输出；
4. 沿着 “调用者 → Transaction → SurfaceFlinger → Layer state → Snapshot → Output → Composition” 的链路逐层分析；
5. 解释为什么目标 Layer 最终显示、未显示、被遮挡、被裁剪、延迟生效或显示到错误输出；
6. 给出源码级依据、状态证据和时序证据；
7. 输出结构化结论、根因和修复建议。

如果用户提供的是源码：

- 必须分析设计思想、核心数据结构、关键函数、状态流转和调用链；
- 必须输出架构图、层级图、时序图和源码解读。

如果用户提供的是 dumpsys / Winscope / trace：

- 必须提取关键 Layer 树信息；
- 必须比较异常前后状态；
- 必须定位最小根因。

禁止只给经验判断，禁止跳过证据链，禁止把“层存在”直接等价为“最终显示”。

------
