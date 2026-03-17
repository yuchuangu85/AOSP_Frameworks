# 问题模式与根因
<!-- source: 02-1.md -->

# 1. 目标与定位

本 Skill 用于对 Android AOSP 中 **Binder 通信机制**进行系统级、工程化、可验证的分析，强调：

- **跨层源码分析**：Java Framework → JNI → libbinder → kernel binder driver
- **跨进程调用链还原**：Client → Binder Proxy → Driver → Server Stub → Service
- **运行时证据结合**：logcat / traces.txt / binder state / Perfetto / systrace / dumpsys
- **异常根因定位**：Binder 阻塞、线程池打满、oneway 堆积、服务端慢调用、system_server 饥饿、Binder ANR、死锁
- **优化策略输出**：线程模型、接口粒度、序列化成本、锁设计、异步化改造、缓存与限流

---


<!-- source: 10-9-binder-oneway-binder.md -->

# 9. 同步 Binder / oneway Binder 模型

------

### 9.1 同步 Binder

特征：

- Client 发起调用后阻塞等待 reply
- 最容易在 trace 和 ANR 栈中体现为等待 Binder 返回
- 如果服务端慢，调用方会直接感知卡顿

适用：

- 需要立即拿结果
- 查询型接口
- 状态强一致依赖

风险：

- 容易形成链式阻塞
- 容易造成主线程卡死
- 服务端慢时直接放大 ANR 风险

------

### 9.2 oneway Binder

特征：

- Client 不等待结果
- 请求进入服务端异步队列
- 从客户端看很快返回

适用：

- 通知型接口
- 非关键路径上报
- 状态广播/事件通知

风险：

- 易被误以为“不会卡”
- 实际会造成：
  - 服务端异步队列膨胀
  - Binder 线程长期繁忙
  - 内存积压
  - 顺序依赖错乱
  - 调试困难

### 9.3 误区

- `oneway != 完全无成本`
- `异步 != 不会阻塞系统`
- `oneway` 大量滥用可能把问题从调用方转移到服务端

------


<!-- source: 11-10-binder-anr.md -->

# 10. Binder ANR 深度模型

------

### 10.1 App ANR 中的 Binder 型根因

典型表现：

- 主线程卡在某系统服务 Binder 调用
- Binder reply 长时间不返回
- 目标服务线程池忙或锁等待
- 下游链路再阻塞其他服务

常见场景：

- `ActivityManager`
- `WindowManager`
- `PackageManager`
- `InputMethodManager`
- `ContentProvider`
- `SurfaceFlinger`
- `media` / `audio` / `location` / `telephony`

------

### 10.2 Binder 型 ANR 判定步骤

1. 看主线程栈是否阻塞在：
   - `BinderProxy.transact`
   - `android.os.BinderProxy.transactNative`
   - `Parcel.readException`
   - AIDL Proxy 方法
2. 看目标服务端线程是否：
   - 在执行长事务
   - 在等待锁
   - 在等待 IO
   - 在等待下游 Binder
3. 看 Binder 线程池是否已满
4. 看是否存在循环等待
5. 看 traces / binder state / Perfetto 是否互相印证

------

### 10.3 典型死锁模型

#### 模型 A：双向 Binder 死锁

```
Thread A in Process P1
  持有 Lock L1
  → Binder call to P2

Thread B in Process P2
  持有 Lock L2
  → Binder callback / Binder call to P1

P1 需要 L1/L2
P2 需要 L1/L2
形成死锁
```

#### 模型 B：主线程-Binder 线程互等

```
App Main Thread
  → Binder call to system_server

system_server Binder Thread
  → post to main thread / handler and wait

App Main Thread blocked
system_server waiting callback/result
形成 ANR
```

#### 模型 C：system_server 内部锁 + 下游 Binder

```
system_server service A
  持有全局锁
  → Binder call service B / native service

service B / callback
  需要反向获取同一锁或依赖被锁保护状态
导致长时间阻塞
```

------


<!-- source: 28-8.md -->

# 8. 风险与回归验证
- 风险点：
- 验证项：
```

------

### 19.2 Binder ANR 专项模板

```
# Binder ANR 专项分析


<!-- source: 33-5.md -->

# 5. 根因
- 服务端慢执行 / 锁竞争 / 线程池打满 / 死锁 / 大对象 / 调度问题


<!-- source: 35-1.md -->

# 1. 性能症状
- 掉帧 / 启动慢 / 卡顿 / CPU 高 / system_server 忙
