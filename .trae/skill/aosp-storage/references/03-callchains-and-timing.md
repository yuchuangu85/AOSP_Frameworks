# 调用链与时序
<!-- source: 04-3.md -->

# 3. 强制分析原则

执行本技能时，必须遵循以下原则：

### 3.1 先整体、后局部
先建立完整存储架构，再进入具体类、方法、线程、binder、socket、命令、syscall 级别分析。

### 3.2 先调用链、后结论
所有结论必须建立在明确调用链上，至少回答：
- 入口是谁？
- 中间经过哪些服务/守护进程？
- 关键状态在哪里变化？
- 最终由谁执行实际挂载/创建/删除/权限设置？

### 3.3 先源码证据、后经验判断
必须尽可能给出：
- 关键类
- 关键方法
- 关键状态枚举
- 关键日志点
- 关键路径映射
- 关键系统属性 / 配置 / fstab / SELinux 约束

### 3.4 同时分析“逻辑路径”和“物理路径”
存储问题常见误区是只看 API 层路径，不看底层真实挂载与 namespace 映射。必须同时回答：
- App 看到的路径是什么？
- 系统内部真实路径是什么？
- 最终落在哪个文件系统/卷/用户隔离目录？

### 3.5 多用户 / 权限 / 加密必须纳入主链路
Android 存储并非单纯文件系统问题，必须显式纳入：
- userId / profile
- UID/GID
- mount namespace
- FUSE / MediaProvider
- scoped storage
- DE / CE 解锁状态
- SELinux context

---


<!-- source: 09-8.md -->

# 8. 完整跨层调用链模板

以下是本技能要求默认输出的跨层调用链模板。

### 8.1 开机挂载主链路

```
init
 └─ first_stage_mount / fs_mgr
     └─ mount critical partitions
         └─ vold start
             └─ VolumeManager init
                 └─ coldboot / netlink / block event
                     └─ scan disk
                         └─ create Disk / Volume objects
                             └─ mount public/private/emulated volumes
                                 └─ notify framework
                                     └─ StorageManagerService update state
                                         └─ broadcast / callback / user visible storage state
```

### 8.2 用户解锁后 CE 可用链路

```
User unlock
 └─ LockSettings / Gatekeeper / vold fscrypt key workflow
     └─ install/unlock CE storage key
         └─ mount / unlock credential-encrypted dirs
             └─ PackageManager / AMS receive user unlocked
                 └─ app CE data becomes accessible
```

### 8.3 应用安装与数据目录创建链路

```
PackageInstaller / PMS
 └─ PackageManagerService
     └─ Installer.java
         └─ installd binder/socket command
             └─ createAppData / prepareAppProfile / dexopt / restorecon
                 └─ create /data/user/<id>/<pkg> and related dirs
                     └─ return inode / ceDataInode / status
```

### 8.4 App 访问外部存储文件链路

```
App
 └─ java.io.File / libc open()
     └─ /storage/emulated/0/...
         └─ FUSE / sdcard / MediaProvider mediation (version dependent)
             └─ path remap to /data/media/0/...
                 └─ permission / appops / scoped storage check
                     └─ VFS / filesystem real I/O
```

### 8.5 外部 SD/USB 插入识别链路

```
Kernel uevent
 └─ netlink
     └─ vold NetlinkManager
         └─ Disk discovered
             └─ partition parse
                 └─ create PublicVolume / PrivateVolume
                     └─ mount / fsck / label / UUID
                         └─ notify StorageManagerService
                             └─ update VolumeInfo state
                                 └─ send broadcast / callback
```

------


<!-- source: 17-10.md -->

# 10. 时序图模板


<!-- source: 19-102.md -->

# 10.2 应用安装数据目录创建时序

```
PackageInstaller
  │
  ├─ PMS 解析安装请求
  │
  ├─ 分配 appId / user state
  │
  ├─ Installer.createAppData()
  │
  ├─ installd 创建 DE/CE 目录
  │
  ├─ 设置 mode / uid / gid / selinux
  │
  ├─ 返回 inode 与状态
  │
  ├─ dexopt / profile 初始化
  │
  └─ 应用可启动并访问对应 data dir
```


<!-- source: 20-103-app.md -->

# 10.3 App 访问外部存储文件时序

```
App
  │
  ├─ open("/storage/emulated/0/DCIM/a.jpg")
  │
  ├─ FUSE/中介层接收请求
  │
  ├─ 根据调用 UID / userId / path 执行权限判定
  │
  ├─ 映射到 /data/media/0/DCIM/a.jpg
  │
  ├─ VFS / filesystem 执行真实 I/O
  │
  └─ 返回 fd / errno
```

------


<!-- source: 23-121.md -->

# 12.1 标准输出结构

### A. 问题定义

- 用户要分析什么现象
- 属于挂载、权限、路径映射、安装数据、容量统计、I/O 性能中的哪一类

### B. 架构总览

- 涉及哪些层
- 涉及哪些核心服务/守护进程/文件系统

### C. 核心调用链

- 从入口到落点的完整链路
- 关键类 / 方法 / 线程 / binder / socket / syscall

### D. 关键状态与数据结构

- Volume / User / Path / UID / MountState / EncryptionState / PermissionState

### E. 关键源码解读

- 关键方法逐段解释
- 条件分支解释
- 状态变化解释

### F. 时序图 / 架构图

- 至少一个架构图
- 至少一个关键时序图

### G. 根因分析

- 现象如何产生
- 哪一层决定了结果
- 哪些条件不满足导致异常

### H. 验证与观测方法

- dumpsys
- logcat tag
- procfs
- mountinfo
- shell 命令
- 需要抓取哪些 trace / logs

### I. 修复与优化建议

- 代码修复点
- 配置修复点
- 风险与兼容性评估

------


<!-- source: 42-22.md -->

# 22. 一句话定义

**aosp-storage 是一个面向 Android 存储子系统的系统级源码分析技能，用于把“文件路径、卷管理、权限策略、用户隔离、加密状态、安装数据与真实 I/O 落点”统一到一条可验证的跨层调用链中。**
