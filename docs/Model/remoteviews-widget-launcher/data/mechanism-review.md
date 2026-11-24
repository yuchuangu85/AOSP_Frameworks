# Mechanism Review

Reviewed on 2026-10-28 against the source snapshot and file hashes in `source-identity.json`. This review is independent of excerpt matching and local browser checks.

- The checkout has framework AppWidget and RemoteViews implementations, not Launcher3. The diagram models Launcher as the widget Host role; private Workspace placement, LauncherAppWidgetHost overrides and actual Executor configuration are not asserted.
- The widget instance is already authorized and bound. The selected first-update ordering prepares a host container before the provider update; it is a feasible teaching order, not the only lifecycle order.
- A RemoteViews constructor plus setters records resource identity and serializable Actions. The provider does not instantiate the destination View tree. Parcel transfers supported metadata, actions, data and handles; the service caches descriptors, not host View objects.
- AppWidgetManager can be invoked both from an update broadcast and by other application update code. Normal XML widgets avoid the legacy-list deferred callback path in this model.
- Full and partial service updates are independent of host recycling. The partial example explicitly starts with an existing full cache. Current executable fallback for an empty cache differs from an older public comment; that branch is documented but not animated.
- Service callback processing is queued; IAppWidgetHost is oneway. A normal host Binder callback posts to a Handler and does not imply that the UI listener has run. The page selects the host main Looper branch, not an invariant of every custom Host.
- With callbacks absent, descriptor and sequence state can update without realtime delivery. startListening returns pending snapshots to the calling thread; this is distinct from the oneway callback route and is not a promise to replay every intermediate revision or persist across a system restart.
- The host resolves provider resources and uses restricted inflation/method eligibility in the ordinary XML path. A qualifying existing tree is reapplied; qualification does not guarantee success. Fresh apply and error content are separate outcomes. Failed reapply is not an atomic rollback guarantee; intermediate mutations and multi-update races are outside this single-update visualization.
- Async preparation runs on the selected executor. AsyncApplyTask binds post-execution to the context main Looper: onViewInflated, final Actions, onViewApplied; the Host listener then applies content in the same completion phase. No artificial cross-frame callback gap is introduced.
- onViewApplied, applyContent, traversal/draw and physical presentation are distinct. The right-side screen changes only at an explicitly illustrative time. No widget-specific present ACK, fixed frame latency or physical display evidence is claimed.
- Click Actions establish a local Host listener and route a supported PendingIntent response; arbitrary OnClickListener lambdas and View objects are not sent back to the provider.
- The independent AppWidget review checked eight scenarios and boundary samples without finding a mechanism or state counterexample. Model tests, source excerpt checks and browser validation remain separate evidence classes.

Model assumptions: one ordinary XML widget, valid provider/host, authorized binding, memory/URI checks passing, no mask/lock path, no collections or DrawInstructions, no multi-update cancellation race. No Android device or actual Launcher GUI acceptance was performed.
