# 工具与证据
<!-- source: 03-41.md -->

# 4.1 可接受输入

### 源码类
- AOSP 源码目录
- 具体文件 / 类 / 方法
- 指定 Android 版本
- 具体模块路径

### 运行时证据类
- Perfetto trace
- systrace
- bugreport
- `dumpsys SurfaceFlinger`
- `dumpsys gfxinfo`
- `dumpsys window`
- `dumpsys activity top`
- `dumpsys display`
- `logcat`
- `binder_calls_stats`
- `SurfaceFlinger --latency`
- framestats
- winscope 导出
- vendor / kernel 显示日志

### 现象描述类
- 现象
- 触发路径
- 复现步骤
- 机型 / 平台 / SoC
- Android 版本
- 刷新率
- 是否分屏 / 旋转 / 截图 / 录屏 / 高刷 / 外接屏

---

# 5. 输出要求

输出必须尽量包含以下结构：

1. 问题定义
2. 影响范围
3. 所属层次判断
4. 完整跨层调用链
5. 关键源码文件与方法
6. 核心时序分析
7. 关键证据
8. 根因归纳
9. 修复建议
10. 验证方案
11. 置信度评级

---

# 6. 分析总原则


<!-- source: 04-61.md -->

# 6.1 证据优先
优先使用以下证据进行结论推导：

- 源码
- Perfetto / trace
- dumpsys
- logcat
- bugreport
- kernel / vendor logs

禁止无证据拍脑袋归因。


<!-- source: 43-127-bufferqueue-blast.md -->

# 12.7 BufferQueue / BLAST 分析强制问题

必须回答：

1. Producer 是谁，Consumer 是谁
2. dequeue 是否阻塞
3. queue 后是否及时被 acquire
4. acquire 后是否及时 release
5. 是否存在 release fence 拖后
6. BLAST transaction 是否与 buffer 同步
7. layer 几何状态与 buffer 内容是否一致
8. 是否是 resize / rotation / transition 特殊路径

------

# 13. Perfetto + dumpsys SurfaceFlinger 自动分析规则

本节定义自动化分析流程。所有图形 trace 分析，都必须优先按此规则执行。

------


<!-- source: 47-134-perfetto-surfaceflinger.md -->

# 13.4 Perfetto + SurfaceFlinger 联合判定规则

### 规则 A：App Miss + dequeueBuffer wait

结论倾向：

- BufferQueue 背压
- consumer / display 慢导致 producer 被阻塞

### 规则 B：App 正常 + SF Miss + present 慢

结论倾向：

- HWC / DRM / present 路径异常

### 规则 C：App 正常 + SF 正常 + 实际显示仍晚

结论倾向：

- present fence / panel / mode switch / driver 问题

### 规则 D：连续帧都发生 queue→latch 延迟

结论倾向：

- SF 消费不及时
- transaction 过重
- composition 路径拥塞

### 规则 E：首帧 queue 成功但 layer 不显示

结论倾向：

- BLAST / transaction 同步问题
- layer 可见性 / 父子层级 / alpha / crop 异常

------

# 14. 图形异常模式库（80+）

以下模式用于快速归因。实际分析时，必须把现象映射到模式，再结合源码与证据做确认。

------


<!-- source: 75-s.md -->

# S 级

- Perfetto / dumpsys / 源码强一致直接证据


<!-- source: 76-a.md -->

# A 级

- 多源证据一致，归因高度可信
