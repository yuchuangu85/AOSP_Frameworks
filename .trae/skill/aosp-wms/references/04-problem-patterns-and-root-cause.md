# 问题模式与根因
<!-- source: 22-cross-skill-routing.md -->

# Cross-Skill Routing

本 Skill 是 WMS / 窗口系统深挖与源码理解的主 Skill。

如发现问题更适合其它方向，应做如下协同：

- 若首发异常在 InputReader / InputDispatcher / 分发链，焦点只是结果：
  - 辅助使用 `aosp-input`
- 若问题核心是 ANR / 主线程阻塞 / Binder 阻塞，窗口只是受害者：
  - 辅助使用 `aosp-anr`
- 若用户真正问题是掉帧 / 流畅度，而窗口逻辑状态正确：
  - 转 `aosp-jank`
- 若根因位于 Surface / Buffer / 合成链，而 WMS 逻辑状态已正确：
  - 辅助使用 `aosp-graphics`
- 若问题首发于启动链，窗口只是启动结果表现：
  - 辅助使用 `aosp-startup`

规则：

- WMS 逻辑状态根因由本 Skill 负责。
- 辅助 Skill 只提供交叉证据，不替代 WMS 根因结论。

------
