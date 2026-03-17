# 问题模式与根因
<!-- source: 14-94-installd.md -->

# 9.4 installd 数据目录管理模型

必须分析：

- createAppData
- destroyAppData
- migrateAppData
- dexopt
- free cache
- quota / stats
- prepareAppProfile
- snapshot / rollback（视版本）
- external / obb / media 路径联动

必须输出：

- PMS 到 installd 的调用点
- 目录结构
- 权限设置
- SELinux restorecon
- UID/GID 规则
- 用户维度
- 主用户/工作资料隔离

------


<!-- source: 25-131.md -->

# 13.1 存储问题快速分流

```
存储问题
├─ 挂载失败
│  ├─ first-stage mount
│  ├─ vold volume mount
│  ├─ fsck / format / unsupported fs
│  └─ selinux / permission / fstab config
├─ 文件访问失败
│  ├─ 路径错误
│  ├─ userId/path remap 问题
│  ├─ scoped storage / appops / provider 限制
│  ├─ FUSE 权限拒绝
│  └─ CE 未解锁
├─ 安装/卸载/数据目录异常
│  ├─ installd createAppData 失败
│  ├─ UID/GID 不一致
│  ├─ quota / inode / ENOSPC
│  ├─ selinux restorecon 问题
│  └─ PMS 与 installd 状态不一致
├─ 空间统计异常
│  ├─ quota 统计口径
│  ├─ media / cache / app data 归类差异
│  ├─ snapshot / reserved block
│  └─ 多用户空间叠加
└─ I/O 性能差
   ├─ FUSE 开销
   ├─ 小文件频繁创建
   ├─ fsync / rename / metadata update
   ├─ storage device 慢
   └─ trim / checkpoint / encryption overhead
```

------


<!-- source: 30-15.md -->

# 15. 典型异常模式库

以下为 Storage 分析中常见异常模式。

### 15.1 挂载类

1. fstab 配置错误导致 userdata 未正确挂载
2. first-stage mount 失败导致后续 vold 行为异常
3. 文件系统类型不匹配导致 external volume unmountable
4. 块设备热插拔事件未到达 vold
5. 分区表异常导致 Disk 可见但 Volume 不生成
6. UUID/label 解析失败导致卷信息不完整
7. fsck 失败导致卷只读或拒绝挂载
8. SELinux 限制导致 mount helper 执行失败
9. adoptable storage metadata 损坏导致卷不可用
10. 用户切换后 emulated volume 状态未同步

### 15.2 权限与可见性类

1. App 对 `/storage/emulated/0` 有路径但无实际读写权限
2. 运行时权限已授予，但 FUSE / AppOps 仍拒绝访问
3. 文件存在于 `/data/media`，但 MediaStore 未索引
4. shell 能访问，App 不能访问
5. system uid 可访问，third-party app 不可访问
6. work profile 与主用户路径混淆
7. CE 未解锁导致目录存在但打开失败
8. SAF 可访问而直接 File API 不可访问
9. `MANAGE_EXTERNAL_STORAGE` 行为与预期不一致
10. app-specific external dir 可用，共享目录不可用

### 15.3 安装与数据目录类

1. installd 创建目录失败返回 errno
2. appId / uid 不一致导致目录权限异常
3. 卸载后 CE/DE 数据残留
4. 用户删除后 profile 目录未清理
5. code_cache / oat / profiles 残留导致空间异常
6. restorecon 未执行导致后续访问失败
7. PMS 认为安装成功，但 app data 创建不完整
8. clone/profile user 下目录复用错误
9. migrateAppData 中断导致数据不一致
10. quota / inode 耗尽导致 createAppData 失败

### 15.4 容量统计类

1. Settings 中“其他”空间异常膨胀
2. `du` 与系统设置显示容量差异极大
3. 多用户共享媒体目录统计口径误解
4. cache 与 app data 分类不一致
5. quota 延迟更新导致统计滞后
6. snapshot / checkpoint 占用未计入常规目录扫描
7. reserved block 导致“明明还有空间却写入失败”
8. tiny files 数量巨大导致 inode 耗尽
9. 媒体缩略图、数据库、索引缓存导致隐性膨胀
10. log / tombstone / trace 文件异常占用空间

### 15.5 性能类

1. FUSE 带来高频 open/read/write 开销
2. 小文件密集创建导致 metadata 开销大
3. fsync 频繁导致写入抖动
4. rename/atomic write 模式导致 UI 卡顿
5. MediaScanner / MediaProvider 大量扫描影响 I/O
6. 后台 trim / idle-maint 影响前台性能
7. 大目录遍历导致 Binder/Provider 侧延迟
8. 外部 SD 卡随机 I/O 性能差
9. 加密、quota、selinux 检查叠加开销
10. 安装/解压/编译时 data 分区写放大显著

------


<!-- source: 34-163.md -->

# 16.3 应用安装数据目录异常分析模板

需要回答：

1. PMS 调用了哪个 Installer 接口？
2. installd 是否成功创建 DE/CE 路径？
3. 目录 mode/owner/context 是否正确？
4. 用户切换/多用户路径是否正确？
5. quota/inode/space 是否充足？


<!-- source: 37-17.md -->

# 17. 分析结论撰写规范

输出结论时必须遵循以下格式：

### 17.1 结论格式

- **现象**：用户看到的问题
- **直接原因**：触发异常的直接条件
- **根本原因**：设计、配置、状态、权限或源码逻辑中的根因
- **证据链**：源码位置 + 状态 + 日志 + 路径映射 + 系统观测
- **影响范围**：是否影响单应用、单用户、全部用户、全部卷
- **修复建议**：短期 workaround + 长期代码修复
- **回归风险**：权限、兼容性、性能、数据一致性风险

### 17.2 禁止输出

禁止只给出如下空泛表述：

- “大概率是权限问题”
- “看起来像 vold 问题”
- “可能是系统限制”
- “建议检查日志”

必须明确指出：

- 哪个权限
- 哪个服务
- 哪个方法
- 哪个状态
- 哪个路径映射
- 哪个版本行为差异

------


<!-- source: 38-18.md -->

# 18. 技能执行提示词模板

以下模板可作为该技能的标准执行 Prompt。

```
你现在是 AOSP Storage 源码分析专家。请对我提供的 Android 存储问题进行系统级分析。

分析要求：
1. 先给出存储栈整体架构与问题所属层次。
2. 给出完整调用链：App / Framework / SystemServer / vold / installd / fs_mgr / Kernel。
3. 明确关键对象：VolumeInfo、StorageVolume、Disk、User、CE/DE、路径映射、UID/GID。
4. 结合源码解释关键方法、状态流转、权限与路径映射逻辑。
5. 必须输出：
   - 架构图
   - 时序图
   - 关键源码入口
   - 根因分析
   - 验证方法
   - 修复建议
6. 若问题涉及文件不可见、访问失败、挂载失败、容量异常、I/O 慢，必须明确指出问题发生层及证据链。
7. 不允许只给经验判断，必须给源码与状态依据。
```

------
