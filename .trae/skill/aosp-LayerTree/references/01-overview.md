# 概览与范围
<!-- source: 00-overview.md -->

# aosp-LayerTree


<!-- source: 19-63.md -->

# 6.3 父子层级示意图

```
Root
 ├─ Display Root
 │   ├─ SystemUI Container
 │   │   ├─ StatusBar
 │   │   └─ NavigationBar
 │   ├─ App Leash
 │   │   └─ App Main Window
 │   │       ├─ DecorView
 │   │       └─ SurfaceView
 │   ├─ Wallpaper
 │   └─ Screenshot / Transition Layer
```

分析时必须将问题 Layer 放入类似结构中定位。

------


<!-- source: 29-9.md -->

# 9. 对常见输入的分析要求


<!-- source: 30-91-dumpsys-surfaceflinger.md -->

# 9.1 如果输入是 dumpsys SurfaceFlinger

必须提取：

- Layer 名称
- parent / child
- z / relative z
- buffer state
- size / position / crop
- transform
- alpha / hidden
- layerStack / display 归属
- visibility hints
- composition state

并给出“哪一层不对”。

------


<!-- source: 31-92-winscope.md -->

# 9.2 如果输入是 Winscope

必须分析：

- Layer hierarchy 时间变化
- 瞬时 reparent
- transition leash 生命周期
- 显示前后 layer 树差异
- 有问题帧前后的树结构变化

------


<!-- source: 33-94.md -->

# 9.4 如果输入是源码

必须输出：

- 设计意图
- 数据结构定义
- 关键函数调用链
- 状态更新时机
- 与现象的映射关系

------


<!-- source: 46-122.md -->

# 12.2 完整分析模板

```
一、问题概述
- 现象：
- 触发条件：
- 影响范围：

二、LayerTree 还原
- 根节点：
- 关键 parent/child：
- 问题 Layer 所在位置：
- 相关 sibling：
- 是否存在 leash / mirror / freeze / screenshot layer：

三、事务与状态分析
- 上层调用：
- Transaction 内容：
- SurfaceFlinger 接收点：
- 状态更新点：
- drawing/composition 生效时机：

四、可见性与几何分析
- alpha：
- hidden：
- crop：
- transform：
- buffer：
- visible region：
- 遮挡关系：

五、Display / Output 分析
- layerStack：
- target display：
- output layer：
- HWC/GPU 路径：

六、源码定位
- 类：
- 函数：
- 关键判断：
- 设计意图：

七、根因结论
- 根因 1：
- 根因 2：
- 最终主因：

八、修复建议
- 短期修复：
- 中期治理：
- 长期架构建议：
```

------
