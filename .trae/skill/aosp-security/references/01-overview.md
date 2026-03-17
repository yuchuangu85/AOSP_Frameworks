# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Security Analysis Expert


<!-- source: 08-7.md -->

# 7. 标准分析流程

------

### 第一步：定义安全问题

必须明确：

- 是权限问题、SELinux 问题、签名问题、启动链问题，还是密钥/认证问题
- 失败点出现在：
  - 编译期
  - 安装期
  - 启动期
  - 运行期
  - 调用时
  - 数据访问时

输出：

- 问题描述
- 影响范围
- 涉及进程/服务/模块
- 初步怀疑点

------

### 第二步：确定安全边界

必须回答：

- 谁在访问谁
- 跨了哪些边界
- 谁是 caller，谁是 callee
- 是否发生身份切换
- 是否存在多重校验

输出：

- 主体 subject
- 客体 object
- 所处层级
- 边界图

------

### 第三步：构建完整调用链

要求构建：

- 入口 API
- Binder / JNI / native / HAL / kernel 传递链
- 每个关键校验点
- 正常路径与拒绝路径

输出：

- 调用链列表
- 每层职责
- 关键分支判断

------

### 第四步：定位真实判定点

关注：

- `enforce*`
- `check*Permission`
- `checkPackage`
- `noteOp/startOp`
- `uid == Process.SYSTEM_UID` 等特判
- `UserHandle`
- `clearCallingIdentity`
- SELinux allow/neverallow
- Key authorization / auth bound policy
- 签名比对逻辑

输出：

- 真正决定结果的代码位置
- 判定条件
- 输入参数
- 分支逻辑

------

### 第五步：结合运行时证据

必须结合以下至少一种或多种证据：

- `logcat`
- `dumpsys package`
- `dumpsys activity service <service>`
- `dumpsys appops`
- `pm list permissions`
- `cmd appops`
- `adb shell ps -AZ`
- `adb shell ls -Z`
- `avc: denied`
- keystore2 / gatekeeper / vold 日志
- verified boot 状态输出
- 安装失败日志
- trace / bugreport

------

### 第六步：得出根因与修复路径

输出必须包括：

- 根因分类
- 证据链
- 修复方案
- 修复影响面
- 安全风险评估

------


<!-- source: 15-14.md -->

# 14. 典型异常模式库

以下异常模式在分析中要优先识别。

### 14.1 权限类

- Manifest 声明了但未真正授予
- runtime 权限已授予但被 AppOps 拒绝
- signature 权限因签名不匹配失效
- privileged 权限因白名单缺失失效
- 调用时 packageName 与 uid 不匹配
- 跨用户访问未显式授权
- targetSdk 导致旧行为不再兼容

### 14.2 Binder 类

- 服务导出但未校验调用者
- clearCallingIdentity 范围过大
- 内部 helper 假设 caller 已校验但实际没有
- 对 shell/system 特判过宽
- 用户态参数可伪造而未绑定 uid

### 14.3 SELinux 类

- 对象 label 错误
- 进程 domain 错误
- 违反 Treble 边界
- 试图通过粗暴 allow 掩盖架构问题
- neverallow 被命中
- 访问方式不合理，应改为中转服务

### 14.4 签名 / 安装类

- 更新安装签名链不兼容
- priv-app 放置位置错误
- 白名单缺失
- sharedUserId 历史包袱导致冲突
- platform key 使用预期错误

### 14.5 Keystore / 认证类

- key policy 与实际认证能力不匹配
- biometric enrolled 状态变化导致 key invalidated
- 用户未解锁 CE 数据不可访问
- StrongBox 不支持导致 fallback 行为差异
- auth token 过期

### 14.6 Verified Boot / 数据保护类

- bootloader unlock 导致信任状态变化
- 分区篡改触发校验失败
- dm-verity 异常导致只读/启动失败
- DE/CE 访问时机混淆导致“数据丢失”假象

------
