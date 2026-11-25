#ifndef NEON_RUNTIME_PS3_H
#define NEON_RUNTIME_PS3_H
int runtime_init(void);
int runtime_exit_requested(void);
int runtime_draw_suspended(void);
void runtime_shutdown(void);
/* Last on-screen-keyboard dialog event (SYSUTIL_OSK_*), 0 if none. */
int runtime_osk_event(void);
void runtime_osk_reset(void);
/* Sticky latch for the terminal OSK UNLOADED event: returns 1 once
 * per unload (and clears). osk_event above is last-event-wins, so a
 * fast ENTERED -> DONE -> UNLOADED sequence can overwrite UNLOADED
 * before the poller sees it; this latch cannot be lost that way. */
int runtime_osk_unloaded_take(void);
/* Sticky latch for the OSK finish events (SYSUTIL_OSK_INPUT_ENTERED /
 * INPUT_CANCELED / DONE): returns the first finish event seen since
 * the last reset, once (and clears), or 0 if none. The finish batch
 * the firmware delivers on confirm can overwrite a finish event in
 * the last-event-wins osk_event with the UNLOADED behind it - the
 * 1.10 search freeze - so state transitions must consume this latch,
 * not the atomic. First finish wins: CANCELED followed by DONE stays
 * a cancel. */
int runtime_osk_finish_take(void);
/* Non-consuming read of the finish latch (0 if none). The OSK
 * deadline logic needs to know a finish/cancel is pending without
 * consuming it - poll owns consumption. */
int runtime_osk_finish_peek(void);
/* Arm/disarm the raw sysutil event trace. While armed (one OSK
 * dialog session, set by yt_osk_open/close paths), the sysutil
 * callback appends every status it receives - the OSK range
 * 0x0502-0x0506 and the draw/menu range 0x0121/0x0122/0x0131/0x0132 -
 * to youtube.log via yt_log_sink, so one hardware run shows the true
 * event sequence in the release build (FREEZE2-DIAGNOSIS.md; the
 * NL_DIAGNOSTIC system-events.log is a diagnostic-build artifact and
 * was never present in release sessions). */
void runtime_osk_trace(int on);
#ifdef NL_DIAGNOSTIC
void runtime_diagnostic_stall_flip(int exit_during_wait);
void runtime_diagnostic_menu_open(void);
#endif
#endif
