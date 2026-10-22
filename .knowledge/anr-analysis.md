---
schema_version: 2
id: anr-analysis
title: "ANR 触发与超时机制"
summary: "输入、广播、Service 与 Provider 四类 ANR 的超时投放与回收路径。"
module: platform
---
# ANR 触发与超时机制

## Triggers

### Summary

ANR 由输入分发、广播、Service 与 ContentProvider 四类超时触发，各自有不同的计时起点。

### Detail

输入事件在 InputDispatcher 侧通过 pending input 监控；广播由 AMS 在
BroadcastQueue 中记录 timeout；Service 在 ActiveServices 里按前台后台区分超时；
Provider 在启动完成后由 AMS 监控。计时起点、时长与是否弹窗取决于组件状态。

## Timeout path

### Summary

超时到达后由系统服务收集进程状态并交给 system_server 决策，过程不可被应用直接取消。

### Detail

超时投放后，系统收集 CPU 使用、主线程堆栈与锁信息并写入 traces 与 dropbox，
随后由 ActivityManagerService 决定是否弹出无响应对话框。应用侧只能通过
避免主线程阻塞来规避，不能拦截或取消该过程。

## Diagnosis

### Summary

ANR 日志分析应从主线程堆栈、锁竞争与系统负载三方面交叉验证。

### Detail

traces 文件给出主线程与关键线程堆栈，日志给出事件序列与 CPU 统计。
单一日志片段不足以证明因果，需要与源码路径和运行时状态一起核对。

## Limits

### Summary

本文档描述机制与排查方向，不代表任何具体线上问题的根因结论。

### Detail

不同 Android 版本与厂商定制会改变超时阈值和日志格式，结论必须绑定到
具体 Revision 与设备证据。
