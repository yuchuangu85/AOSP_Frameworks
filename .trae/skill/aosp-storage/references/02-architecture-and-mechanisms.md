# 架构与核心机制
<!-- source: 02-1.md -->

# 1. 技能目标

该技能用于对 Android AOSP 存储栈进行**体系化、可验证、可追溯**的源码分析，重点解决以下问题：

1. **理解存储栈整体架构**
   - Framework、SystemServer、Native Daemon、Kernel/VFS、Block Device、文件系统之间如何协作。
   - App 文件访问为何能映射到 `/data`、`/storage/emulated`、`/mnt/runtime`、FUSE、MediaProvider 等不同路径与权限模型。

2. **还原关键调用链**
   - 应用安装、用户解锁、分区挂载、卷状态变化、外部存储插拔、媒体扫描、容量统计、目录创建、清理与回收等路径。

3. **定位复杂问题根因**
   - 挂载失败、找不到存储卷、存储权限异常、文件不可见、空间统计错误、安装失败、dex/odex/data 目录异常、FUSE 性能瓶颈、quota 异常、存储加密或多用户隔离问题。

4. **输出工程化分析结果**
   - 架构图、时序图、模块职责、源码入口、关键数据结构、状态机、证据链、问题归因、优化建议、风险评估。

---


<!-- source: 03-2.md -->

# 2. 适用问题范围

本技能适用于以下 AOSP 存储相关分析任务：

### 2.1 架构与源码理解
- Android 存储栈总体设计
- `/data`、`/sdcard`、`/storage/emulated`、`/mnt/user`、`/mnt/runtime` 路径关系
- vold、installd、StorageManagerService、StorageVolume、VolumeInfo、DiskInfo 关系
- fs_mgr、fstab、first-stage mount、dm-verity、metadata、userdata 分区初始化
- adoptable storage / portable storage 机制
- CE/DE（Credential Encrypted / Device Encrypted）目录模型
- 多用户存储隔离与 userId/profileId 路径映射
- scoped storage / MediaStore / FUSE / app-specific directory 访问模型

### 2.2 功能与行为分析
- 开机挂载流程
- 用户解锁后 CE 目录可用流程
- 外部存储插拔识别与卷状态广播
- 应用安装、卸载、移动、清理数据流程
- 应用数据目录、cache、code_cache、obb、media、sandbox 目录生成流程
- 应用通过 Java / NDK 访问文件的真实链路
- MediaProvider 与 FUSE 对文件访问可见性的影响
- 存储空间统计、quota、磁盘清理、缓存回收机制

### 2.3 异常与问题定位
- 存储卷未挂载 / 状态异常
- SD 卡 / USB OTG 识别失败
- “没有权限访问文件”但目录实际存在
- 文件创建成功但系统文件管理器看不到
- App 卸载后残留数据
- Package 安装失败与数据目录权限错误
- 容量显示不准 / 存储空间异常减少
- FUSE 导致 I/O 慢、卡顿、耗电
- vold / installd crash 或命令执行失败
- user 0 / work profile / clone profile 下路径或可见性异常
- 系统升级后 fstab / mount / selinux / vold 行为变化

---


<!-- source: 05-4.md -->

# 4. 核心分析视角

分析 AOSP Storage 时，必须从以下 8 个视角展开。

### 4.1 分层架构视角
典型分层如下：

1. **App 层**
   - Java File API
   - ContentResolver / MediaStore
   - SAF（Storage Access Framework）
   - NDK POSIX file I/O

2. **Framework API / System Service 层**
   - StorageManager
   - StorageVolume
   - StorageStatsManager
   - Environment
   - PackageManager / Installer interface
   - MountService 历史模型 / StorageManagerService 当前主模型

3. **SystemServer 层**
   - StorageManagerService
   - PackageManagerService 与 installd 交互
   - UserManager / ActivityManager 与 user unlock 生命周期
   - MediaProvider 相关广播与卷扫描联动

4. **Native Daemon 层**
   - vold
   - installd
   - apexd / otapreopt / dexopt 某些数据目录链路相关组件

5. **Early Mount / Init / fs_mgr 层**
   - init
   - first_stage_mount
   - fstab parser
   - dm-verity / logical partition / metadata / userdata

6. **Kernel / VFS / Block Device 层**
   - ext4 / f2fs / vfat / exfat / erofs 等
   - dm device
   - loop / block device
   - mount namespace
   - quota
   - inotify / fanotify（如相关）

7. **安全与权限层**
   - SELinux
   - fs permissions
   - UID/GID / sdcard_rw / media_rw / everybody
   - runtime permission / appops / scoped storage policy

8. **运行时观测层**
   - logcat
   - dmesg / kernel log
   - dumpsys mount / storage
   - lsof / mountinfo / proc filesystem
   - statsd / perfetto / atrace（如 I/O 性能）

---


<!-- source: 06-5-aosp-storage.md -->

# 5. AOSP Storage 核心模块图

```text
App
 ├─ File / NIO / libc open()
 ├─ MediaStore / ContentResolver
 └─ SAF / DocumentsProvider
        │
        ▼
Framework API
 ├─ StorageManager / StorageVolume
 ├─ StorageStatsManager
 ├─ Environment
 └─ PackageManager / Installer
        │
        ▼
SystemServer
 ├─ StorageManagerService
 ├─ PackageManagerService
 ├─ UserManagerService
 ├─ ActivityManagerService
 └─ MediaProvider / related broadcast flow
        │
        ├──────── Binder / Installer protocol ───────► installd
        └──────── Native daemon command channel ─────► vold
                                                        │
                                                        ├─ VolumeManager
                                                        ├─ PublicVolume / PrivateVolume / EmulatedVolume
                                                        ├─ fs utilities / fscrypt / checkpoint / trim / idle-maint
                                                        └─ mount / unmount / format / benchmark
                                                                │
                                                                ▼
Kernel / VFS / Block / Filesystem
 ├─ ext4 / f2fs / vfat / exfat
 ├─ dm-verity / dm-default-key / dm-user
 ├─ quota / fscrypt
 └─ block device / logical partition
```


<!-- source: 07-6.md -->

# 6. 必须掌握的核心源码模块

以下模块是执行分析时的主入口。

### 6.1 Framework / Java 层

#### Storage 相关

- `frameworks/base/core/java/android/os/Environment.java`
- `frameworks/base/core/java/android/os/storage/StorageManager.java`
- `frameworks/base/core/java/android/os/storage/StorageVolume.java`
- `frameworks/base/core/java/android/app/usage/StorageStatsManager.java`

#### System Service 相关

- `frameworks/base/services/core/java/com/android/server/StorageManagerService.java`
- `frameworks/base/services/core/java/com/android/server/StorageManagerService.java` 内部回调、广播、Volume 状态流转
- `frameworks/base/services/core/java/com/android/server/usage/StorageStatsService.java`

#### Package / Installer 相关

- `frameworks/base/services/core/java/com/android/server/pm/Installer.java`
- `frameworks/base/services/core/java/com/android/server/pm/PackageManagerService.java`
- `frameworks/base/services/core/java/com/android/server/pm/AppDataHelper.java`
- `frameworks/base/services/core/java/com/android/server/pm/InstallPackageHelper.java`

#### 用户与解锁相关

- `frameworks/base/services/core/java/com/android/server/locksettings/`
- `frameworks/base/services/core/java/com/android/server/pm/UserManagerService.java`
- `frameworks/base/services/core/java/com/android/server/am/UserController.java`

### 6.2 Native / daemon 层

#### vold

- `system/vold/`
- 重点目录：
  - `VolumeManager.*`
  - `VolumeBase.*`
  - `PublicVolume.*`
  - `PrivateVolume.*`
  - `EmulatedVolume.*`
  - `Disk.*`
  - `NetlinkManager.*`
  - `ResponseCode.h`
  - `Utils.*`
  - `VoldNativeService.*`
  - `Checkpoint.*`
  - `IdleMaint.*`
  - `FsCrypt.*`
  - `model/`
  - `fs/`

#### installd

- `frameworks/native/cmds/installd/`
- 重点：
  - app data create / destroy
  - dexopt / profiles
  - migrate / move
  - quota / cache / free space
  - CE / DE 目录操作
  - OBB / external storage 关联路径处理

### 6.3 early mount / fs_mgr / init

- `system/core/init/`
- `system/core/fs_mgr/`
- `system/core/libcutils/`
- `system/core/rootdir/`
- `fstab.<device>`
- `first_stage_mount.cpp`
- `fs_mgr_fstab.*`
- `fs_mgr_mount_all.*`
- `fs_mgr_dm_linear.*`
- `fs_mgr_overlayfs.*`
- `fs_mgr_remount.*`

### 6.4 Provider / 访问中介层

- `packages/providers/MediaProvider/`
- `packages/modules/Permission/`
- DocumentsUI / DocumentsProvider 相关实现
- ExternalStorageProvider（视版本而定）

------


<!-- source: 10-9.md -->

# 9. 存储栈关键机制分析框架


<!-- source: 13-93-vold.md -->

# 9.3 vold 卷管理模型

必须分析：

- vold 如何启动
- block device 如何被发现
- Disk 与 Volume 如何创建
- public/private/emulated volume 区别
- 卷状态机如何变化
- volume state 如何同步到 framework
- mount、unmount、format、benchmark、trim、idle-maintenance 如何执行

重点状态：

- unmounted
- checking
- mounted
- mounted_read_only
- ejecting
- unmountable
- removed
- bad_removal

------


<!-- source: 16-96.md -->

# 9.6 配额、空间统计与缓存回收

需要分析：

- quota 如何启用
- StorageStatsService 如何获取统计数据
- package / uid / user / external volume 维度如何统计
- cache 回收由谁触发
- 为什么设置页看到的空间分布与 `du` 不一致
- reserved blocks / metadata / snapshots / filesystem overhead 如何影响显示

------


<!-- source: 18-101.md -->

# 10.1 开机到存储可用时序

```
init
  │
  ├─ 解析 fstab
  │
  ├─ first stage mount
  │
  ├─ 挂载关键分区 / metadata / userdata
  │
  ├─ 启动 vold
  │
  ├─ vold 扫描块设备与卷
  │
  ├─ SystemServer 启动 StorageManagerService
  │
  ├─ 建立 vold ↔ framework 回调通路
  │
  ├─ 识别/挂载 emulated/public/private volumes
  │
  └─ 系统广播存储状态，应用开始感知可用卷
```


<!-- source: 21-11.md -->

# 11. 必须重点检查的源码入口

执行分析时，优先从以下入口开始：

### 11.1 开机挂载

- init main
- first stage mount
- fs_mgr_mount_all
- vold main
- StorageManagerService.systemReady / lifecycle

### 11.2 卷状态变化

- vold volume event handling
- VolumeInfo state mapping
- `onVolumeCreated / onVolumeStateChanged / onDiskScanned`
- 广播发送点与 callback 分发点

### 11.3 应用数据目录

- PMS 安装/卸载入口
- `Installer.java`
- installd `createAppData`, `destroyAppData`, `fixupAppData`, `clearAppData`

### 11.4 外部存储访问

- `Environment.getExternalStorageDirectory`
- `Context.getExternalFilesDir`
- MediaStore insert/query/openFile
- FUSE / MediaProvider 路径检查
- AppOps / permission check / user restriction

### 11.5 空间统计

- `StorageStatsService`
- quota collection
- Package stats aggregation
- Settings 空间显示链路

------


<!-- source: 27-141-logcat-tag.md -->

# 14.1 logcat 重点 TAG

- `StorageManagerService`
- `Vold`
- `vold`
- `installd`
- `PackageManager`
- `Installer`
- `MediaProvider`
- `sdcard`
- `fs_mgr`
- `init`
- `ActivityManager`
- `SystemServer`


<!-- source: 32-161.md -->

# 16.1 挂载失败分析模板

需要回答：

1. 失败发生在 first-stage mount、vold mount、还是 framework 状态同步阶段？
2. 对应分区/卷类型是什么？
3. fstab、fs type、block device、fsck、SELinux 是否正常？
4. vold 是否收到事件并创建 Volume？
5. framework 是否正确接收到 state change？


<!-- source: 35-164.md -->

# 16.4 容量统计异常分析模板

需要回答：

1. 系统 UI 展示数据来自哪个 service？
2. 统计口径基于 quota 还是扫描？
3. 是否包含多用户、snapshot、reserved space？
4. 哪类目录被归入“其他”？
5. 与 `du`/`df` 不一致的原因是什么？
