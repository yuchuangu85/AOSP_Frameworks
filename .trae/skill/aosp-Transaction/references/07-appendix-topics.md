# 补充专题
<!-- source: 25-132.md -->

# 13.2 “位置变化延迟一帧”

可能原因：

- 与 buffer latch 未同帧对齐
- SF 在下一次 commit 才应用
- BLAST sync 等待其他参与方
- transition 中延迟显示

------
