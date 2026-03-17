# 工具与证据
<!-- source: 13-12.md -->

# 12. 输出格式规范

执行本 Skill 时，建议按如下结构输出：

### 1. 问题定义

- 目标问题
- 涉及模块
- 分析范围

### 2. 安全架构总览

- 分层结构
- 安全边界
- 信任关系

### 3. 核心调用链

- 入口
- 关键校验点
- 跨层链路

### 4. 关键源码解析

- 类
- 方法
- 数据结构
- 判定逻辑

### 5. 时序图 / 流程图

- 正常路径
- 失败路径
- 异常路径

### 6. 运行时证据

- 日志
- dumpsys
- 配置
- 策略
- trace

### 7. 根因分析

- 根因分类
- 证据链闭环

### 8. 修复与加固建议

- 推荐方案
- 最小改动方案
- 风险较高方案
- 安全影响评估

### 9. 延伸建议

- 关联模块
- 后续建议排查方向
- 架构优化建议

------


<!-- source: 14-13.md -->

# 13. 常用命令与证据收集清单

### 13.1 权限 / 包信息

```
adb shell dumpsys package <package>
adb shell pm list permissions -d -g
adb shell cmd package query-permissions
adb shell cmd appops get <package>
adb shell cmd appops query-op <op>
```

### 13.2 进程 / SELinux / 文件上下文

```
adb shell ps -AZ
adb shell ls -Z /data/user/0/
adb shell ls -Z /system/priv-app/
adb shell getenforce
adb shell dmesg | grep avc
adb logcat | grep avc
```

### 13.3 用户 / profile / 运行态

```
adb shell dumpsys user
adb shell am get-current-user
adb shell dumpsys activity processes
adb shell dumpsys activity services
```

### 13.4 安装 / 签名 / 系统属性

```
adb shell pm path <package>
adb shell dumpsys package --check-permission <permission> <package>
adb shell getprop | grep -i verified
adb shell getprop ro.boot.verifiedbootstate
adb shell getprop ro.boot.vbmeta.device_state
```

### 13.5 Keystore / 生物认证 / 锁屏

```
adb shell dumpsys lock_settings
adb shell dumpsys biometric
adb logcat | grep -i keystore
adb logcat | grep -i keymint
adb logcat | grep -i gatekeeper
```

------


<!-- source: 20-19.md -->

# 19. 最终要求

本 Skill 的最终目标不是“泛泛讲 Android 安全”，而是：

- 能从源码中还原真实安全控制链
- 能从日志与配置中闭环验证结论
- 能识别安全边界与越权风险
- 能对权限、SELinux、签名、启动链、密钥管理等问题给出工程级结论
- 能输出可直接用于排障、评审、设计和加固的正式分析结果

在任何具体分析任务中，始终以“**源码真实行为 + 运行时证据 + 安全设计意图**”三者闭环作为最终标准。
