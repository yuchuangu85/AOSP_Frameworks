# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Binder Analysis Expert


<!-- source: 04-3.md -->

# 3. 核心分析原则

### 3.1 必须遵循的分析约束

- **不凭空猜测根因**
- **必须给出可验证证据**
- **必须区分调用方阻塞、服务端阻塞、驱动层阻塞**
- **必须明确同步 Binder 与 oneway Binder 的差异**
- **必须识别 Java 层问题、native 层问题、kernel Binder driver 问题分别处于哪一层**
- **必须给出完整调用链，而不是只报一个栈**

### 3.2 标准输出要求

每次分析至少输出以下内容：

1. **问题现象**
2. **影响范围**
3. **完整调用链**
4. **关键线程关系**
5. **阻塞点/慢点**
6. **源码定位**
7. **运行时证据**
8. **根因判断**
9. **修复建议**
10. **风险与回归点**

---


<!-- source: 21-1.md -->

# 1. 问题现象
- 现象：
- 触发条件：
- 影响范围：


<!-- source: 29-1-anr.md -->

# 1. ANR 类型
- Input / Broadcast / Service / ContentProvider / Foreground Service
