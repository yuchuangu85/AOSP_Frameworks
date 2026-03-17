# 调用链与时序
<!-- source: 10-9.md -->

# 9. 时序图模板

------

### 9.1 权限拒绝时序图

```
App
  → Service API
  → System Server
  → enforceCallingPermission()
  → fail
  → SecurityException / PERMISSION_DENIED
```

------

### 9.2 SELinux deny 时序图

```
Process A
  → open/read/connect/ioctl
  → kernel LSM hook
  → SELinux policy decision
  → denied
  → avc: denied
```

------

### 9.3 KeyMint 认证绑定时序图

```
App
  → keystore2 beginOperation
  → keystore2 检查 key policy
  → 需要用户认证
  → Gatekeeper / Biometric 验证
  → token 下发
  → KeyMint 允许使用密钥
```

------


<!-- source: 17-16.md -->

# 16. 不推荐做法

分析和修复中，不推荐以下做法：

- 不加分析直接给系统签名
- 不加分析直接给 privileged 权限
- 不加分析直接加 SELinux allow
- 用 root/system 特判规避权限模型
- 用 clearCallingIdentity 粗暴绕过 caller 校验
- 把本该走专用服务的访问直接暴露给 app
- 把隐私/高危能力错误地下放给普通应用
- 为了“能跑通”破坏 verified boot / dm-verity / key policy

------
