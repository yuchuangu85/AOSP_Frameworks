# Mechanism Review

Reviewed against the repository snapshot recorded in `source-identity.json` on 2026-10-28. This review is separate from excerpt matching and browser behavior.

- Application identity and window focus use different update paths. A WMS focus request is not proof that FocusResolver has granted input focus.
- The selected key is assumed to pass earlier policy interception. The no-window timer begins at focused-target lookup `currentTime + application timeout`, not at process launch or the key's original timestamp.
- The current implementation uses `mNoFocusedWindowAnrState`. The 5000 ms constant is a base default, subject to application configuration and HW multiplier.
- Ordinary pointer hit testing, no focused event, and no application/window do not arm this branch in the chosen examples. A visible, touchable NOT_FOCUSABLE window still cannot accept the focused key.
- Pre-ANR is feature-gated; it does not disable or extend full ANR processing. The page toggle does not claim a device-default flag value.
- Expiry checks the awaited application token and display's focused window again, captures the notification into a command, and resets the wait. A later reset does not retract the captured callback. The illustration retains the historical incident separately from the reset native state.
- A changed application cancels its old wait, not necessarily its pending input. New focus re-enqueues the pending event (`InputDispatcher.cpp:1794-1801`). The switch scenario excludes touch-driven BLOCKED pruning and permits retargeting to B. A regression first failed on the old discarded-event depiction, then passed after correction.
- Policy delivery releases the dispatcher lock and crosses JNI inside system_server. WMS can filter unknown/stopped activities or change attribution before AMS handling. No unconditional dialog, process kill, or main-thread-only root cause is asserted.
- Window and input times, command visual transitions, a single display/event, and same-time window-before-timeout ordering are teaching assumptions, not measured scheduler guarantees.
- The supplied WeChat article body was not obtained. Its endpoint returned an environment-verification page, and alternate text extraction timed out; no article-specific claims were imported.

Remaining evidence boundary: local source, model tests and browser rendering only. No Android device, vendor implementation, trace timing, or ANR dialog acceptance was performed.
