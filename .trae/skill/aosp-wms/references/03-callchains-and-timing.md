# 调用链与时序
<!-- source: 12-sequence-diagram-requirements.md -->

# Sequence Diagram Requirements

分析结果必须包含至少一张 Mermaid 时序图。

### 必须覆盖内容
- 正常时序
- 异常时序
- 首发异常点
- 状态切换点
- 用户感知点

### 建议覆盖对象
- User
- App / ActivityThread / ViewRootImpl
- WMS
- Shell / TransitionController
- Insets / IME
- Input
- SurfaceFlinger

### 必须标注
- addWindow / relayout / focus update / insets dispatch / transition start / draw / commit
- 同步调用
- 异步回调
- 状态更新
- 事务提交点
- 超时/卡住点（如相关）

---
