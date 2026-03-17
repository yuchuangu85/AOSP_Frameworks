# 调用链与时序
<!-- source: 07-6.md -->

# 6. 完整跨层调用链

------

### 6.1 Java AIDL 调用链

```
Client Java
  ↓
AIDL generated Proxy.method()
  ↓
Parcel.obtain()
  ↓
BinderProxy.transact()
  ↓
android_os_BinderProxy_transact() JNI
  ↓
BpBinder::transact()
  ↓
IPCThreadState::transact()
  ↓
talkWithDriver()
  ↓
binder_ioctl(BINDER_WRITE_READ)
  ↓
binder_transaction()
  ↓
target proc/thread enqueue
  ↓
binder thread wakeup
  ↓
IPCThreadState::executeCommand()
  ↓
BBinder::transact()
  ↓
Binder.onTransact()
  ↓
AIDL generated Stub.onTransact()
  ↓
real service method()
  ↓
reply Parcel
  ↓
driver return
  ↓
client thread wakeup
  ↓
readException()/readResult()
```

------

### 6.2 SystemService 注册调用链

```
SystemServer.startOtherServices()
  ↓
new XxxService(context)
  ↓
ServiceManager.addService(name, service)
  ↓
BinderInternal / ServiceManagerProxy
  ↓
BpBinder transact to servicemanager
  ↓
native ServiceManager
  ↓
binder_context_mgr_node 管理服务表
  ↓
其他进程通过 getService/checkService 获取句柄
```

------

### 6.3 native Binder 调用链

```
client
  ↓
sp<IInterface> service = interface_cast<IInterface>(binder)
  ↓
BpInterface::method()
  ↓
BpBinder::transact()
  ↓
IPCThreadState::transact()
  ↓
binder driver
  ↓
server binder thread
  ↓
BnInterface::onTransact()
  ↓
real native service implementation
```

------

### 6.4 system_server 典型跨服务调用链

```
App
  ↓ Binder
AMS/WMS/PMS in system_server
  ↓ service internal call or nested Binder
other native/system service
  ↓
HAL / kernel / storage / display / input
```

分析重点：

- 是不是 **App → system_server** 阻塞
- 是不是 **system_server 内部某服务处理慢**
- 是不是 **system_server 再次发起下游 Binder** 导致链式阻塞
- 是不是 **持锁进入 Binder** 导致死锁放大

------


<!-- source: 09-8-binder.md -->

# 8. Binder 线程模型深度分析

------

### 8.1 基本模型

每个 Binder 进程有：

- 一个 `ProcessState`
- 一个 Binder fd
- 一个 Binder 映射区
- 若干 Binder 线程

线程分为：

- 发起调用的普通线程
- 处理入站事务的 Binder 线程池线程

### 8.2 线程池行为

服务端进程如果没有空闲 Binder 线程：

- 新事务可能排队
- 同步调用方会阻塞等待
- 出现高延迟甚至 ANR

### 8.3 system_server 特殊性

`system_server` 是 Binder 问题高发区，因为：

- 系统服务多
- Binder 请求密集
- 很多服务共享 Binder 线程池
- 容易出现：
  - 长事务
  - 嵌套 Binder
  - 锁竞争
  - 线程池饥饿
  - 主线程与 Binder 线程相互等待

### 8.4 常见线程问题

1. **Binder thread full**
2. **Binder callback reentry**
3. **持锁进入 Binder**
4. **主线程等待 Binder，服务端又等主线程**
5. **oneway 队列积压**
6. **线程优先级不匹配导致调度延迟**

------


<!-- source: 12-11-binder.md -->

# 11. Binder 性能分析体系

------

### 11.1 关键性能维度

1. **调用频率**
2. **单次耗时**
3. **排队耗时**
4. **序列化/反序列化成本**
5. **大对象搬运成本**
6. **线程池占用**
7. **嵌套调用深度**
8. **调用线程是否主线程**
9. **CPU 调度延迟**
10. **锁竞争放大效应**

------

### 11.2 高频低耗时问题

表现：

- 单次 Binder 很快，但频繁调用导致总开销大
- 常见于 UI 刷新、状态轮询、属性读取、多次小包调用

优化方向：

- 批处理
- 缓存
- 减少跨进程 getter
- 订阅替代轮询
- 合并事务

------

### 11.3 低频高耗时问题

表现：

- 个别事务特别慢
- 常见于 PMS/AMS/WMS/存储/权限/Provider/多媒体路径

优化方向：

- 剥离重逻辑
- 避免 Binder 线程执行慢 IO
- 避免持锁做重活
- 将重活切换到工作线程
- 缩短 reply 前路径

------

### 11.4 大对象传输问题

表现：

- `Parcel` 数据过大
- transaction buffer 压力大
- transaction failed / `TransactionTooLargeException`
- 内存抖动大

优化方向：

- 减少大 Bundle / 大 List / 大 Bitmap
- 用文件描述符 / shared memory / 分页加载 / lazy fetch
- 传 ID，不传全量对象

------


<!-- source: 13-12-binder-driver.md -->

# 12. Binder Driver 分析框架

------

### 12.1 Kernel 层必须关注的问题

1. 事务有没有成功入队
2. 目标线程有没有被唤醒
3. buffer 分配是否失败
4. oneway 队列是否堆积
5. 事务有没有超大对象
6. 引用计数和 node/ref 是否异常
7. 死亡通知是否及时处理

### 12.2 常见 binder driver 异常信号

- transaction failed
- no async space left
- undelivered transaction
- binder_alloc buf failed
- thread busy / no thread available
- stale ref / invalid handle
- failed reply / target dead

### 12.3 关键诊断对象

#### `binder_proc`

- 进程级 Binder 状态
- 线程列表
- 待处理事务
- 节点和引用

#### `binder_thread`

- 当前线程事务栈
- todo 队列
- transaction_stack
- looper 状态

#### `binder_transaction`

- from/to 关系
- synchronous / async
- code / flags
- buffer 大小
- 优先级 / 调度继承信息

------


<!-- source: 15-14-perfetto-systrace.md -->

# 14. Perfetto / Systrace 联合分析规则

------

### 14.1 重点观察轨道

- App Main Thread
- Binder Thread
- system_server Binder Threads
- system_server main / handler threads
- SurfaceFlinger / Input / ActivityTaskManager / WindowManager 相关线程
- CPU scheduling
- binder transaction slices
- locks / blocking reason
- async events

### 14.2 关键分析方法

#### 方法一：从卡顿线程反查 Binder

- 找到主线程卡住区间
- 看是否阻塞在 Binder transact
- 查服务端对应处理线程
- 查该线程正在做什么

#### 方法二：从 system_server Binder 线程池反推

- 看某段时间 Binder 线程是否全忙
- 看都在执行哪些 service method
- 看是不是被少数长事务占满

#### 方法三：从嵌套调用链回溯

- client 等 server
- server 又等下游 service
- 下游又等 IO / lock / main thread
- 确认链式阻塞深度

### 14.3 Trace 中常见信号

- Binder transaction slice 长时间不结束
- Main thread sleeping on binder reply
- system_server binder thread 连续繁忙
- 某 service handler thread 无法及时消费
- CPU runnable 但迟迟未调度
- 锁等待覆盖 Binder 处理区间

------


<!-- source: 18-17-binder-60.md -->

# 17. Binder 异常模式库（60+）

------

### 17.1 调用阻塞类

1. 主线程同步调用系统服务阻塞
2. Binder reply 长时间不返回
3. 服务端 Binder 线程被锁阻塞
4. 服务端 Binder 线程执行慢 IO
5. 服务端 Binder 线程等待 Handler
6. 服务端 Binder 线程等待 Future/CountDownLatch
7. 服务端 Binder 线程调用下游 Binder 再阻塞
8. system_server Binder 线程池打满
9. app 进程 Binder 线程池耗尽
10. Binder 调用与主线程形成互等

------

### 17.2 线程池/队列类

1. Binder 线程池线程数不足
2. oneway transaction 队列爆满
3. 某服务被高频小事务打爆
4. 事务排队时间远高于执行时间
5. 单个长事务占住 Binder 线程
6. callback 风暴导致线程饥饿
7. Binder 线程优先级过低
8. RT/高优线程被普通 Binder 阻塞
9. 线程池被非关键服务占满
10. 启动期服务注册/查询雪崩

------

### 17.3 锁与死锁类

1. 持全局锁进入 Binder
2. 持对象锁进入 Binder
3. 双向 Binder 死锁
4. Binder 回调中反向获取锁
5. 主线程持锁等 Binder
6. Binder 线程持锁等主线程
7. WMS/AMS/PMS 等系统大锁与 Binder 交叉
8. Java lock 与 native mutex 交叉死锁
9. ContentProvider 锁与 Binder 交错
10. 死锁未完全卡死但形成长时间抖动

------

### 17.4 数据与对象传输类

1. `TransactionTooLargeException`
2. 大 Bundle 频繁跨进程
3. 大 List / Parcelable 序列化成本高
4. Bitmap/Image 跨 Binder 传输
5. Parcel 对象嵌套复杂过深
6. 文件描述符过多
7. 反复创建/销毁临时对象
8. 频繁小对象碎片化开销
9. 高维结构体反序列化慢
10. 不必要的全量对象返回

------

### 17.5 架构设计类

1. getter/setter 粒度过细导致 IPC 风暴
2. 轮询接口替代事件订阅
3. 同步接口承载重逻辑
4. 通知型接口未使用 oneway
5. oneway 被滥用导致异步雪崩
6. 服务边界划分错误导致层层跨进程
7. 冷启动关键路径 Binder 过多
8. 跨进程对象生命周期设计混乱
9. 死亡通知过多导致抖动
10. 不合理的 callback 设计导致重入复杂

------

### 17.6 system_server 专项类

1. AMS Binder 线程长期忙
2. WMS Binder 线程受窗口锁阻塞
3. PMS 调包/扫描慢拖累 Binder
4. Input 相关 Binder 阻塞导致输入超时
5. SurfaceFlinger/图形链路 Binder 影响帧时序
6. media service 回复慢影响前台体验
7. system_server 处理广播/Provider 时反向放大 Binder 阻塞
8. watch dog 前出现 Binder thread starvation
9. 多服务串行初始化造成启动期 Binder 堵塞
10. system_server 内部嵌套调用把卡顿扩散到全系统

------


<!-- source: 34-6.md -->

# 6. 修复建议
- 避免主线程同步 Binder
- 缩短服务端事务
- 去锁化 / 异步化 / 合并事务
```

------

### 19.3 Binder 性能专项模板

```
# Binder 性能分析


<!-- source: 36-2.md -->

# 2. 高频事务
- Top binder calls:
- 调用频率：
- 调用线程：


<!-- source: 37-3.md -->

# 3. 慢事务
- Top slow binder transactions:
- 平均耗时：
- P95/P99：


<!-- source: 40-20.md -->

# 20. 回答风格要求

使用本 Skill 时，回答必须满足：

- 优先给 **调用链**
- 优先给 **线程关系**
- 优先给 **证据闭环**
- 明确区分：
  - Java Binder
  - native Binder
  - kernel Binder driver
- 明确区分：
  - 同步事务
  - oneway 事务
- 明确判断：
  - 谁阻塞谁
  - 谁是第一慢点
  - 谁是放大器
- 不只说“Binder 卡了”，必须说清：
  - 卡在哪里
  - 为什么卡
  - 为什么会放大成系统问题

------


<!-- source: 42-22.md -->

# 22. 一句话调用提示

当你需要分析 Android Binder 的 **原理、源码、线程模型、事务流程、ANR、性能瓶颈、死锁、system_server 阻塞、trace 和 binder state 联合证据**时，调用本 Skill，输出完整跨层调用链、线程阻塞关系、源码定位、运行时证据和优化建议。
