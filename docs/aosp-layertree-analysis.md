# LayerTree 源码分析

## 一、问题定义与范围
- 范围：Layer 与 LayerHierarchy 入口完整，可分析 layer 树组织、遍历与可见性。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- SurfaceControl transaction -> Layer state -> hierarchy traversal -> composition output
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- LayerTree 通过树形结构把可见性、裁剪、Z 序、父子关系统一表达，便于一次性合成决策。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Transaction --> Layer
    Layer --> Hierarchy
    Hierarchy --> Composer
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Transaction
    participant Layer
    participant Hierarchy
    participant Composer
    Transaction->>Layer: 进入主链
    Layer->>Hierarchy: 状态推进
    Hierarchy->>Composer: 结果提交
```

## 六、关键代码详细分析
- Layer 保存单层可见性、buffer、几何属性等状态。
- LayerHierarchy 负责从事务后的节点关系构建遍历视图。
- 显示错乱要优先检查父子关系、reparent、隐藏标志与裁剪链。

## 七、证据链（源码 + 运行时）
- 源码证据：`native/services/surfaceflinger/Layer.cpp`
- 源码证据：`native/services/surfaceflinger/FrontEnd/LayerHierarchy.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-layertree` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
