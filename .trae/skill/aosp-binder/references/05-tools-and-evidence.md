# 工具与证据
<!-- source: 03-2.md -->

# 2. 适用场景

当用户出现以下需求时调用本 Skill：

1. **分析 Binder 基础原理**
   - Binder 是什么
   - 为什么 Android 选择 Binder
   - Binder 和 socket / pipe / ashmem / hwbinder 的关系
   - Binder 的线程模型、内存模型、对象模型

2. **分析源码调用链**
   - `BpBinder` / `BBinder` / `BinderProxy` / `Binder`
   - `transact()` 到驱动的完整路径
   - ServiceManager / SystemService 注册与查询流程
   - AIDL 接口调用链
   - Java Binder 和 native Binder 对应关系

3. **分析系统问题**
   - App 卡在 Binder 调用
   - `system_server` Binder 线程耗尽
   - Binder 死锁 / 长事务 / 大对象传输
   - input / window / activity / package / power 等系统服务调用慢
   - Binder 导致 ANR、掉帧、启动慢

4. **分析 trace 与日志**
   - Perfetto / systrace Binder slice
   - `dumpsys binder`
   - `/sys/kernel/debug/binder/*`
   - ANR traces 中 Binder block 栈
   - kernel log 中 binder warning / transaction failed

5. **做架构评估与优化**
   - 服务边界是否合理
   - IPC 频度是否过高
   - 接口设计是否导致大对象搬运
   - oneway 是否被误用
   - 是否需要缓存、批处理、异步队列或共享内存

---


<!-- source: 16-15-dumpsys-debugfs.md -->

# 15. dumpsys / debugfs / 日志联合分析规则

------

### 15.1 常用命令

```
adb shell dumpsys binder
adb shell dumpsys activity
adb shell dumpsys window
adb shell dumpsys package
adb shell cat /sys/kernel/debug/binder/state
adb shell cat /sys/kernel/debug/binder/stats
adb shell cat /sys/kernel/debug/binder/proc/<pid>
adb shell logcat -b system -b main -b events
adb shell dmesg | grep -i binder
```

> 实际路径可能因内核版本、权限、设备配置不同而变化，部分设备会挂载在 binderfs 或限制 debugfs 访问。

------

### 15.2 `binder state` 分析关注点

1. 哪些进程事务最多
2. 哪些线程 transaction stack 很深
3. 哪些 async transaction 堆积
4. 是否存在目标线程长期无空闲
5. 某 node 是否被异常高频访问
6. 某 proc 是否 pending work 堆积

------

### 15.3 ANR traces 联合判断

看到如下栈要高度怀疑 Binder：

- `android.os.BinderProxy.transact`
- `android.os.BinderProxy.transactNative`
- `android.os.Parcel.readException`
- `android.os.Parcel.readExceptionOrNull`
- AIDL Proxy 方法
- native 栈中的 `ioctl`
- `talkWithDriver`
- `IPCThreadState::transact`

然后继续找：

- 对端服务线程栈
- system_server 内部锁
- 下游等待
- IO / fsync / content provider / package scan 等慢路径

------


<!-- source: 17-16.md -->

# 16. 源码分析标准流程

------

### 16.1 第一阶段：界定问题边界

先回答：

1. 谁发起 Binder 调用
2. 谁是服务端
3. 是同步还是 oneway
4. 调用发生在哪个线程
5. 该线程是否关键线程（主线程/UI/Binder 池）
6. 慢在请求发送、排队、服务处理、回复返回哪一段

------

### 16.2 第二阶段：还原调用链

必须还原：

```
调用入口
→ AIDL/接口定义
→ Proxy.transact
→ native transact
→ driver
→ server onTransact
→ real service
→ 下游依赖
→ reply
```

------

### 16.3 第三阶段：证据闭环

至少结合其中三类证据：

- 源码
- traces / Perfetto
- ANR traces
- binder state
- dumpsys
- logcat
- kernel binder 日志

------

### 16.4 第四阶段：根因分类

将问题归类为：

- 接口设计问题
- 服务端慢逻辑问题
- 锁竞争问题
- 线程池不足问题
- 嵌套 Binder 问题
- 大对象传输问题
- oneway 队列积压问题
- 调度/优先级问题
- driver 层资源问题

------

### 16.5 第五阶段：优化建议

输出必须包含：

- 快速止血方案
- 中期结构优化
- 长期架构治理建议

------


<!-- source: 25-5.md -->

# 5. 证据
- 源码：
- trace：
- dumpsys：
- binder state：
- logcat / kernel log：
