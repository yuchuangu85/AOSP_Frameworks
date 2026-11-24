# Mechanism Review

Reviewed against the current source snapshot recorded in `source-identity.json` on 2026-10-28. This review is separate from exact-excerpt matching and browser validation.

- Selected ordinary window connection, non-batched Motion events, successful publication, no key fallback, one to three original entries. Synthetic cancellation events are acknowledged as a side effect but omitted from the displayed original-event queues; recovery assumes they introduce no additional overdue item.
- `deliveryTime` and per-entry deadline are populated before the publication attempt. Only successful publication moves that entry to `waitQueue` and inserts its timeout into `AnrTracker` for a responsive connection. A WOULD_BLOCK outbound entry does not establish that entry's ACK tracking.
- Client consumption, callback handling, final finish, FINISHED transport, and dispatcher cleanup are distinct boundaries. Event payload crosses an InputChannel socket; it is not a per-event Binder dispatch.
- FINISHED carries sequence and handled. Clearing uses sequence; handled=false still completes the selected Motion entry. Current native code permits multiple entries in flight despite an older Java comment that could be misread as strict one-event serialization.
- The tracker selects the earliest deadline. ANR reason metadata uses the first-sent remaining waitQueue entry, which may have a later deadline when window configuration changed. Existing entries retain their original deadlines.
- Timeout marks the connection unresponsive and erases the token's tracking indices, not its waitQueue entries. Cancellation is generated without immediately breaking the connection. Repeated full notifications are not modeled for the same unrecovered connection.
- ACK cleanup removes only the matching original entry and its tracker index. After a timeout, recovery examines the remaining queue for overdue entries; it need not be empty, but one ACK does not guarantee recovery.
- Independent review found a FIFO inversion when ACK cleanup was shortly after expiry but before an arbitrary policy transition. A failing regression reproduced it. The visual transition is now shortened to precede the next cleanup command, preserving unresponsive-before-responsive order. Its duration is illustrative, not a fixed source timeout.
- Independent review also found preset outcome prose surviving changed timing parameters. A failing regression reproduced it. Custom timing now has a parameter-derived description and result marker.
- Native policy commands release the dispatcher lock; JNI calls Java IMS inside system_server. WMS resolves window/PID/Activity attribution and may enter AMS. Neither detection nor recovery directly proves a dialog, process kill, visible frame, or completed business operation.
- Base 5000 ms is not a universal runtime constant: hardware multiplier, window metadata and instrumentation configuration can affect it. Pre-ANR warning from the no-focus branch is not transplanted into this connection model.
- All event times and visual travel intervals are illustrative. Simultaneous ACK cleanup precedes deadline checks only as this model's deterministic tie-break. The long ACK-delay example is an abstract boundary case, not a claim about normal socket latency.

Validation boundaries: local source and pure model/browser evidence only. No Android device, vendor behavior, trace-derived timings or ANR-dialog acceptance.
