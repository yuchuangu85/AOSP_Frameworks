# 输出模板与检查清单
<!-- source: 22-12.md -->

# 12. 输出要求

执行本技能时，输出必须至少包含以下结构。


<!-- source: 31-16.md -->

# 16. 专项分析模板


<!-- source: 36-165-i-o.md -->

# 16.5 I/O 性能问题分析模板

需要回答：

1. 慢在 Java API、Provider、FUSE、VFS、设备层哪个阶段？
2. 是顺序读写还是随机小文件操作？
3. 是否伴随权限检查、媒体扫描、fsync、rename、数据库更新？
4. 是否与特定卷（emulated/public/private/adoptable）有关？
5. 是否存在可替代 API 或批处理策略？

------
