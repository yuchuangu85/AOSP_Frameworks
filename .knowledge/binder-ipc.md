---
schema_version: 2
id: binder-ipc
title: "Binder IPC 机制"
summary: "Android Binder 的分层实现、一次拷贝的数据通路与线程池调度约束。"
module: platform
---
# Binder IPC 机制

## Architecture

### Summary

Binder 是 Android 的核心 IPC 机制，由内核驱动、Native 运行时与 Java Framework 三层组成。

### Detail

Java 层通过 AIDL 生成的 Stub/Proxy 暴露接口，Native 层由 libbinder 提供
BnInterface/BpInterface 与 Parcel 序列化，内核层的 binder 驱动负责内存映射与
线程调度。Client 与 Server 通过 ServiceManager 完成句柄查询。

## Data path

### Summary

一次跨进程调用只发生一次数据拷贝，接收方线程从内核缓冲区直接映射读取。

### Detail

发送方把 Parcel 写入内核分配的物理页，驱动把该页映射进接收进程地址空间，
接收方读取时不再复制。Transaction 大小受驱动缓冲区与进程 mmap 上限约束，
超限会返回失败而不是静默截断。

## Threading

### Summary

Binder 线程池按需扩容，嵌套调用与线程耗尽会表现为阻塞而不是错误。

### Detail

每个进程默认最多 16 个 binder 线程，主线程也参与处理。同步调用会占用调用线程，
在 Server 端发生同步回调时可能出现线程耗尽，需要在设计上避免长耗时操作。

## Limits

### Summary

Binder 的分析结论基于源码与日志证据，不构成时序或性能保证。

### Detail

本文档描述机制与约束，不证明具体设备上的调度顺序、延迟或稳定性。
