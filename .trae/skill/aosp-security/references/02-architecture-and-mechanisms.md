# 架构与核心机制
<!-- source: 02-1.md -->

# 1. 适用范围

本 Skill 用于 **Android Open Source Project（AOSP）安全体系** 的系统级分析，覆盖以下重点方向：

- Android 安全总体架构
- Linux UID/GID + App Sandbox 隔离机制
- Android 权限模型（install-time / runtime / appop / role）
- Binder 调用链中的身份与权限校验
- SELinux 策略、domain/type、neverallow、te 文件分析
- 包签名、共享 UID、签名权限、签名链与安装校验
- Verified Boot / dm-verity / AVB 启动信任链
- Keystore / KeyMint / Gatekeeper / Biometric 认证链路
- System Server 与 Native Daemon 安全边界
- 用户数据保护、FBE/FDE、CE/DE 存储模型
- 隐私访问控制（位置信息、剪贴板、传感器、后台权限等）
- 安全事件根因定位、安全加固建议输出

适用于以下任务：

- 分析某个系统行为为何被拒绝
- 分析某权限为何申请成功/失败
- 分析某个 Binder 服务的安全边界
- 分析 SELinux deny 的根因与修复策略
- 分析应用安装、签名校验、共享 UID 相关机制
- 分析启动信任链与系统完整性校验
- 分析 Keystore/KeyMint 密钥生成、认证与使用流程
- 分析系统安全设计思想、架构演进与风险面

---


<!-- source: 03-2.md -->

# 2. 目标输出

执行本 Skill 时，必须输出尽量完整、可验证、可复用的安全分析结果，至少包括：

1. **问题定义**
   - 用户要分析的安全主题/故障/源码点是什么
   - 对应的系统层级、进程、服务、模块边界是什么

2. **安全架构图**
   - 给出模块分层与信任边界
   - 标出主体（subject）、客体（object）、权限校验点、身份切换点

3. **关键调用链**
   - 从入口到校验点到结果输出的完整链路
   - 标出 Java / JNI / Native / Kernel / HAL / TEE 等边界

4. **核心源码分析**
   - 核心类、核心方法、关键数据结构、判定条件
   - 每一步校验的输入、输出、失败路径

5. **时序图 / 流程图**
   - 还原调用时序
   - 区分正常路径、拒绝路径、异常路径

6. **设计思想分析**
   - 该安全机制为何这样设计
   - 它防御什么攻击面
   - 它的边界、限制与副作用是什么

7. **证据链**
   - 对应源码文件
   - 对应日志/trace/dumpsys/命令输出
   - 对应策略文件/配置文件/清单文件

8. **根因结论**
   - 安全拒绝、越权失败、权限不足、策略冲突、签名不匹配等根因

9. **修复建议 / 加固建议**
   - 修复路径
   - 最小变更方案
   - 风险评估
   - 不推荐方案及其原因

---


<!-- source: 04-3.md -->

# 3. 分析原则

### 3.1 必须遵守的原则

- 不能脱离源码臆测
- 不能只看表层 API，必须追到真实校验点
- 不能只讲结论，必须给证据链
- 不能只分析成功路径，必须分析失败路径
- 不能只看 Framework，必须关注跨层边界
- 不能把“权限申请”误当成“权限生效”
- 不能忽略：
  - UID / userId / appId
  - packageName / signingInfo
  - pid / callingUid / callingPid
  - selinux domain / context
  - AppOps
  - user/profile/work profile
  - targetSdkVersion
  - platform API level

### 3.2 安全分析核心方法论

对任一安全问题，优先从以下 8 个维度分析：

1. **谁发起的**
   - 调用者进程
   - UID / PID / packageName
   - 是否跨用户 / 跨 profile

2. **访问了什么**
   - Service / Binder API / 文件 / 属性 / 设备 / provider / setting / socket

3. **校验发生在哪**
   - Java Framework
   - System Server
   - Native Service
   - SELinux
   - Kernel
   - TEE / KeyMint
   - 安装时校验
   - 启动时校验

4. **用了哪种身份**
   - 调用者原始身份
   - clearCallingIdentity 后身份
   - system_server 身份
   - root/shell/system/bluetooth/media 等特殊 UID

5. **校验规则是什么**
   - Manifest permission
   - runtime permission
   - appop
   - signature check
   - role check
   - package visibility
   - selinux allow/neverallow
   - AVB verification
   - key authorization policy

6. **失败点在哪里**
   - SecurityException
   - PERMISSION_DENIED
   - AppOps MODE_IGNORED / MODE_ERRORED
   - avc: denied
   - INSTALL_FAILED_xxx
   - keystore/keymint error
   - vold/fscrypt 拒绝

7. **系统为何这样设计**
   - 最小权限
   - 默认拒绝
   - 分层防御
   - 能力隔离
   - 签名信任
   - 数据最小暴露
   - 可信启动链

8. **如何修**
   - 配置修复
   - 策略修复
   - 签名/权限模型修复
   - 架构改造
   - 安全加固

---


<!-- source: 05-4-aosp-security.md -->

# 4. AOSP Security 总体架构

---

### 4.1 总体分层

```text
App Layer
  ├─ 普通应用
  ├─ 特权应用 priv-app
  ├─ 系统应用 system app
  └─ shell / adb / test app

Framework API Layer
  ├─ Context / PackageManager / Permission APIs
  ├─ AppOpsManager
  ├─ Biometric / Keystore / DevicePolicy / UserManager
  └─ 各类 manager 对外安全接口

System Server Services
  ├─ PackageManagerService
  ├─ PermissionManagerService
  ├─ AppOpsService
  ├─ ActivityManagerService
  ├─ UserManagerService
  ├─ DevicePolicyManagerService
  ├─ LockSettingsService
  ├─ ClipboardService / LocationManagerService / SensorPrivacyService
  └─ 其他系统服务中的权限校验点

Native Daemons / Services
  ├─ installd
  ├─ vold
  ├─ keystore2
  ├─ gatekeeperd
  ├─ statsd / netd / adbd / servicemanager
  └─ surfaceflinger / mediaserver 等 native 服务

HAL / Secure Components
  ├─ KeyMint HAL
  ├─ Gatekeeper HAL
  ├─ Biometrics HAL
  └─ 其他安全相关 HAL

Kernel / Security Subsystem
  ├─ Linux UID/GID sandbox
  ├─ SELinux LSM
  ├─ capabilities
  ├─ seccomp
  ├─ fscrypt / dm-verity
  └─ binder driver / file permissions

Boot Trust Chain
  ├─ Boot ROM
  ├─ Bootloader
  ├─ vbmeta / AVB
  ├─ partition verification
  └─ verified boot state
```

### 4.2 核心安全边界

AOSP 安全分析必须始终围绕以下边界展开：

- **应用进程边界**
- **UID 边界**
- **用户空间与内核空间边界**
- **普通应用与特权应用边界**
- **调用者与被调服务边界**
- **System Server 与 Native Daemon 边界**
- **Android OS 与 TEE / Secure Element 边界**
- **已验证镜像与未验证镜像边界**
- **不同 user/profile 间的数据边界**

------


<!-- source: 06-5.md -->

# 5. 重点安全子系统分析框架

------

### 5.1 应用沙箱（App Sandbox）

#### 分析目标

- 应用为什么默认互相隔离
- 共享数据、共享进程、共享 UID 需要什么条件
- 某应用为什么无法访问另一个应用的文件/数据/Provider/Service

#### 必查点

- AndroidManifest.xml
- package 安装分配的 UID / appId
- `/data/user/<userId>/<package>`
- Linux 文件权限
- SELinux context
- exported / permission / grantUriPermission / authorities

#### 核心分析链路

```
安装应用
  → PMS 分配 appId / uid
  → 创建 data 目录
  → 配置 SELinux label
  → 运行时进程以对应 uid 启动
  → 访问文件/IPC 时受 Linux + SELinux + framework 权限共同约束
```

#### 输出重点

- UID / GID 分配规则
- 多用户下 uid 计算模型
- sandbox 对文件、进程、Binder、Provider 的影响
- sharedUserId/signature 机制及风险

------

### 5.2 权限模型（Permission Model）

#### 分析目标

- 权限是如何声明、授予、校验、撤销的
- install permission、runtime permission、signature permission、privileged permission 的差异
- 为什么“Manifest 里声明了权限”但调用仍失败

#### 必查点

- `AndroidManifest.xml`
- `frameworks/base/core/res/AndroidManifest.xml`
- `PackageManagerService`
- `PermissionManagerService`
- `Context#enforceCallingOrSelfPermission`
- `AppOpsService`
- `PermissionChecker`
- 权限组、角色、白名单、privapp-permissions XML

#### 典型校验链

```
App 调用 API
  → Manager/Service API
  → enforceCallingPermission / checkPermission
  → PermissionManagerService / AMS / PMS / 具体服务
  → 有时追加 AppOps 检查
  → 成功 / 抛 SecurityException / 返回拒绝
```

#### 必须区分的权限类型

- normal
- dangerous(runtime)
- signature
- privileged
- development
- internal/system-only
- role-based access
- appop-gated permission

#### 输出重点

- 声明点
- 授权点
- 生效点
- 校验点
- targetSdk 对行为的影响
- 权限 + AppOps 叠加模型

------

### 5.3 AppOps 模型

#### 分析目标

- 为什么权限已经授予但操作仍被拦截
- 某隐私能力访问为什么是“静默失败”而不是直接抛异常

#### 核心认识

AppOps 是对“具体操作”的二次控制，通常位于权限校验之后，用于更细粒度地控制访问。

#### 常见场景

- 位置信息
- 摄像头
- 麦克风
- 剪贴板
- 后台运行
- 通知
- Usage Stats
- 精准/模糊位置
- 后台传感器访问

#### 分析链路

```
API 调用
  → permission check
  → noteOp / startOp / checkOp
  → AppOpsService
  → 根据 uid/package/op 计算 mode
  → allow / ignore / error / foreground only
```

#### 输出重点

- permission 与 appop 的关系
- mode 计算逻辑
- 前后台状态影响
- 用户设置项如何映射到 AppOps

------

### 5.4 Binder 安全模型

#### 分析目标

- Binder 服务如何识别调用者
- 为什么 system_server 可以替调用者执行但又要保留 caller identity
- clearCallingIdentity 为什么危险

#### 必查点

- `Binder.getCallingUid() / getCallingPid()`
- `checkCallingPermission()`
- `enforceCallingPermission()`
- `checkPackage() / isSameApp()`
- `clearCallingIdentity()/restoreCallingIdentity()`
- AIDL Service Stub 实现

#### 安全分析重点

1. API 入口是否校验调用者 UID/PID
2. 是否校验 packageName 与 callingUid 一致性
3. 是否存在 clearCallingIdentity 后越权访问
4. 是否对跨用户访问做了 `handleIncomingUser` / `enforceCrossUserPermission`
5. 是否有导出 Binder 接口未做防护

#### Binder 安全时序图

```
Client App
  → Binder transact
  → system_server Binder Stub
  → 读取 callingUid/callingPid
  → permission/signature/user/appop/package 校验
  → 必要时 clearCallingIdentity
  → 执行内部逻辑
  → restoreCallingIdentity
  → 返回结果
```

#### 输出重点

- 真实调用者身份
- 实际执行者身份
- 身份切换位置
- 校验前后顺序是否安全
- 越权窗口是否存在

------

### 5.5 SELinux 分析体系

#### 分析目标

- avc: denied 根因是什么
- 某服务/进程/文件/socket/property 为什么不能访问
- 应如何修 SELinux 策略

#### 必查点

- 源 domain
- 目标 type/class
- request 权限集合
- 当前进程 context
- 目标对象 context
- `.te` / `file_contexts` / `service_contexts` / `property_contexts`
- neverallow 规则
- 宏展开规则

#### 标准分析法

1. 识别 subject context
2. 识别 object context
3. 识别 class 与 perm
4. 查现有 allow 是否存在
5. 查 neverallow 是否阻断
6. 判断是否应该加 allow，还是架构设计本身有问题

#### 典型输出模板

```
subject: u:r:system_server:s0
object : u:object_r:vendor_file:s0
class  : file
perm   : read/open/getattr

结论：
- 当前访问由 system_server 发起
- 目标对象标记为 vendor_file
- 现有策略未允许 system_server 直接读取该类文件
- 若强行加 allow，需要检查是否违反 Treble/neverallow 边界
- 更合理方案可能是通过 vendor HAL / binder service 中转访问
```

#### 修复策略优先级

1. 优先修架构边界
2. 优先修 label 错误
3. 再考虑补 allow
4. 最后才评估例外策略

#### 输出重点

- deny 原因
- 正确责任域
- 正确对象类型
- 是否违反分层
- 最小策略修复方案
- neverallow 风险

------

### 5.6 包签名、安装与信任模型

#### 分析目标

- APK 为什么安装失败
- 为什么声明 signature 权限却拿不到
- 为什么 sharedUserId/共享签名失效
- 平台签名 app 的能力边界在哪里

#### 必查点

- APK signing info
- `PackageManagerService`
- `SigningDetails`
- 共享 UID / sharedUserId
- `compareSignatures`
- priv-app 目录
- privapp-permissions 白名单
- 预装分区位置（system/product/vendor/system_ext）

#### 核心分析链

```
安装 APK
  → 解析 manifest
  → 校验签名
  → 校验更新时签名兼容性
  → 分配权限
  → 校验 sharedUser / shared signing
  → 校验 privapp 白名单
  → 完成安装
```

#### 输出重点

- 签名匹配规则
- platform key 影响范围
- 签名权限授予条件
- 预装位置与 privileged 权限关系
- 更新安装中的签名旋转/兼容链

------

### 5.7 Verified Boot / AVB 启动信任链

#### 分析目标

- Android 启动链如何保证系统完整性
- bootloader、vbmeta、partition verification 如何串联
- 为什么设备处于 orange/yellow/green state
- 自定义镜像为何导致验证失败

#### 分析对象

- Boot ROM
- Bootloader
- vbmeta
- boot / init_boot / system / vendor / product 等分区
- dm-verity
- boot state
- rollback protection

#### 时序图

```
Boot ROM
  → 验证 bootloader
  → bootloader 验证 vbmeta
  → vbmeta 验证各分区哈希/descriptor
  → kernel 启动
  → dm-verity 保障运行时 block 完整性
  → 系统读取 verified boot state
```

#### 输出重点

- 信任根在哪里
- 校验链如何传播
- 分区完整性如何保证
- 解锁 bootloader 的安全影响
- 调试机 / 量产机的安全差异

------

### 5.8 Keystore / KeyMint / Gatekeeper / Biometric

#### 分析目标

- 密钥如何安全生成、存储、使用
- 用户认证如何绑定到密钥使用策略
- 强认证、设备锁屏、biometric 与密钥授权的关系

#### 分析对象

- Java Keystore API
- keystore2
- KeyMint HAL
- Gatekeeper
- BiometricService / AuthService
- LockSettingsService
- TEE / StrongBox

#### 关键问题

- 密钥是否可导出
- 是否绑定用户认证
- 认证有效期多长
- 是否要求设备解锁
- 是否硬件支持
- 失败发生在 framework、keystore2 还是 keymint

#### 典型流程

```
App 请求生成密钥
  → Android Keystore API
  → keystore2
  → KeyMint HAL
  → 生成 key blob / authorization set
  → 存储并返回句柄

App 使用密钥
  → keystore2 校验 key policy
  → 如需认证，联动 Gatekeeper/Biometric
  → KeyMint 执行 crypto operation
  → 返回结果
```

#### 输出重点

- KeyGenParameterSpec 到底映射到哪些授权
- hardware-backed / strongbox-backed 的差异
- biometric 与 device credential 的约束
- 认证失效、密钥失效、永久失效的根因

------

### 5.9 用户数据保护：FBE / CE / DE

#### 分析目标

- 为什么开机后部分数据可访问，部分不可访问
- Direct Boot 模式下哪些数据可用
- 用户解锁后哪些目录才可访问

#### 核心概念

- **DE (Device Encrypted)**：设备启动后即可访问
- **CE (Credential Encrypted)**：用户解锁后可访问

#### 分析链路

```
开机
  → vold 初始化加密存储
  → DE 数据可挂载
  → 用户输入凭据解锁
  → CE key 解封装
  → CE 目录可访问
```

#### 输出重点

- 应用数据存储位置
- Direct Boot aware 组件行为
- 解锁前后访问差异
- vold / fscrypt / user key 生命周期

------

### 5.10 隐私访问控制

#### 关注对象

- Location
- Camera
- Microphone
- Clipboard
- Sensors
- Notifications
- Foreground service access
- Package visibility
- Background activity launch
- Overlay / accessibility 高敏能力

#### 输出重点

- 哪些能力由 Permission 控制
- 哪些能力由 AppOps 控制
- 哪些由前后台态控制
- 哪些由系统弹窗/用户确认控制
- targetSdkVersion 与行为差异

------


<!-- source: 07-6-aosp-security.md -->

# 6. AOSP Security 源码分析入口索引

以下为进行安全分析时的常见入口索引。

### 6.1 Framework / System Server 常见入口

- `frameworks/base/services/core/java/com/android/server/pm/PackageManagerService.java`
- `frameworks/base/services/core/java/com/android/server/pm/permission/PermissionManagerService.java`
- `frameworks/base/services/core/java/com/android/server/appop/AppOpsService.java`
- `frameworks/base/core/java/android/content/Context.java`
- `frameworks/base/core/java/android/os/Binder.java`
- `frameworks/base/services/core/java/com/android/server/am/ActivityManagerService.java`
- `frameworks/base/services/core/java/com/android/server/clipboard/ClipboardService.java`
- `frameworks/base/services/core/java/com/android/server/location/*`
- `frameworks/base/services/core/java/com/android/server/biometrics/*`
- `frameworks/base/services/core/java/com/android/server/locksettings/LockSettingsService.java`
- `frameworks/base/services/devicepolicy/java/com/android/server/devicepolicy/*`

### 6.2 Native / Daemon 入口

- `system/sepolicy/`
- `system/security/keystore2/`
- `system/vold/`
- `system/core/init/`
- `system/core/adb/`
- `frameworks/native/cmds/servicemanager/`
- `system/gatekeeper/`
- `hardware/interfaces/keymint/`
- `hardware/interfaces/gatekeeper/`

### 6.3 启动信任链相关

- `system/core/fs_mgr/`
- `external/avb/`
- bootloader 相关厂商实现
- vbmeta / avb descriptors 相关工具与定义

### 6.4 权限/配置文件

- `frameworks/base/core/res/AndroidManifest.xml`
- `packages/modules/Permission/`
- `etc/permissions/*.xml`
- `privapp-permissions*.xml`
- `platform.xml`

------


<!-- source: 09-8.md -->

# 8. 架构图模板

------

### 8.1 权限 + AppOps 双层校验图

```
App
  → Framework API
    → System Service
      → Permission Check
      → AppOps Check
      → User/Profile Check
      → Package/UID Check
      → 执行业务逻辑
```

------

### 8.2 Binder 身份模型图

```
Caller App(uid A)
  → Binder transact
  → system_server service
     ├─ Binder.getCallingUid() == A
     ├─ 权限校验
     ├─ package/uid 一致性校验
     ├─ clearCallingIdentity() [可选]
     ├─ 以 system_server 身份访问内部资源
     └─ restoreCallingIdentity()
```

------

### 8.3 SELinux 访问控制图

```
Subject(domain=context A)
  → access object(type=context B, class C, perm P)
  → sepolicy allow ?
     ├─ yes → 继续
     └─ no  → avc denied
```

------

### 8.4 启动信任链图

```
ROM Root of Trust
  → Bootloader
  → vbmeta
  → boot/system/vendor/product verification
  → kernel + dm-verity
  → Android userspace
```

------

### 8.5 Keystore 认证图

```
App
  → Android Keystore API
  → keystore2
  → KeyMint
  → [需要认证时]
      Gatekeeper / Biometric
  → 执行签名/解密/加密
```

------


<!-- source: 11-10.md -->

# 10. 常见问题分析模板

------

### 10.1 “Manifest 已声明权限，但调用仍失败”

从以下顺序分析：

1. 权限是否真被授予
2. runtime 权限是否允许
3. appop 是否被拒绝
4. 调用 packageName 与 uid 是否匹配
5. 是否要求 signature/privileged
6. 是否要求前台态
7. 是否要求特定 user/profile
8. 是否还有 SELinux / native 层拒绝

------

### 10.2 “avc: denied 怎么修”

按以下顺序分析：

1. subject/object/class/perm 是否识别正确
2. label 是否打错
3. 访问主体是否合理
4. 分层是否被破坏
5. allow 是否违反 neverallow
6. 是否应该改架构而不是加策略
7. 是否存在更小权限集合的修复方式

------

### 10.3 “系统应用为什么拿不到高权限”

排查：

1. 是否只是 system app 而不是 priv-app
2. 是否在正确分区
3. 是否 platform 签名
4. 是否在 privapp-permissions 白名单
5. 权限是否为 signature/privileged/internal
6. targetSdk / user build 限制是否影响

------

### 10.4 “Binder 接口疑似越权”

排查：

1. 是否校验 callingUid/callingPid
2. 是否校验 package/uid 绑定
3. 是否校验跨用户访问
4. 是否 clearCallingIdentity 使用不当
5. 是否内部调用绕过外部校验
6. 是否对 shell/root/system 存在过宽特判

------

### 10.5 “Keystore 密钥使用失败”

排查：

1. key alias 是否存在
2. key 是否硬件支持
3. key 授权策略是什么
4. 是否要求用户认证
5. biometric/device credential 是否满足
6. auth timeout 是否过期
7. key 是否被 invalidated
8. keystore2 / keymint 返回的真实错误码是什么

------

### 10.6 “安装失败 / 签名不匹配”

排查：

1. 安装包签名信息
2. 更新包与已装包签名兼容性
3. sharedUserId 是否冲突
4. 权限白名单是否满足
5. packageName 是否重复冲突
6. target / sdk / abi / apex 等兼容性问题

------


<!-- source: 12-11.md -->

# 11. 安全设计思想输出要求

进行源码分析时，不能只停留在“代码怎么走”，还必须解释“为什么这样设计”。

每次分析都应尽量回答：

- 为什么 Android 要采用多层校验，而不是单点校验
- 为什么权限模型与 AppOps 分离
- 为什么 System Server 仍要保留 caller identity
- 为什么 SELinux 要默认拒绝
- 为什么 privileged 权限需要额外白名单
- 为什么密钥授权要绑定到 TEE/StrongBox
- 为什么用户数据要区分 CE/DE
- 为什么 Verified Boot 必须建立完整信任链
- 为什么很多能力需要前台态、显式用户交互或可见性约束

------


<!-- source: 16-15.md -->

# 15. 专家增强要求

执行本 Skill 时，优先达到以下“专家级输出”标准：

### 15.1 必须做跨层分析

不能只停留在单层源码，必须尽量建立：

- App → Framework → System Server → Native → HAL → Kernel
- Package / Permission / AppOps / SELinux 的联合判定链
- Boot → Verified Boot → Userspace 安全状态传播链
- App → Keystore2 → KeyMint → TEE 的密钥链路

### 15.2 必须识别真实强制点

分析时必须明确：

- 真正 enforce 的位置
- 真正 deny 的位置
- 真正做身份绑定的位置
- 真正做安全决策的位置

### 15.3 必须区分“逻辑限制”和“安全限制”

例如：

- 是业务逻辑不允许
- 还是权限系统拒绝
- 还是 AppOps 静默拦截
- 还是 SELinux 拒绝
- 还是签名/安装阶段未满足前提
- 还是启动信任链不通过
- 还是密钥授权策略不满足

### 15.4 必须给出最小修复面

修复建议必须给出：

- 最小代码改动点
- 最小策略改动点
- 是否影响 CTS/VTS
- 是否影响系统攻击面
- 是否破坏既有边界

------


<!-- source: 18-17-skill.md -->

# 17. Skill 调用提示词模板

可用于上层 orchestrator 调用本 Skill：

```
请作为 AOSP Security Analysis Expert，对以下 Android 安全问题进行源码级分析：

【问题描述】
<填写问题>

【分析目标】
1. 给出完整安全架构与信任边界
2. 构建从入口到真实判定点的调用链
3. 标出权限、AppOps、Binder、SELinux、签名、用户态、启动链或 Keystore 等关键校验点
4. 结合源码、配置、日志和运行时证据给出根因
5. 输出修复方案、最小改动点与安全风险评估

【输出要求】
- 必须包含架构图
- 必须包含时序图
- 必须包含关键源码解释
- 必须包含失败路径分析
- 必须包含设计思想分析
- 必须包含证据链与结论闭环
```

------


<!-- source: 19-18.md -->

# 18. 质量检查清单

在输出最终分析前，检查是否满足：

-  是否明确了调用者与被访问对象
-  是否明确了安全边界
-  是否给出了完整调用链
-  是否定位到真实判定点
-  是否分析了失败路径
-  是否结合了运行时证据
-  是否区分了 permission / appop / selinux / signature / policy / boot trust / keystore 问题
-  是否给出架构图
-  是否给出时序图
-  是否解释了设计思想
-  是否给出最小修复方案
-  是否评估了安全风险与副作用

------
