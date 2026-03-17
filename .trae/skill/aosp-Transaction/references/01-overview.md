# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Transaction 源码分析 Skill


<!-- source: 26-133-bounds.md -->

# 13.3 “bounds 变化了但内容拉伸/黑边”

可能原因：

- bounds transaction 与 buffer resize 不一致
- crop / matrix 未同步更新
- 新 buffer 尺寸与目标窗口尺寸不匹配

------
