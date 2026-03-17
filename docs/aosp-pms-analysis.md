# PMS 源码分析

## 一、问题定义与范围
- 范围：PackageManagerService、权限与扫描入口完整，可完成安装解析、权限授予与包状态分析。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- scan/install request -> PackageManagerService -> Settings/permission state -> package visibility/result
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- PMS 通过集中式包数据库和权限决策保持多用户一致性，代价是首次扫描与升级路径复杂。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Installer --> PMS
    PMS --> Settings
    Settings --> Permission
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Installer
    participant PMS
    participant Settings
    participant Permission
    Installer->>PMS: 进入主链
    PMS->>Settings: 状态推进
    Settings->>Permission: 结果提交
```

## 六、关键代码详细分析
- PackageManagerService.main/scanPackageOnlyLI 是包进入系统视图的关键入口。
- 解析结果会写入 Settings 与权限状态，形成后续启动和授权的基础。
- 安装异常通常要同时看存储、签名、权限模型，而不是只看单个异常码。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/services/core/java/com/android/server/pm/PackageManagerService.java`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-pms` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
