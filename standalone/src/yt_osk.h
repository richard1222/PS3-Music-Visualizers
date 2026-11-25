/* yt_osk.h - the PS3 system on-screen keyboard, for YouTube search entry.
 *
 * Non-blocking: yt_osk_open() loads the dialog, the main loop keeps
 * presenting frames and calling yt_osk_poll() (the sysutil callback pump
 * in the main loop delivers the dialog events), and poll returns the
 * entered text when the user finishes. yt_osk_close() aborts without
 * waiting - it exists for the app shutdown path, where the safety
 * watchdog is the backstop, as everywhere else in this app. */
#ifndef NL_YT_OSK_H
#define NL_YT_OSK_H

#include <stddef.h>

/* Open the keyboard with an initial query string. 0 ok, -1 on failure
 * or if already open. On failure yt_osk_last_error() says which step
 * failed and its return code. */
int yt_osk_open(const char *initial);

/* Human-readable description of the last open failure ("" if none). */
const char *yt_osk_last_error(void);

/* 1 while the dialog is up (the app should not consume pad input). */
int yt_osk_is_open(void);

/* Poll once per frame. Returns 0 while open, 1 when text was entered
 * (copied into out, UTF-8, control/non-Latin characters dropped),
 * -1 when the user cancelled or the dialog failed, -2 when the
 * dialog closed unexpectedly (a close-path deadline expired and the
 * dialog was aborted); yt_osk_last_error() then holds the message
 * to surface. No state waits forever: state 2 expires 10 s after the
 * finish/unload request, state 1 after a generous typing cap.
 * The finish handling follows the platform contract: ENTERED/DONE
 * are answered with oskUnloadAsync, but INPUT_CANCELED is answered
 * with oskAbort() - a cancel only notifies the app, and the dialog
 * waits for the abort before it finishes (FREEZE2-DIAGNOSIS.md). */
int yt_osk_poll(char *out, size_t cap);

/* Deadline enforcement, for the main loop to call on EVERY frame
 * while the dialog is open, unconditionally (before any branch that
 * could starve the poll, e.g. the draw-suspended early-out).
 * Returns -2 when the current state's deadline had expired and the
 * dialog was force-closed (yt_osk_last_error() holds the message),
 * 0 otherwise. Once a finish/cancel has latched, the state-1 typing
 * cap no longer applies - the close sequence's own bound governs. */
int yt_osk_enforce_deadline(void);

/* Abort immediately (app shutdown). Never blocks. The dialog's
 * memory container is kept for the next open. */
void yt_osk_close(void);

/* App exit only: abort if open and release the memory container
 * (created once and reused across opens; see yt_osk.c). */
void yt_osk_shutdown(void);

#endif
