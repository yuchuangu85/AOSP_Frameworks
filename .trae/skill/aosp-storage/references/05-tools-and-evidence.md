# 工具与证据
<!-- source: 26-14.md -->

# 14. 常用日志与观测点


<!-- source: 28-142-dumpsys-shell.md -->

# 14.2 dumpsys / shell 命令

- `dumpsys mount`
- `dumpsys storage`
- `dumpsys package <package>`
- `sm list-volumes all`
- `sm list-disks`
- `sm list-users`
- `mount`
- `cat /proc/mounts`
- `cat /proc/self/mountinfo`
- `df -h`
- `du -sh`
- `ls -lZ`
- `getfattr` / `stat`
- `id`
- `logcat -b all`
- `dmesg`
- `lsof`（设备支持时）
- `cmd media_session` / `content query`（按问题场景）


<!-- source: 39-19.md -->

# 19. 推荐输出风格

推荐采用以下风格：

- 专业、严谨、工程化
- 先总后分
- 结论先行，但必须附证据链
- 图示优先
- 方法名、类名、路径、状态名、errno、UID/GID、SELinux context 尽量精确
- 对 Android 版本差异保持敏感

------
