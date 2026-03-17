# 输出模板与检查清单
<!-- source: 13-51-layertree.md -->

# 5.1 LayerTree 基础抽象模型

将 LayerTree 抽象为以下模型：

```text
Client / WMS / Shell
   ↓
SurfaceControl / Transaction
   ↓
SurfaceFlinger 接收状态变更
   ↓
Layer 创建 / 更新 / reparent / setZ / setBuffer
   ↓
构建 LayerHierarchy
   ↓
生成 LayerSnapshot / OutputLayer
   ↓
可见性、裁剪、透明度、变换求值
   ↓
按 Display / Output 组织
   ↓
HWC / GPU 合成
   ↓
屏幕输出
```


<!-- source: 34-10.md -->

# 10. 必须掌握的关键问题清单

执行分析时，必须尽量回答下列问题：

1. 这个 Layer 是谁创建的
2. 它属于哪个窗口或动画 leash
3. 它的 parent 是谁
4. 它何时被 reparent
5. 它当前是否有 buffer
6. 它的 alpha / hidden / crop / transform 是什么
7. 它的 Z 序是否正确
8. 它是否参与当前 display 的输出
9. 它是否被父层或兄弟层遮挡
10. 事务是否已应用到 drawing/composition
11. 问题发生前后 LayerTree 的差异是什么
12. 根因究竟是在 WMS、SurfaceControl、SF、Buffer 还是 HWC 输出阶段

------


<!-- source: 41-116.md -->

# 11.6 显示输出类

### 模式 14：Layer 在树中，但不在目标 display 输出里

现象：

- 某个屏幕看不到，另一个屏幕正常

排查：

- layerStack
- display mapping
- virtual display / mirror display

------

### 模式 15：截图路径能看到，物理屏看不到

现象：

- screenshot 正常
- 实机显示异常

排查：

- screenshot composition path
- physical output composition path
- HWC / secure / overlay 限制

------


<!-- source: 44-12.md -->

# 12. 标准输出模板
