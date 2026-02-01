# Android触摸事件分发机制完整分析

[toc]

## 5. 系统层到应用层的事件传递流程

### 5.1 InputDispatcher到ViewRootImpl

触摸事件首先由InputDispatcher通过Binder跨进程传递到应用进程：

```mermaid
sequenceDiagram
    participant ID as InputDispatcher
    participant VRI as ViewRootImpl
    participant D as DecorView
    participant A as Activity
    participant VG as ViewGroup
    participant V as View
    
    ID->>VRI: dispatchInputEvent (Binder调用)
    VRI->>VRI: enqueueInputEvent
    VRI->>VRI: doProcessInputEvents
    VRI->>D: dispatchPointerEvent
    D->>A: dispatchTouchEvent
    A->>D: superDispatchTouchEvent
    D->>VG: dispatchTouchEvent
    VG->>V: dispatchTouchEvent
    V->>V: onTouchEvent
```

### 5.2 详细的事件拦截和TouchTarget查找流程图

```mermaid
flowchart TD
    A[开始事件拦截检查] --> B{"检查拦截条件<br/>ACTION_DOWN或mFirstTouchTarget!=null?"}
    B -->|是| C{检查FLAG_DISALLOW_INTERCEPT?}
    B -->|否| D["直接拦截，无TouchTarget<br/>intercepted=true"]
    
    C -->|否| E["调用onInterceptTouchEvent<br/>检查是否需要拦截"]
    C -->|是| F["不允许拦截<br/>intercepted=false"]
    
    E --> G{onInterceptTouchEvent返回true?}
    G -->|是| H[设置intercepted=true]
    G -->|否| I[设置intercepted=false]
    
    H --> J{"检查mFirstTouchTarget状态<br/>mFirstTouchTarget != null?"}
    J -->|是| K["发送ACTION_CANCEL给子View<br/>清理TouchTarget链表"]
    J -->|否| L[无需清理]
    
    K --> M[设置mFirstTouchTarget=null]
    L --> N[拦截检查完成]
    F --> N
    I --> N
    D --> N
    
    N --> O[开始TouchTarget查找] --> P{"检查查找条件<br/>!canceled && !intercepted?"}
    P -->|是| Q{"检查事件类型<br/>ACTION_DOWN/POINTER_DOWN/HOVER_MOVE?"}
    P -->|否| R[跳过查找，直接处理]
    
    Q -->|是| S["建立idBitsToAssign<br/>清理旧TouchTarget"]
    Q -->|否| T[跳过查找]
    
    S --> U["构建子View列表<br/>buildTouchDispatchChildList"]
    U --> V["遍历子View查找目标<br/>从后向前遍历"]
    
    V --> W{"检查子View条件<br/>canReceivePointerEvents?"}
    W -->|是| X{"检查触摸点是否在View内<br/>isTransformedTouchPointInView?"}
    W -->|否| Y[跳过该子View]
    Y --> V
    
    X -->|是| Z["检查是否已有TouchTarget<br/>getTouchTarget(child)"]
    X -->|否| AA[跳过该子View]
    AA --> V
    
    Z --> AB{已有TouchTarget?}
    AB -->|是| AC["更新pointerIdBits<br/>break循环"]
    AB -->|否| AD["分发事件给子View<br/>dispatchTransformedTouchEvent"]
    
    AD --> AE{子View消费事件?}
    AE -->|是| AF["记录触摸信息<br/>mLastTouchDownTime/Index/X/Y"]
    AE -->|否| AG[继续查找下一个子View]
    AG --> V
    
    AF --> AH["建立TouchTarget<br/>addTouchTarget(child, idBitsToAssign)"]
    AH --> AI[设置alreadyDispatchedToNewTouchTarget=true]
    AI --> AJ[break循环]
    
    AJ --> AK[TouchTarget查找完成]
    AC --> AK
    T --> AK
    R --> AK
```

### 5.3 多指触摸的详细分发流程图

```mermaid
flowchart TD
    A[开始多指触摸处理] --> B{检查事件类型}
    B -->|ACTION_DOWN| C[第一个手指按下<br/>建立TouchTarget1]
    B -->|ACTION_POINTER_DOWN| D[后续手指按下<br/>建立TouchTargetN]
    B -->|ACTION_MOVE| E[手指移动<br/>直接分发]
    B -->|ACTION_POINTER_UP| F[非第一个手指抬起<br/>清理TouchTarget]
    B -->|ACTION_UP| G[最后一个手指抬起<br/>清理所有TouchTarget]
    
    C --> H[获取手指ID和坐标<br/>pointerId=0, actionIndex=0]
    H --> I[建立idBitsToAssign=1<<0]
    I --> J[清理旧TouchTarget<br/>removePointersFromTouchTargets]
    J --> K[遍历子View查找目标]
    K --> L{找到合适子View?}
    L -->|是| M[建立TouchTarget1<br/>pointerIdBits=0b1]
    L -->|否| N[ViewGroup自己处理]
    
    D --> O[获取新手指信息<br/>actionIndex=N, pointerId=N]
    O --> P[建立idBitsToAssign=1<<N]
    P --> Q[清理对应手指的旧TouchTarget]
    Q --> R[重新遍历子View查找目标]
    R --> S{找到合适子View?}
    S -->|是| T[建立TouchTargetN<br/>pointerIdBits=1<<N]
    S -->|否| U[分配给最近TouchTarget]
    
    U --> V[找到链表尾的TouchTarget]
    V --> W[更新pointerIdBits<br/>添加新手指ID]
    
    E --> X[遍历TouchTarget链表]
    X --> Y{检查手指ID匹配<br/>target.pointerIdBits & idBits != 0?}
    Y -->|是| Z[分发事件给对应子View]
    Y -->|否| AA[跳过该TouchTarget]
    AA --> X
    
    Z --> AB[子View处理MOVE事件]
    AB --> AC[继续下一个TouchTarget]
    AC --> X
    
    F --> AD[获取抬起手指信息<br/>actionIndex=N, pointerId=N]
    AD --> AE[建立idBitsToRemove=1<<N]
    AE --> AF[遍历TouchTarget链表]
    AF --> AG{检查手指ID匹配<br/>target.pointerIdBits & idBitsToRemove != 0?}
    AG -->|是| AH[移除对应手指ID<br/>target.pointerIdBits &= ~idBitsToRemove]
    AG -->|否| AI[跳过该TouchTarget]
    AI --> AF
    
    AH --> AJ{检查pointerIdBits是否为0?}
    AJ -->|是| AK[移除整个TouchTarget]
    AJ -->|否| AL[保留TouchTarget]
    AL --> AF
    
    G --> AM[调用resetTouchState<br/>清理所有TouchTarget]
    AM --> AN[设置mFirstTouchTarget=null]
    
    M --> AO[多指触摸处理完成]
    N --> AO
    T --> AO
    W --> AO
    AB --> AO
    AK --> AO
    AN --> AO
```

### 5.4 时序图：从系统层到应用层的完整调用链

```mermaid
sequenceDiagram
    participant InputReader
    participant InputDispatcher
    participant ViewRootImpl
    participant Activity
    participant Window
    participant DecorView
    participant ViewGroup
    participant View
    
    InputReader->>InputDispatcher: 读取触摸事件
    InputDispatcher->>ViewRootImpl: Binder跨进程调用
    ViewRootImpl->>DecorView: 分发事件
    DecorView->>Activity: dispatchTouchEvent
    
    Activity->>Activity: ACTION_DOWN检查
    Activity->>Window: superDispatchTouchEvent
    Window->>DecorView: superDispatchTouchEvent
    DecorView->>ViewGroup: dispatchTouchEvent
    
    ViewGroup->>ViewGroup: 检查拦截条件
    ViewGroup->>ViewGroup: 遍历子View查找目标
    ViewGroup->>View: dispatchTransformedTouchEvent
    View->>View: onTouchEvent处理
    View-->>ViewGroup: 返回处理结果
    
    ViewGroup->>ViewGroup: 建立TouchTarget
    ViewGroup-->>DecorView: 返回处理结果
    DecorView-->>Window: 返回处理结果
    Window-->>Activity: 返回处理结果
    Activity-->>ViewRootImpl: 返回处理结果
    
    Note over ViewGroup,View: 后续MOVE/UP事件直接分发
    ViewGroup->>View: 通过TouchTarget直接分发
    View->>View: onTouchEvent处理
    View-->>ViewGroup: 返回处理结果
```

### 5.5 ViewRootImpl.dispatchInputEvent方法

[源码证据：frameworks/base/core/java/android/view/ViewRootImpl.java#L7890-7920]

```java
void dispatchInputEvent(InputEvent event) {
    // 将事件加入队列
    enqueueInputEvent(event);
}

void enqueueInputEvent(InputEvent event) {
    // 处理输入事件队列
    if (mInputEventReceiver != null) {
        mInputEventReceiver.onInputEvent(event);
    }
}
```

### 5.6 InputEventReceiver.onInputEvent方法

[源码证据：frameworks/base/core/java/android/view/InputEventReceiver.java#L250-280]

```java
public void onInputEvent(InputEvent event) {
    // 处理输入事件
    finishInputEvent(event, false);
}
```

