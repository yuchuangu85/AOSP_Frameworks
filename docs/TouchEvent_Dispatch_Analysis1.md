# Android触摸事件分发机制完整分析

[toc]

## 概述

本文档详细分析Android系统中从Activity的`dispatchTouchEvent`方法开始的触摸事件分发流程。通过源码分析和流程图展示，揭示触摸事件从系统层到应用层的完整传递路径。

## 1. 触摸事件分发整体架构

### 1.1 事件分发层级结构

Android触摸事件分发采用分层架构，从底层硬件到上层应用，包含以下关键层级：

```mermaid
graph TB
    A[硬件输入设备] --> B[InputReader]
    B --> C[InputDispatcher]
    C --> D[应用进程Binder通信]
    D --> E[ViewRootImpl]
    E --> F[DecorView]
    F --> G[Activity]
    G --> H[Window]
    H --> I[ViewGroup]
    I --> J[View]
```

### 1.2 核心组件角色

| 组件 | 角色描述 | 关键方法 |
|------|----------|----------|
| InputDispatcher | 系统级事件分发器 | dispatchMotionEvent |
| ViewRootImpl | 应用窗口根节点 | dispatchInputEvent |
| Activity | 应用入口点 | dispatchTouchEvent |
| Window | 窗口管理器 | superDispatchTouchEvent |
| DecorView | 窗口装饰视图 | dispatchTouchEvent |
| ViewGroup | 视图容器 | dispatchTouchEvent |
| View | 具体视图 | dispatchTouchEvent |

## 2. 详细的事件处理流程：DOWN、MOVE、UP分析

### 2.1 ACTION_DOWN事件处理流程

ACTION_DOWN是触摸事件的起点，决定了后续事件序列的分发目标。

#### 2.1.1 Activity层处理

[源码证据：frameworks/base/core/java/android/app/Activity.java#L4661-4670]

```java
public boolean dispatchTouchEvent(MotionEvent ev) {
    if (ev.getAction() == MotionEvent.ACTION_DOWN) {
        onUserInteraction();  // 通知用户交互开始
    }
    if (getWindow().superDispatchTouchEvent(ev)) {
        return true;  // 窗口处理了事件
    }
    return onTouchEvent(ev);  // Activity自己处理
}
```

**ACTION_DOWN关键作用：**
1. **确定事件序列起点**：每个触摸序列都以ACTION_DOWN开始
2. **建立TouchTarget**：ViewGroup会记录消费事件的子View
3. **重置状态**：清除前一个触摸序列的状态

#### 2.1.2 ViewGroup的TouchTarget建立

[源码证据：frameworks/base/core/java/android/view/ViewGroup.java#L2710-2750]

```java
// 在ACTION_DOWN时建立TouchTarget
if (dispatchTransformedTouchEvent(ev, false, child, idBitsToAssign)) {
    // 子View消费了事件，建立TouchTarget
    newTouchTarget = addTouchTarget(child, idBitsToAssign);
    alreadyDispatchedToNewTouchTarget = true;
    break;
}
```

### 2.2 ACTION_MOVE事件处理流程

ACTION_MOVE事件遵循"U型图"分发原则，直接分发给已建立的TouchTarget。

#### 2.2.1 U型图原理详解

**U型图分发机制：**
```
ACTION_DOWN: Activity → Window → DecorView → ViewGroup → View (建立TouchTarget)
ACTION_MOVE: View ← ViewGroup ← (直接分发，跳过上层)
ACTION_UP:   View ← ViewGroup ← (直接分发，跳过上层)
```

**源码实现：**
```java
// ViewGroup.dispatchTouchEvent中对ACTION_MOVE的处理
if (mFirstTouchTarget == null) {
    // 没有TouchTarget，自己处理
    handled = dispatchTransformedTouchEvent(ev, canceled, null, TouchTarget.ALL_POINTER_IDS);
} else {
    // 有TouchTarget，直接分发
    TouchTarget target = mFirstTouchTarget;
    while (target != null) {
        if (dispatchTransformedTouchEvent(ev, cancelChild, target.child, target.pointerIdBits)) {
            handled = true;
        }
        target = next;
    }
}
```

#### 2.2.2 事件拦截机制

ViewGroup可以在ACTION_MOVE时通过`onInterceptTouchEvent`拦截事件：

```java
// 检查是否需要拦截
if (actionMasked == MotionEvent.ACTION_DOWN || mFirstTouchTarget != null) {
    final boolean disallowIntercept = (mGroupFlags & FLAG_DISALLOW_INTERCEPT) != 0;
    if (!disallowIntercept) {
        intercepted = onInterceptTouchEvent(ev);
    }
}
```

### 2.3 ACTION_UP事件处理流程

ACTION_UP是触摸序列的结束，触发点击事件和清理状态。

#### 2.3.1 onClick触发机制

[源码证据：frameworks/base/core/java/android/view/View.java#L14820-14840]

```java
case MotionEvent.ACTION_UP:
    if ((mPrivateFlags & PFLAG_PRESSED) != 0) {
        // 触发点击事件
        performClick();
    }
    setPressed(false);
    break;

// performClick方法实现
public boolean performClick() {
    final boolean result;
    final ListenerInfo li = mListenerInfo;
    if (li != null && li.mOnClickListener != null) {
        playSoundEffect(SoundEffectConstants.CLICK);
        li.mOnClickListener.onClick(this);  // 调用onClick回调
        result = true;
    } else {
        result = false;
    }
    return result;
}
```

**onClick触发条件：**
1. **按下状态**：View必须处于按下状态（PFLAG_PRESSED）
2. **在View边界内抬起**：手指在View边界内抬起
3. **未发生长按**：未触发onLongClick

#### 2.3.2 onLongClick触发机制

[源码证据：frameworks/base/core/java/android/view/View.java#L8281-8291]

```java
// checkForLongClick方法
private void checkForLongClick(int delay, float x, float y) {
    if ((mViewFlags & LONG_CLICKABLE) == LONG_CLICKABLE) {
        mHasPerformedLongPress = false;
        
        if (mPendingCheckForLongPress == null) {
            mPendingCheckForLongPress = new CheckForLongPress();
        }
        mPendingCheckForLongPress.setAnchor(x, y);
        postDelayed(mPendingCheckForLongPress, delay);
    }
}

// CheckForLongPress.run方法
public void run() {
    if (isPressed() && (mParent != null)) {
        if (performLongClick(mX, mY)) {
            mHasPerformedLongPress = true;
        }
    }
}

// performLongClick方法
public boolean performLongClick(float x, float y) {
    boolean handled = false;
    final OnLongClickListener listener = mListenerInfo == null ? null : mListenerInfo.mOnLongClickListener;
    if (listener != null) {
        handled = listener.onLongClick(View.this);  // 调用onLongClick回调
    }
    return handled;
}
```

**onLongClick触发条件：**
1. **长按时长**：默认500ms（ViewConfiguration.getLongPressTimeout()）
2. **按下状态保持**：手指持续按下未移动出边界
3. **可长按标志**：View必须设置LONG_CLICKABLE标志

### 2.4 状态清理和重置

在ACTION_UP或ACTION_CANCEL时，系统会清理触摸状态：

```java
case MotionEvent.ACTION_UP:
case MotionEvent.ACTION_CANCEL:
    // 清理触摸状态
    mPrivateFlags3 &= ~PFLAG3_FINGER_DOWN;
    setPressed(false);
    removeTapCallback();      // 移除点击回调
    removeLongPressCallback(); // 移除长按回调
    mHasPerformedLongPress = false;
    break;
```

### 2.2 Window.superDispatchTouchEvent方法

Window是一个抽象类，具体实现由PhoneWindow提供：

[源码证据：frameworks/base/core/java/android/view/Window.java#L2124]

```java
public abstract boolean superDispatchTouchEvent(MotionEvent event);
```

### 2.3 PhoneWindow.superDispatchTouchEvent实现

[源码证据：frameworks/base/core/java/com/android/internal/policy/PhoneWindow.java#L1512-1516]

```java
@Override
public boolean superDispatchTouchEvent(MotionEvent event) {
    return mDecor.superDispatchTouchEvent(event);
}
```

PhoneWindow将事件委托给DecorView处理。

### 2.4 DecorView.superDispatchTouchEvent方法

DecorView是窗口的根视图，继承自FrameLayout：

[源码证据：frameworks/base/core/java/com/android/internal/policy/DecorView.java#L1284-1288]

```java
public boolean superDispatchTouchEvent(MotionEvent event) {
    return super.dispatchTouchEvent(event);
}
```

DecorView调用父类的`dispatchTouchEvent`方法，即ViewGroup的dispatchTouchEvent。

## 3. 多指触摸处理机制

### 3.1 多指触摸事件类型

Android支持多指触摸，通过以下事件类型处理：

| 事件类型 | 描述 | 触发条件 |
|---------|------|----------|
| ACTION_DOWN | 第一个手指按下 | 触摸序列开始 |
| ACTION_POINTER_DOWN | 后续手指按下 | 第二个及以后的手指按下 |
| ACTION_MOVE | 手指移动 | 任何手指移动 |
| ACTION_POINTER_UP | 非第一个手指抬起 | 第二个及以后的手指抬起 |
| ACTION_UP | 最后一个手指抬起 | 触摸序列结束 |

### 3.2 多指触摸事件分发原理

#### 3.2.1 事件索引和ID管理

[源码证据：frameworks/base/core/java/android/view/MotionEvent.java#L1200-1250]

```java
// 获取手指数量
int pointerCount = event.getPointerCount();

// 获取事件索引和手指ID
int actionIndex = event.getActionIndex();
int pointerId = event.getPointerId(actionIndex);

// 获取特定手指的坐标
float x = event.getX(actionIndex);
float y = event.getY(actionIndex);
```

#### 3.2.2 ViewGroup的多指触摸处理

ViewGroup通过TouchTarget链表管理多个手指的触摸目标：

```java
// ViewGroup中的TouchTarget链表结构
private static final class TouchTarget {
    public View child;
    public int pointerIdBits;
    public TouchTarget next;
    
    // 添加手指ID到bits中
    public static TouchTarget obtain(View child, int pointerIdBits) {
        TouchTarget target = new TouchTarget();
        target.child = child;
        target.pointerIdBits = pointerIdBits;
        return target;
    }
}
```

### 3.3 多指触摸的分发流程

#### 3.3.1 ACTION_POINTER_DOWN处理

当第二个手指按下时，系统会重新分发事件：

```java
case MotionEvent.ACTION_POINTER_DOWN: {
    // 获取新手指的索引和ID
    final int actionIndex = ev.getActionIndex();
    final int idBitsToAssign = 1 << ev.getPointerId(actionIndex);
    
    // 重新分发事件，寻找新的触摸目标
    final int childrenCount = mChildrenCount;
    if (childrenCount != 0) {
        final float x = ev.getX(actionIndex);
        final float y = ev.getY(actionIndex);
        
        // 查找能够接收新手指事件的子View
        final ArrayList<View> preorderedList = buildOrderedChildList();
        final boolean customOrder = preorderedList == null && isChildrenDrawingOrderEnabled();
        final View[] children = mChildren;
        
        for (int i = childrenCount - 1; i >= 0; i--) {
            final int childIndex = getAndVerifyPreorderedIndex(childrenCount, i, customOrder);
            final View child = getAndVerifyPreorderedView(preorderedList, children, childIndex);
            
            if (!canViewReceivePointerEvents(child) || 
                !isTransformedTouchPointInView(x, y, child, null)) {
                continue;
            }
            
            // 分发事件给子View
            if (dispatchTransformedTouchEvent(ev, false, child, idBitsToAssign)) {
                // 子View消费了事件，添加到TouchTarget链表
                newTouchTarget = addTouchTarget(child, idBitsToAssign);
                alreadyDispatchedToNewTouchTarget = true;
                break;
            }
        }
    }
    break;
}
```

#### 3.3.2 多指触摸的U型图分发

多指触摸同样遵循U型图原则，但每个手指可能有不同的TouchTarget：

```
手指1: ACTION_DOWN → ViewA (建立TouchTarget1)
手指2: ACTION_POINTER_DOWN → ViewB (建立TouchTarget2)
手指1移动: ACTION_MOVE → ViewA (直接分发)
手指2移动: ACTION_MOVE → ViewB (直接分发)
手指1抬起: ACTION_POINTER_UP → ViewA (清理TouchTarget1)
手指2抬起: ACTION_UP → ViewB (清理TouchTarget2)
```

## 4. 事件分发机制

### 4.1 ViewGroup

#### 4.1.1 ViewGroup.dispatchTouchEvent核心逻辑

[源码证据：frameworks/base/core/java/android/view/ViewGroup.java#L2670-2750]

```java
@Override
public boolean dispatchTouchEvent(MotionEvent ev) {
    // 1. 检查是否需要拦截事件
    final boolean intercepted;
    if (actionMasked == MotionEvent.ACTION_DOWN || mFirstTouchTarget != null) {
        final boolean disallowIntercept = (mGroupFlags & FLAG_DISALLOW_INTERCEPT) != 0;
        if (!disallowIntercept) {
            intercepted = onInterceptTouchEvent(ev);
            ev.setAction(action); // restore action in case it was changed
        } else {
            intercepted = false;
        }
    } else {
        intercepted = true;
    }
    
    // 2. 分发事件给子View
    if (!canceled && !intercepted) {
        // 查找能够接收事件的子View
        final View[] children = mChildren;
        for (int i = childrenCount - 1; i >= 0; i--) {
            final int childIndex = getAndVerifyPreorderedIndex(childrenCount, i, customOrder);
            final View child = getAndVerifyPreorderedView(preorderedList, children, childIndex);
            
            if (!canViewReceivePointerEvents(child) || !isTransformedTouchPointInView(x, y, child, null)) {
                continue;
            }
            
            newTouchTarget = getTouchTarget(child);
            if (newTouchTarget != null) {
                newTouchTarget.pointerIdBits |= idBitsToAssign;
                break;
            }
            
            resetCancelNextUpFlag(child);
            if (dispatchTransformedTouchEvent(ev, false, child, idBitsToAssign)) {
                // 子View消费了事件
                mLastTouchDownTime = ev.getDownTime();
                if (preorderedList != null) {
                    for (int j = 0; j < childrenCount; j++) {
                        if (children[childIndex] == mChildren[j]) {
                            mLastTouchDownIndex = j;
                            break;
                        }
                    }
                } else {
                    mLastTouchDownIndex = childIndex;
                }
                mLastTouchDownX = ev.getX();
                mLastTouchDownY = ev.getY();
                newTouchTarget = addTouchTarget(child, idBitsToAssign);
                alreadyDispatchedToNewTouchTarget = true;
                break;
            }
        }
    }
    
    // 3. 如果没有子View消费事件，自己处理
    if (mFirstTouchTarget == null) {
        handled = dispatchTransformedTouchEvent(ev, canceled, null, TouchTarget.ALL_POINTER_IDS);
    } else {
        // 分发事件给已找到的TouchTarget
        TouchTarget target = mFirstTouchTarget;
        while (target != null) {
            final TouchTarget next = target.next;
            if (alreadyDispatchedToNewTouchTarget && target == newTouchTarget) {
                handled = true;
            } else {
                final boolean cancelChild = resetCancelNextUpFlag(target.child) || intercepted;
                if (dispatchTransformedTouchEvent(ev, cancelChild, target.child, target.pointerIdBits)) {
                    handled = true;
                }
            }
            target = next;
        }
    }
    
    return handled;
}
```

#### 4.1.2 事件拦截机制：onInterceptTouchEvent

ViewGroup通过`onInterceptTouchEvent`方法决定是否拦截事件：

```java
public boolean onInterceptTouchEvent(MotionEvent ev) {
    if (ev.isFromSource(InputDevice.SOURCE_MOUSE) && ev.getAction() == MotionEvent.ACTION_DOWN) {
        // 处理鼠标事件
    }
    return false; // 默认不拦截
}
```

#### 4.1.3 事件分发：dispatchTransformedTouchEvent

该方法负责将事件转换并分发给子View：

```java
private boolean dispatchTransformedTouchEvent(MotionEvent event, boolean cancel,
        View child, int desiredPointerIdBits) {
    final boolean handled;
    
    // 如果需要取消事件
    final int oldAction = event.getAction();
    if (cancel || oldAction == MotionEvent.ACTION_CANCEL) {
        event.setAction(MotionEvent.ACTION_CANCEL);
        if (child == null) {
            handled = super.dispatchTouchEvent(event);
        } else {
            handled = child.dispatchTouchEvent(event);
        }
        event.setAction(oldAction);
        return handled;
    }
    
    // 转换坐标系统
    final float offsetX = mScrollX - child.mLeft;
    final float offsetY = mScrollY - child.mTop;
    event.offsetLocation(offsetX, offsetY);
    
    // 分发事件
    if (child == null) {
        handled = super.dispatchTouchEvent(event);
    } else {
        handled = child.dispatchTouchEvent(event);
    }
    
    // 恢复坐标系统
    event.offsetLocation(-offsetX, -offsetY);
    return handled;
}
```

### 4.2 View的事件处理机制

#### 4.2.1 View.dispatchTouchEvent方法

[源码证据：frameworks/base/core/java/android/view/View.java#L14320-14380]

```java
public boolean dispatchTouchEvent(MotionEvent event) {
    // 如果View不可用，直接返回
    if (!isEnabled()) {
        return isClickable() || isLongClickable() ? true : false;
    }
    
    // 处理辅助功能
    if (mTouchDelegate != null) {
        if (mTouchDelegate.onTouchEvent(event)) {
            return true;
        }
    }
    
    // 调用onTouchEvent处理事件
    if (onTouchEvent(event)) {
        return true;
    }
    
    return false;
}
```

#### 4.2.2 View.onTouchEvent方法

[源码证据：frameworks/base/core/java/android/view/View.java#L14750-14920]

```java
public boolean onTouchEvent(MotionEvent event) {
    final float x = event.getX();
    final float y = event.getY();
    final int action = event.getAction();
    final boolean clickable = (mViewFlags & CLICKABLE) == CLICKABLE || (mViewFlags & LONG_CLICKABLE) == LONG_CLICKABLE;
    
    if (!clickable) {
        return false;
    }
    
    switch (action) {
        case MotionEvent.ACTION_DOWN:
            mPrivateFlags3 |= PFLAG3_FINGER_DOWN;
            // 设置按下状态
            setPressed(true);
            // 检查长按
            checkForLongClick(0, x, y);
            break;
            
        case MotionEvent.ACTION_MOVE:
            // 检查是否移出View边界
            if (!pointInView(x, y, mTouchSlop)) {
                removeTapCallback();
                removeLongPressCallback();
                if ((mPrivateFlags & PFLAG_PRESSED) != 0) {
                    setPressed(false);
                }
            }
            break;
            
        case MotionEvent.ACTION_UP:
            mPrivateFlags3 &= ~PFLAG3_FINGER_DOWN;
            if ((mPrivateFlags & PFLAG_PRESSED) != 0) {
                // 处理点击事件
                performClick();
            }
            setPressed(false);
            break;
            
        case MotionEvent.ACTION_CANCEL:
            setPressed(false);
            removeTapCallback();
            removeLongPressCallback();
            break;
    }
    
    return true;
}
```

