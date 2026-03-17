# 输出模板与检查清单
<!-- source: 19-diagram-output-templates.md -->

# Diagram Output Templates

在输出时，优先使用如下图类型。

### 窗口系统架构图模板
```mermaid
flowchart TD
    App[App / Activity / ViewRootImpl]
    WMS[WindowManagerService]
    Shell[Shell / Transition]
    Display[DisplayContent / Task / ActivityRecord / WindowState]
    Insets[Insets / IME]
    SF[SurfaceControl / SurfaceFlinger]
    Input[Input Focus / Target]

    App --> WMS
    WMS --> Display
    Display --> Insets
    Display --> Input
    WMS --> Shell
    WMS --> SF
```

### 核心对象关系图模板

```mermaid
flowchart LR
    DC[DisplayContent]
    Task[Task]
    AR[ActivityRecord]
    WS[WindowState]
    FT[FocusedWindow]
    IME[IME Target]
    IN[InsetsSource]

    DC --> Task
    Task --> AR
    AR --> WS
    DC --> FT
    WS --> IME
    WS --> IN
```

### 正常时序图模板

```mermaid
sequenceDiagram
    participant U as User/App Action
    participant A as Activity/App
    participant W as WMS
    participant T as Transition/Shell
    participant I as Insets/IME
    participant S as SurfaceFlinger

    U->>A: Start / show / resize / request
    A->>W: addWindow / relayout
    W->>W: update focus/visibility/layer
    W->>T: collect/start if needed
    W->>I: update insets/ime target
    W->>S: commit surface transaction
    S-->>A: frame visible
```

### 异常时序图模板

```mermaid
sequenceDiagram
    participant U as User/App Action
    participant A as Activity/App
    participant W as WMS
    participant T as Transition/Shell
    participant I as Insets/IME
    participant S as SurfaceFlinger

    U->>A: Start / focus / ime show
    A->>W: relayout / state request
    W->>W: visibility/focus calculation deviates
    W->>T: transition not finished / participant wrong
    W->>I: ime/insets target mismatch
    W->>S: transaction delayed or wrong layer
    Note over W: first wrong state appears here
    Note over U: user sees wrong window result
```

------


<!-- source: 23-suggested-shared-resources.md -->

# Suggested Shared Resources

建议与以下共享资源配合使用：

- `../shared/templates/analysis_report.md`
- `../shared/templates/root_cause_report.md`
- `../shared/templates/evidence_table.md`
- `../shared/templates/architecture_analysis.md`
- `../shared/templates/architecture_diagram.md`
- `../shared/templates/sequence_diagram.md`
- `../shared/templates/code_walkthrough.md`
- `../shared/checklists/common_checklist.md`
- `../shared/checklists/log_checklist.md`
- `../shared/checklists/trace_checklist.md`
- `../shared/checklists/bugreport_checklist.md`
- `../shared/checklists/wms_checklist.md`
- `../shared/refs/aosp_module_index.md`
- `../shared/refs/common_paths.md`
- `../shared/refs/android_version_notes.md`
- `../shared/refs/glossary.md`
- `../shared/refs/wms_object_map.md`
- `../shared/refs/transition_flow_guide.md`
- `../shared/refs/insets_ime_guide.md`
- `../shared/examples/wms_case_01.md`
- `../shared/examples/wms_case_02.md`

------
