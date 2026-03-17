# 补充专题
<!-- source: 12-9.md -->

# 9. 完整跨层分析模型


<!-- source: 19-102-broadcast.md -->

# 10.2 Broadcast 超时

关注：

- 广播队列 / 分发器
- 前台广播与后台广播不同超时阈值
- ordered broadcast 链式阻塞
- receiver 进程启动时间是否计入整体路径


<!-- source: 21-104-provider.md -->

# 10.4 Provider 超时

关注：

- provider 发布是否完成
- 进程启动、dexopt、class loading、DB init 是否拖慢
- 是 provider 自身慢还是启动链整体慢

------


<!-- source: 26-114.md -->

# 11.4 调度 / 锁 / 内核相关

- futex wait
- binder driver
- scheduler runqueue
- I/O wait
- cgroup / cpuset / uclamp / top-app 调度相关策略

------


<!-- source: 28-13.md -->

# 13. 源码分析方法论


<!-- source: 54-182.md -->

# 18.2 关键分析项

- 广播是前台还是后台
- ordered 还是 parallel
- 当前 receiver 是第几个
- 进程是否刚拉起
- onReceive 是否在主线程执行
- 是否有 goAsync，但未及时 finish
- AMS 广播分发线程是否拥塞

------


<!-- source: 59-201.md -->

# 20.1 高风险场景

- Application / provider 同时做重初始化
- SQLite open/upgrade
- 多 provider 串行初始化
- 首启解压、迁移、校验
- 冷启动 + provider 阻塞首帧


<!-- source: 68-242-binder.md -->

# 24.2 Binder 模式

1. 主线程 Binder 调用卡在 system_server
2. system_server binder thread pool 饱和
3. app binder thread pool 饱和
4. 双向 Binder 回调环路等待
5. Binder 结果依赖另一个被阻塞线程
6. 大量小 Binder 串行排队
7. 死亡通知或回调风暴导致拥塞


<!-- source: 70-244.md -->

# 24.4 广播模式

1. onReceive 主线程耗时
2. ordered broadcast 前序 receiver 卡死
3. goAsync 后忘记 finish
4. receiver 冷启动太慢
5. receiver 内访问 provider 导致阻塞
6. receiver 内同步等待后台任务


<!-- source: 72-246-provider.md -->

# 24.6 Provider 模式

1. provider onCreate 初始化数据库
2. provider 打开大文件
3. provider schema 升级
4. provider 多实例串行初始化
5. provider 拉起进程太慢
6. provider 被锁或 DB 锁阻塞


<!-- source: 81-5.md -->

# 5. 线程栈分析
### 5.1 App main thread
### 5.2 Binder thread
### 5.3 system_server 关键线程
### 5.4 其它线程


<!-- source: 84-8.md -->

# 8. 排除项
- 已排除哪些可能性：
- 依据是什么：


<!-- source: 85-9.md -->

# 9. 修复建议
- 短期规避：
- 中期修复：
- 长期治理：


<!-- source: 87-26-skill.md -->

# 26. Skill 执行步骤

执行本 Skill 时建议遵循以下步骤。


<!-- source: 90-step-3.md -->

# Step 3：锁定被等待对象

明确系统在等待哪个线程/组件/窗口状态。


<!-- source: 91-step-4.md -->

# Step 4：分析线程栈

同时看：

- app main thread
- binder 线程
- system_server 关键线程


<!-- source: 94-step-7.md -->

# Step 7：验证与排除

说明为什么不是别的原因。
