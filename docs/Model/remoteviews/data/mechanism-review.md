# Mechanism Review

Scope: the current local `base/` snapshot identified by `source-identity.json`.
This is a separate semantic review, not a consequence of excerpt matching or browser success.

The Launcher-focused extension additionally cites the local `native/` tree for
window buffers and SurfaceFlinger. Source identity records both trees.

## Direct Implementation Evidence

- Recording versus execution: `RemoteViews.java:7011-7013`, `8138-8141`, `2862-2877`, `8987-9014`. Setters record actions; applying them performs the actual host-side call.
- Classes versus methods: `RemoteViews.java:372-373`, `2273-2282`, `8617-8626`. Inflation and reflection have separate permission gates.
- Dispatch versus completion: `AppWidgetServiceImpl.java:3267-3335`, `IAppWidgetHost.aidl:25-31`, `AppWidgetHost.java:121-130,186-195`. Queueing and oneway callback return are not host application acknowledgments.
- Other host entry points: `AppWidgetHost.java:260-298,627-639`. Pending updates and cached views can be dispatched directly on their callers; the model deliberately selects the ordinary Handler path.
- Full/partial versus apply/reapply: `AppWidgetServiceImpl.java:3134-3148`, `RemoteViews.java:853-890`, `AppWidgetHostView.java:613-641`. Description replacement and tree reuse are independent policies.
- Executor versus main Looper: `RemoteViews.java:8736-8847`. Preparation uses the executor; final actions and listeners use the context main Looper. The model selects a background executor rather than assuming all custom executors imply a thread hop.
- Error installation: `AppWidgetHostView.java:644-665,863-868`. Failure creates an actual error TextView and installs it; failure does not imply the host has no child tree.
- Notifications: `NotificationRowContentBinderImpl.kt:1354-1373,1394-1412,1709-1718`. Reapply eligibility and synchronous fallback differ from AppWidgetHostView; notification scheduling is not simulated.
- Collections: `RemoteViews.java:7507-7514,7564-7566`, `RemoteViewsAdapter.java:850-855,988-996`, `RemoteCollectionItemsAdapter.java:186-198`. Service-backed loading and local items are separate paths.

## Discrepancy Found And Corrected

Independent read-only review found that the first model represented error fallback as
`tree=null, attached=false`, while its prose correctly said an error View was installed.

A new regression was run before the fix and failed on `getErrorView creates a real View`.
The model now creates `host-tree-error` with `kind=error, attached=true`, while keeping
`applied=false`, no successful callback, and `screen=unobserved`. The regression and
the rest of the model suite then passed. The view identifies this as installed error
content rather than successful RemoteViews content.

## Inferences And Teaching Assumptions

- There is no end-to-end display acknowledgment in the traced update/apply path.
  Host installation is not proof of later traversal, draw, compositing, or physical display.
- Event positions are causal teaching steps, not clock readings. The API return is
  intentionally not given a fixed position relative to concurrent host handling.
- The async completion callback and host installation are expanded observation points
  on one callback stack, not an invented asynchronous dispatch between them.
- One online, authorized, unmasked AppWidget is selected; budgets pass. No update races,
  cancellation timings, notification orchestration, or collection loading are simulated.
- Partial merging only models the demonstrated setter replacement, not every Action policy.

## Unknowns

No Android build, device execution, vendor behavior, runtime feature flag values,
trace timings, or physical display measurements were obtained. Excerpt, pure model,
and isolated browser checks remain separate evidence classes.

## Launcher Workflow Extension

- The default scene now starts before binding. `allocateAppWidgetId` creates a service
  record; `bindAppWidgetIdIfAllowed` associates Host and Provider, not a persistent
  remote drawing service. Authorization and configuration remain conditional branches.
- Broadcast submission precedes the selected Provider callback, but does not order
  all Launcher work relative to callback execution. The scene explicitly chooses
  HostView/workspace creation before Provider content arrives, one permitted interleaving.
- Receiver Binder ingress is separate from the ActivityThread main-thread callback.
  Likewise the service CallbackHandler, Launcher Binder ingress and Launcher Handler
  execution are separate observations. Default loopers are not attributed to all
  custom hosts or every direct initialization call.
- Only the incoming update tree is tracked by `tree`. `initialContent` separately
  records the HostView's initial content; `tree=null` at this point does not mean the
  Launcher has no existing View. `workspaceAttached` and content `attached` are
  separate relationships, tested independently.
- Review found that the first extension incorrectly implied initial content always
  requires another tree. A regression failed before the correction. The selected
  scene now explicitly declares different initial and update layout IDs, explaining
  its fresh-tree branch; compatible initial content can be reapplied without another
  `addView`. State labels are not a general object-identity guarantee.
- Configuration content is an application-side responsibility, not necessarily a
  direct call from the configuration Activity. `onUpdate`, that Activity or an
  application-scheduled task may submit it; proxy completion updates remain conditional.
- Launcher3 is absent. The workspace `addView` is a stated host integration duty,
  supported by the framework test host, not a fabricated Launcher3 implementation.
- The hardware drawing path names Launcher UI/RenderThread and the independent
  SurfaceFlinger process; it does not equate the async-apply Executor with RenderThread.
  Software drawing is an alternative. All physical display observations remain unknown.

Fresh local validation for this extension: 80/80 authored excerpts without warnings,
18/18 pure model tests, 344 browser step states and 80 source-topic selections at
1440, 1920, 390 and 320 pixels. The browser report records screenshots, no page errors
and no external requests; desktop and narrow screenshots were inspected separately.
