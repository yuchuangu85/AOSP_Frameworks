# 补充专题
<!-- source: 08-7.md -->

# 7. 关键对象模型

分析时必须构建以下对象模型。

### 7.1 卷模型

- `DiskInfo`
- `VolumeInfo`
- `StorageVolume`
- `VolumeRecord`
- vold 内部 `Disk`
- vold 内部 `VolumeBase`
- `PublicVolume`
- `PrivateVolume`
- `EmulatedVolume`

需要说明：

- 每种卷的来源
- 状态流转
- 对外暴露的挂载点
- 是否可移除
- 是否主存储
- 是否对 App 可见
- 是否受用户解锁状态影响

### 7.2 用户与存储路径模型

必须区分：

- `/data/user/<userId>/`
- `/data/user_de/<userId>/`
- `/data/media/<userId>/`
- `/storage/emulated/<userId>/`
- `/mnt/user/<userId>/...`
- `/mnt/runtime/default/<userId>/...`
- `/mnt/runtime/read/<userId>/...`
- `/mnt/runtime/write/<userId>/...`
- `/storage/self/primary`

需要明确它们之间：

- 映射关系
- 可见性差异
- 权限差异
- 解锁前后变化
- 面向 App / shell / system 的差异

### 7.3 应用数据模型

- code path
- data dir
- cache dir
- code_cache
- no_backup
- external files dir
- external cache dir
- obb dir
- media dir
- profile / dalvik-cache / oat / artifacts

必须回答：

- 由谁创建？
- 用什么 UID/GID / mode？
- 何时销毁？
- 升级、重装、用户删除、清缓存时如何变化？

------


<!-- source: 11-91-data-ce-de.md -->

# 9.1 `/data` 分区与 CE / DE 模型

分析时必须讲清：

### DE（Device Encrypted）

- 设备启动早期即可访问
- 用户未解锁时可用
- 存放必须在锁屏前可用的数据
- 典型路径：
  - `/data/user_de/<userId>/`
  - `/data/system_de/<userId>/`

### CE（Credential Encrypted）

- 依赖用户凭证解锁
- 用户 unlock 后可用
- 典型路径：
  - `/data/user/<userId>/`
  - `/data/system_ce/<userId>/`

必须回答：

- key 是谁管理的
- vold/fscrypt 在何时安装 key
- PMS/AMS 如何感知 user unlocked
- 解锁前后应用行为差异是什么

------


<!-- source: 24-13.md -->

# 13. 问题分析决策树


<!-- source: 29-143.md -->

# 14.3 关键文件

- `/fstab.<hardware>`
- `/vendor/etc/fstab.*`
- `/proc/filesystems`
- `/proc/mounts`
- `/data/system/storage.xml`（视版本）
- `/metadata/`
- `/data/media/`
- `/data/user/`
- `/data/user_de/`

------
