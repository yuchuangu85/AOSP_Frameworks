# 补充专题
<!-- source: 43-112.md -->

# 11.2 进程管理

- `ProcessList.java`
- `ProcessRecord.java`
- `ProcessStateRecord.java`
- `OomAdjuster.java`
- `CachedAppOptimizer.java`
- `AppProfiler.java`


<!-- source: 45-114-broadcast.md -->

# 11.4 Broadcast

- `BroadcastQueue*.java`
- `BroadcastDispatcher.java`
- `BroadcastRecord.java`


<!-- source: 46-115-provider.md -->

# 11.5 Provider

- `ContentProviderHelper.java`
- `ContentProviderRecord.java`
- `ProviderMap.java`


<!-- source: 50-119.md -->

# 11.9 进程创建

- `ZygoteProcess.java`
- `Process.java`

------

# 12. 专家级分析步骤


<!-- source: 51-121.md -->

# 12.1 第一步：确定问题主语

先明确是：

- 进程问题
- 组件问题
- 超时问题
- 重要性问题
- 回收问题
- 可见性问题


<!-- source: 52-122.md -->

# 12.2 第二步：建立跨层主链

必须回答：

- 请求从谁进入
- 决策在 AMS / ATMS / WMS 哪层
- 执行在 app 哪个线程
- 状态由谁维护
- 最终结果回传到哪


<!-- source: 58-152-aosp-wms.md -->

# 15.2 与 `aosp-wms`

当涉及：

- 可见性
- 焦点
- resumed 但不可见
- 窗口准备慢
   必须联动 `aosp-wms`
