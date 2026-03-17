# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Storage 源码分析技能


<!-- source: 12-92-emulated-storage-fuse-sdcard.md -->

# 9.2 Emulated Storage / FUSE / sdcard 模型

必须分析以下问题：

1. 为什么 App 访问 `/storage/emulated/0/`，真实数据却在 `/data/media/0/`
2. 为什么不同 App 对同一路径可见性不同
3. 为什么 shell/system/app 看到的路径和权限不同
4. 为什么媒体文件在文件系统存在，但图库或文件管理器不可见
5. 为什么 Android 新版本对直接路径访问限制更强

分析要点：

- path remap
- user isolation
- FUSE 层检查
- MediaProvider 索引
- appops / permission
- scoped storage policy
- open / rename / delete / scan 的行为差异

------


<!-- source: 15-95-storage-scoped-storage.md -->

# 9.5 Storage 权限与 Scoped Storage 模型

必须区分：

- legacy external storage
- scoped storage
- MANAGE_EXTERNAL_STORAGE
- READ_MEDIA_IMAGES / VIDEO / AUDIO
- SAF
- MediaStore
- app-specific external dirs
- shared collections

必须说明：

- 权限检查发生在 API、framework、provider、FUSE、内核中的哪些层
- 为什么“文件存在但访问报错”
- 为什么“路径访问失败但 MediaStore 能读”

------


<!-- source: 33-162.md -->

# 16.2 文件访问失败分析模板

需要回答：

1. 调用方 UID/userId 是谁？
2. 访问的是 direct path 还是 MediaStore/SAF？
3. 对应路径真实映射到哪里？
4. 权限检查在哪一层失败？
5. 是 CE 未解锁、scoped storage、FUSE、SELinux、还是文件系统权限问题？


<!-- source: 40-20.md -->

# 20. 版本适配建议

该技能默认面向现代 Android 版本（Android 12+ 到 AOSP 16 分析思路同样适用），但在执行时应显式检查以下差异：

1. **sdcard / FUSE 实现差异**
2. **scoped storage 行为差异**
3. **MediaProvider 中介逻辑演进**
4. **vold 命令接口与 volume state 映射变化**
5. **fscrypt / metadata encryption / adoptable storage 细节变化**
6. **PackageManager / installd 接口细节变化**
7. **apex / modularization 对路径与权限模型的影响**

------


<!-- source: 41-21.md -->

# 21. 交付标准

当使用本技能完成一次分析时，交付结果至少应达到以下标准：

- 能解释问题在存储栈的哪一层发生
- 能画出从入口到落点的跨层调用链
- 能定位关键源码文件与方法
- 能说明路径映射、权限、用户隔离、加密状态之间关系
- 能给出验证方式
- 能给出修复建议
- 能经得起二次追问和源码复核

------
