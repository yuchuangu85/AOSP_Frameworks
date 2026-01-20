# VSYNC原理及申请返回刷新流程分析

## 1. VSYNC基本概念与作用

VSYNC（垂直同步）是Android图形系统的核心机制，用于同步渲染、合成和显示过程，防止屏幕撕裂。Android系统中有三种主要的VSYNC信号：

- **HW-VSYNC**：由硬件产生的VSYNC信号，频率与显示器刷新率一致
- **VSYNC-app**：应用渲染使用的软件VSYNC信号，与HW-VSYNC同频率但有相位差
- **VSYNC-sf**：SurfaceFlinger合成使用的软件VSYNC信号，与HW-VSYNC同频率但有相位差

软件VSYNC信号需要先申请后使用，当信号到来时会触发回调函数执行渲染或合成工作。

## 2. HW-VSYNC计算模型的建立与校准

Android系统通过线性回归建立HW-VSYNC的计算模型，具体步骤如下：

1. **数据采集**：SurfaceFlinger启动或屏幕刷新率改变时，收集至少6个HW-VSYNC信号的到达时间
2. **模型建立**：以信号到达次序为X轴，实际到达时间为Y轴，通过线性回归计算出线性关系`y = ax + b`
3. **模型使用**：给定任意时间，代入模型可预测下一个HW-VSYNC信号的到达时间
4. **校准优化**：计算结果会减去半个周期后重新计算，确保准确性

这个模型使得系统能够在不持续监听硬件中断的情况下，准确预测VSYNC信号的到达时间，降低功耗。

## 3. 软件VSYNC信号的计算过程

VSYNC-app和VSYNC-sf都是基于HW-VSYNC计算模型生成的软件信号，它们与HW-VSYNC保持同频率，但具有特定的相位差。计算过程涉及两个关键参数：

- **workDuration**：模块完成自身工作的理论耗时（如app的渲染耗时）
- **readyDuration**：工作完成后传递给下一模块的等待时间

### VSYNC-app信号计算示例

假设：
- app的workDuration = 16.6ms
- app的readyDuration = 15.6ms
- 应用在24.9ms时请求VSYNC-app信号

计算流程：
1. 在请求时间(24.9ms)基础上加上workDuration和readyDuration，得到时间点a：`24.9ms + 16.6ms + 15.6ms = 57.1ms`
2. 找到a之后的下一个HW-VSYNC时间点b：假设为81ms
3. 从b减去workDuration和readyDuration，得到VSYNC-app信号时间：`81ms - 16.6ms - 15.6ms = 48.8ms`
4. 设置定时器，在48.8ms时向应用发送VSYNC-app信号

## 4. 应用层VSYNC请求流程

当View需要刷新时，整个VSYNC请求流程如下：

### 4.1 ViewRootImpl触发刷新请求

```java
// ViewRootImpl.java
void scheduleTraversals() {
    if (!mTraversalScheduled) {
        mTraversalScheduled = true;
        mTraversalBarrier = mQueue.postSyncBarrier();
        mChoreographer.postCallback(Choreographer.CALLBACK_TRAVERSAL, mTraversalRunnable, null);
    }
}
```

该方法通过以下步骤触发刷新：
1. 设置`mTraversalScheduled`标志防止重复请求
2. 在消息队列中添加同步屏障，确保绘制任务优先执行
3. 通过Choreographer注册绘制回调

### 4.2 Choreographer请求VSYNC信号

```java
// Choreographer.java
private void scheduleFrameLocked(long now) {
    if (USE_VSYNC) {
        if (isRunningOnLooperThreadLocked()) {
            scheduleVsyncLocked();
        } else {
            // 发送消息到当前线程请求VSYNC
        }
    }
}

private void scheduleVsyncLocked() {
    mDisplayEventReceiver.scheduleVsync();
}
```

Choreographer通过`DisplayEventReceiver`向SurfaceFlinger请求VSYNC信号。

### 4.3 DisplayEventReceiver与Native层交互

```java
// DisplayEventReceiver.java
public void scheduleVsync() {
    if (mReceiverPtr != 0) {
        nativeScheduleVsync(mReceiverPtr);
    }
}
```

`scheduleVsync()`方法通过JNI调用Native层的`nativeScheduleVsync`，最终向SurfaceFlinger的EventThread发送VSYNC请求。

## 5. SurfaceFlinger中VSYNC信号的处理与分发

### 5.1 EventThread管理VSYNC连接与请求

```cpp
// EventThread.cpp
binder::Status EventThreadConnection::requestNextVsync() {
    SFTRACE_CALL();
    mEventThread->requestNextVsync(sp<EventThreadConnection>::fromExisting(this));
    return binder::Status::ok();
}

void EventThread::requestNextVsync(const sp<EventThreadConnection>& connection) {
    mCallback.resync(IEventThreadCallback::ResyncCaller::RequestNextVsync);

    std::lock_guard<std::mutex> lock(mMutex);

    if (connection->vsyncRequest == VSyncRequest::None) {
        connection->vsyncRequest = VSyncRequest::Single;
        mCondition.notify_all();
    } else if (connection->vsyncRequest == VSyncRequest::SingleSuppressCallback) {
        connection->vsyncRequest = VSyncRequest::Single;
    }
}
```

EventThread负责管理应用的VSYNC连接和请求，当收到请求时会更新连接的VSYNC请求状态。

### 5.2 VSYNC信号的调度与生成

```cpp
// EventThread.cpp
if (mState == State::VSync) {
    const auto scheduleResult = mVsyncRegistration.schedule(
            {.workDuration = mWorkDuration.get().count(),
             .readyDuration = mReadyDuration.count(),
             .lastVsync = mLastVsyncCallbackTime.ns(),
             .committedVsyncOpt = mLastCommittedVsyncTime.ns()});
    LOG_ALWAYS_FATAL_IF(!scheduleResult, "Error scheduling callback");
}
```

EventThread根据workDuration和readyDuration参数，通过VSyncRegistration调度VSYNC信号的生成。

### 5.3 VSYNC信号的分发

```cpp
// EventThread.cpp
void EventThread::threadMain(std::unique_lock<std::mutex>& lock) {
    DisplayEventConsumers consumers;

    while (mState != State::Quit) {
        // ... 处理pending events ...
        
        // 查找需要消费事件的连接
        auto it = mDisplayEventConnections.begin();
        while (it != mDisplayEventConnections.end()) {
            if (const auto connection = it->promote()) {
                if (event && shouldConsumeEvent(*event, connection)) {
                    consumers.push_back(connection);
                }
                // ...
            }
        }
        
        // 分发事件
        if (!consumers.empty()) {
            dispatchEvent(*event, consumers);
            consumers.clear();
        }
        
        // ... 调度下一个VSYNC信号 ...
    }
}
```

EventThread的主循环负责分发VSYNC事件给注册的连接，确保只有请求了VSYNC的应用才能收到信号。

## 6. VSYNC信号的时序分析

### 6.1 相位差设计

软件VSYNC信号与HW-VSYNC保持特定相位差，确保渲染和合成流程能够及时完成：

- VSYNC-app比HW-VSYNC提前(workDuration + readyDuration)时间触发
- VSYNC-sf在VSYNC-app之后、HW-VSYNC之前触发

这种相位差设计使得应用有足够时间渲染，SurfaceFlinger有足够时间合成，最终在HW-VSYNC到来时完成显示。

### 6.2 帧调度时序

以120Hz刷新率为例，完整的帧调度时序如下：

1. **t0**：VSYNC-app信号触发应用渲染
2. **t0+workDuration**：应用渲染完成，提交buffer给SurfaceFlinger
3. **t0+workDuration+readyDuration**：VSYNC-sf信号触发SurfaceFlinger合成
4. **t1**：HW-VSYNC信号到来，显示合成后的帧（t1 = t0 + 8.33ms，即120Hz的周期）

## 7. VSYNC申请与返回完整流程图

```
┌───────────────────┐     ┌───────────────────┐     ┌───────────────────┐
│   ViewRootImpl    │     │   Choreographer   │     │DisplayEventReceiver│
└───────────────────┘     └───────────────────┘     └───────────────────┘
         │                          │                          │
         │ scheduleTraversals()     │                          │
         │─────────────────────────>│                          │
         │                          │                          │
         │                          │ postCallback()          │
         │                          │                          │
         │                          │ scheduleVsyncLocked()    │
         │                          │─────────────────────────>│
         │                          │                          │ scheduleVsync()
         │                          │                          │
         │                          │                          │<───────────────────┐
         │                          │                          │                   │
         │                          │                          │                   │
         │                          │                          │                   │
┌───────────────────┐     ┌───────────────────┐     ┌───────────────────┐     ┌───────────────────┐
│   EventThread     │     │  VSyncSchedule    │     │  VSyncRegistration│     │   HW-VSYNC Model  │
└───────────────────┘     └───────────────────┘     └───────────────────┘     └───────────────────┘
         │                          │                          │                   │
         │ requestNextVsync()       │                          │                   │
         │<─────────────────────────┘                          │                   │
         │                          │                          │                   │
         │ resync()                 │                          │                   │
         │─────────────────────────>│                          │                   │
         │                          │                          │                   │
         │                          │ calculate next vsync time│                   │
         │                          │─────────────────────────>│───────────────────>│
         │                          │                          │                   │
         │                          │                          │                   │
         │                          │     vsync time           │                   │
         │                          │<─────────────────────────│<───────────────────┘
         │                          │                          │
         │ schedule callback        │                          │
         │<─────────────────────────┘                          │
         │                          │                          │
         │ onVsync()                │                          │
         │────────────────────────────────────────────────────>│
         │                          │                          │
         │ postEvent()              │                          │
         │────────────────────────────────────────────────────>│
         │                          │                          │
         │                          │ nativeVSync()           │
         │                          │─────────────────────────>│
         │                          │                          │
         │                          │ doFrame()                │
         │                          │<─────────────────────────┘
         │                          │                          │
         │                          │ traverse()               │
         │                          │─────────────────────────>│
└───────────────────┘     └───────────────────┘     └───────────────────┘
```

## 8. 代码优化建议

1. **减少VSYNC请求频率**：避免不必要的`requestLayout()`和`invalidate()`调用，减少VSYNC请求次数

2. **合理设置帧率**：对于不需要高帧率的应用，使用`FrameRateRange`限制最大帧率，降低功耗

3. **优化渲染耗时**：确保应用渲染时间不超过workDuration，避免丢帧

4. **使用SurfaceControl.Transaction**：对于批量UI更新，使用`SurfaceControl.Transaction`减少VSYNC信号的触发次数

## 9. 总结

VSYNC机制是Android图形系统的核心，通过HW-VSYNC的计算模型和软件VSYNC的相位差设计，确保了渲染、合成和显示的高效同步。理解VSYNC的工作原理对于优化应用性能、避免卡顿和提高用户体验至关重要。

应用层通过Choreographer请求VSYNC信号，SurfaceFlinger根据HW-VSYNC模型计算软件VSYNC信号的时间点，并通过EventThread分发给应用，最终触发渲染或合成工作。这种分层设计既保证了同步精度，又降低了系统功耗。