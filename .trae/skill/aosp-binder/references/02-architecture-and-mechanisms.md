# 架构与核心机制
<!-- source: 05-4-binder.md -->

# 4. Binder 架构总览

---

### 4.1 Android Binder 分层架构

```text
App / System Service
    ↓
Java Binder API
    - IBinder
    - Binder
    - BinderProxy
    - Parcel
    - ServiceManager
    ↓ JNI
android_util_Binder.cpp
    ↓
Native Binder
    - IBinder
    - BBinder
    - BpBinder
    - Parcel
    - IPCThreadState
    - ProcessState
    ↓
Binder Driver
    - binder_ioctl
    - binder_thread_write/read
    - binder_transaction
    - binder_proc / binder_thread / binder_node / binder_ref
    ↓
Target Process Binder Thread
    ↓
Service Stub / onTransact()
    ↓
Real Service Implementation
```

### 4.2 Binder 核心角色

#### Client 侧

- `BinderProxy` / `BpBinder`
- 发起 `transact()`
- 将参数序列化到 `Parcel`
- 等待同步结果或直接返回（oneway）

#### Driver 层

- 负责进程间事务转发
- 管理 Binder 对象引用关系
- 管理线程唤醒与事务队列
- 管理 buffer 映射、对象引用、死亡通知

#### Server 侧

- `Binder` / `BBinder`
- `onTransact()` 解析 `Parcel`
- 调用真正服务实现
- 将返回值写回 reply

#### Binder 线程池

- 每个进程 Binder 线程池负责处理入站事务
- Java 服务端通常在 Binder 线程执行 `onTransact`
- system_server 中许多系统服务共享 Binder 线程资源

------


<!-- source: 06-5-binder.md -->

# 5. Binder 设计思想

### 5.1 为什么 Android 选择 Binder

Binder 的设计目标不是“通用 IPC 最快”，而是：

- **系统服务模型友好**
- **对象语义明确**
- **引用管理安全**
- **权限校验容易接入**
- **适合 request/reply 模式**
- **可做 UID/PID 身份传播**
- **适合系统级服务治理**

因此 Binder 的价值在于：

- 能让 “调用系统服务” 看起来像“本地对象调用”
- 让权限、进程身份、生命周期、死亡通知成为系统级一等能力
- 让 Framework 能稳定构建 AMS/WMS/PMS/SurfaceFlinger 等核心服务体系

### 5.2 Binder 的核心机制设计

1. **对象引用语义**
   - 不是简单字节流，而是支持 Binder object
   - 支持跨进程对象句柄传递
2. **调用身份传播**
   - 服务端可获取调用方 UID/PID
   - 便于权限检查和审计
3. **线程池处理模型**
   - 服务端线程池按需处理
   - 避免为每个调用单独建线程
4. **零拷贝并不绝对**
   - Binder 常被误解为“完全零拷贝”
   - 实际关注点更偏向“受控共享映射 + 对象管理 + 系统服务通信效率”
5. **同步/异步混合模型**
   - 普通事务：同步 request/reply
   - `oneway`：异步入队，客户端不等结果

------


<!-- source: 08-7-binder.md -->

# 7. Binder 关键源码索引

------

### 7.1 Java Framework 层

#### 核心类

- `frameworks/base/core/java/android/os/IBinder.java`
- `frameworks/base/core/java/android/os/Binder.java`
- `frameworks/base/core/java/android/os/BinderProxy.java`
- `frameworks/base/core/java/android/os/Parcel.java`
- `frameworks/base/core/java/android/os/ServiceManager.java`

#### 关注点

- `transact()`
- `onTransact()`
- `execTransactInternal()`
- `clearCallingIdentity() / restoreCallingIdentity()`
- `getCallingUid() / getCallingPid()`

------

### 7.2 JNI 层

- `frameworks/base/core/jni/android_util_Binder.cpp`

#### 关注点

- Java 与 native Binder 的桥接
- `android_os_BinderProxy_transact`
- `android_os_Binder_execTransact`
- Java object 与 native object 的关联方式

------

### 7.3 Native Binder 层

- `frameworks/native/libs/binder/IBinder.cpp`
- `frameworks/native/libs/binder/Binder.cpp`
- `frameworks/native/libs/binder/BpBinder.cpp`
- `frameworks/native/libs/binder/Parcel.cpp`
- `frameworks/native/libs/binder/IPCThreadState.cpp`
- `frameworks/native/libs/binder/ProcessState.cpp`
- `frameworks/native/libs/binder/IServiceManager.cpp`

#### 重点类

- `ProcessState`
- `IPCThreadState`
- `BpBinder`
- `BBinder`
- `Parcel`

#### 重点函数

- `ProcessState::self()`
- `ProcessState::startThreadPool()`
- `IPCThreadState::transact()`
- `IPCThreadState::talkWithDriver()`
- `IPCThreadState::joinThreadPool()`
- `BpBinder::transact()`
- `BBinder::transact()`

------

### 7.4 Kernel Binder Driver 层

通常位于：

- `drivers/android/binder.c`
- 新内核中可能拆分为多个 binder 相关文件

#### 核心结构体

- `binder_proc`
- `binder_thread`
- `binder_node`
- `binder_ref`
- `binder_transaction`
- `binder_buffer`

#### 核心函数

- `binder_ioctl`
- `binder_ioctl_write_read`
- `binder_thread_write`
- `binder_thread_read`
- `binder_transaction`
- `binder_alloc_new_buf`
- `binder_enqueue_thread_work`
- `binder_wakeup_thread_ilocked`

------


<!-- source: 14-13-servicemanager.md -->

# 13. ServiceManager 分析体系

------

### 13.1 注册流程重点

- 服务何时注册
- 注册是否成功
- 是否被重复注册或覆盖
- lazy service 是否按需拉起
- 获取服务是否可能阻塞

### 13.2 获取服务慢的典型原因

1. 服务还没启动
2. servicemanager 本身繁忙
3. 服务进程初始化慢
4. lazy service 拉起耗时
5. 获取到服务后首次 transact 初始化慢

### 13.3 分析关注点

- `addService`
- `getService`
- `checkService`
- `waitForService`
- 启动期是否出现串行依赖链

------


<!-- source: 19-18-binder.md -->

# 18. Binder 性能优化策略库

------

### 18.1 接口设计优化

- 合并多个小 IPC
- 减少 getter 式跨进程频繁访问
- 改轮询为订阅
- 区分热路径接口和冷路径接口
- 将通知类改为异步
- 将大对象改为句柄/ID/FD/shared memory

### 18.2 服务端执行优化

- `onTransact()` 只做轻量工作
- 重活切到工作线程
- 避免 Binder 线程做磁盘 IO/网络 IO
- 避免长时间持锁
- 避免链式同步 Binder
- 为关键任务设置独立执行模型

### 18.3 线程与调度优化

- 调整 Binder 线程池规模
- 分离关键服务与非关键服务负载
- 避免后台任务抢占关键 Binder 线程
- 检查线程优先级继承是否合理
- 避免所有请求都汇聚到单点服务

### 18.4 启动性能优化

- 启动阶段延迟非关键服务调用
- 缓存早期查询结果
- 减少串行 `getService()` / `waitForService()`
- 首帧前避免不必要同步 Binder

------


<!-- source: 23-3.md -->

# 3. 完整调用链
- Client：
- Proxy：
- Driver：
- Server：
- Real Service：
- 下游依赖：


<!-- source: 26-6.md -->

# 6. 根因分析
- 直接根因：
- 深层根因：
- 架构性根因：


<!-- source: 32-4.md -->

# 4. 阻塞链
- App/Main
  → system_server/service A
  → service B / IO / lock / callback


<!-- source: 41-21.md -->

# 21. 禁止事项

- 禁止只贴源码不解释设计意图
- 禁止只看一个线程栈就下结论
- 禁止把所有 IPC 问题都归结为 Binder driver
- 禁止忽略锁、调度、IO、下游服务依赖
- 禁止把 oneway 简化理解成“没有问题”
- 禁止把 transaction 慢简单等同于 driver 慢
- 禁止没有证据就判断线程池不足
- 禁止脱离具体服务上下文分析 Binder

------
