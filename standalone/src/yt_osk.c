/* yt_osk.c - see yt_osk.h. Uses the low-level PSL1GHT OSK API
 * (oskLoadAsync/oskUnloadAsync) with a 4 MB memory container; dialog
 * events arrive through the app's existing sysutil callback (SLOT0),
 * which records them in runtime_ps3 for us.
 *
 * Search-fix notes (2026-11-17, SEARCH-DIAGNOSIS.md): the first M1
 * build passed OSK_PANEL_TYPE_DEFAULT (0) as both allowedPanels and
 * firstViewPanel - "no panels allowed, no first panel" - and every
 * return code on the open path was discarded, so the dialog failed
 * instantly and silently. Now: real panel params (the PSL1GHT sample
 * pattern), every sub-call's return code logged distinctly and the
 * failure text kept for the UI, the memory container created once
 * and reused, and the terminal UNLOADED event read from the sticky
 * latch in runtime_ps3 (last-event-wins could drop it).
 *
 * Freeze-fix notes (2026-11-17, FREEZE-DIAGNOSIS.md): 1.10 froze as
 * the keyboard closed. On user confirm the firmware closes the dialog
 * itself and delivers the finish batch (ENTERED/DONE, then UNLOADED)
 * back-to-back; UNLOADED overwrote the finish event in the
 * last-event-wins atomic, state 1 did not accept UNLOADED, the sticky
 * latch was only read in state 2, and the state 1->2 transition's
 * runtime_osk_reset() could clear the latch after the only UNLOADED
 * had already fired. Neither state had a timeout, so the miss was
 * forever. Now: finish events are consumed from a second sticky
 * latch (runtime_osk_finish_take), state 1 also accepts UNLOADED and
 * finalizes on the spot (fetching the text with oskGetInputText when
 * the dialog self-closed before we could unload it), the latches are
 * cleared only in yt_osk_open, both states carry a deadline whose
 * expiry aborts the dialog and returns the pad, and every close-path
 * transition is logged.
 *
 * Freeze-fix-2 notes (2026-11-17, FREEZE2-DIAGNOSIS.md): 1.11 fixed
 * the confirm path and froze on CANCEL instead, for two stacked
 * reasons. (1) The cancel contract was never implemented: a user
 * cancel delivers INPUT_CANCELED and the dialog then waits for the
 * APP to call oskAbort(); only the abort produces DONE, only then is
 * oskUnloadAsync valid, and only the unload produces UNLOADED. 1.11
 * answered CANCELED with oskUnloadAsync - the wrong verb. State 1
 * now splits the finish handling: CANCELED -> oskAbort() -> state 2,
 * where the post-abort DONE makes the unload valid; ENTERED/DONE
 * keep the direct unload path. (2) yt_osk_poll sat behind main.c's
 * draw-suspended early-out, so while the VSH keyboard held drawing
 * suspended the poll (and its deadlines) could starve forever; main
 * now services the poll on every frame and calls
 * yt_osk_enforce_deadline() unconditionally, and the deadline drops
 * the state-1 typing cap once a finish/cancel has latched.
 *
 * Search-fix-2 notes (2026-11-17, SEARCH2-DIAGNOSIS.md): 1.12's
 * keyboard opened and closed perfectly and search STILL returned
 * nothing, because oskCallbackReturnParam.len is an in/out field -
 * capacity in, character count out - and yt_osk_open() left it 0.
 * oskUnloadAsync duly returned zero characters, finalize's
 * (res == OSK_OK && len > 0) gate read that as "cancelled or
 * empty", poll returned -1, and the search job was never
 * dispatched. Now: the capacity is preset wherever the parameter
 * is prepared (at open and immediately before each unload /
 * getInputText call), finalize accepts on res == OSK_OK plus a
 * non-empty bounded scan of the buffer (len is a logged
 * cross-check, never a gate), finalize falls back to
 * oskGetInputText on every path when the unload parameter yields
 * no text, and the retrieved text's head is logged at finalize. */
#include "yt_osk.h"
#include "runtime_ps3.h"
#include "youtube.h"   /* yt_log_sink */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef NEON_PS3
#include <sys/memory.h>
#include <sys/systime.h>
#include <sysutil/osk.h>
#include <sysutil/sysutil.h>
#else
/* Host-test shims (tests/test_yt_osk.c): minimal stand-ins for the
 * PSL1GHT OSK/sysutil surface used below, with SDK values, so the
 * poll state machine can be driven on a PC. The osk and sysMem
 * functions and yt_osk_host_now_ms() are implemented by the harness;
 * the runtime_osk signal functions are faked there too. */
typedef unsigned short u16;
typedef int sys_mem_container_t;
typedef enum { OSK_OK = 0, OSK_CANCELED = 1 } oskInputFieldResult;
typedef struct { float x, y; } oskPoint;
typedef struct {
    int allowedPanels;
    int firstViewPanel;
    oskPoint controlPoint;
    int prohibitFlags;
} oskParam;
typedef struct {
    u16 *message;
    u16 *startText;
    int maxLength;
} oskInputFieldInfo;
typedef struct {
    oskInputFieldResult res;
    int len;
    u16 *str;
} oskCallbackReturnParam;
#define OSK_FULLKEY_PANEL      2
#define OSK_PANEL_TYPE_ENGLISH   0x00000002
#define OSK_PANEL_TYPE_ALPHABET  0x01000000
#define OSK_PANEL_TYPE_NUMERAL   0x08000000
#define OSK_PANEL_TYPE_LATIN     0x02000000
#define SYSUTIL_OSK_DONE            0x0503
#define SYSUTIL_OSK_UNLOADED        0x0504
#define SYSUTIL_OSK_INPUT_ENTERED   0x0505
#define SYSUTIL_OSK_INPUT_CANCELED  0x0506
int sysMemContainerCreate(sys_mem_container_t *id, unsigned int size);
int sysMemContainerDestroy(sys_mem_container_t id);
int oskSetKeyLayoutOption(int option);
int oskLoadAsync(sys_mem_container_t id, oskParam *param, oskInputFieldInfo *info);
int oskUnloadAsync(oskCallbackReturnParam *param);
int oskAbort(void);
int oskGetInputText(oskCallbackReturnParam *info);
unsigned long long yt_osk_host_now_ms(void);
#endif

#define OSK_MAX_CHARS 100
/* Close-path bounds (FREEZE-DIAGNOSIS.md fix item 4): state 2 waits
 * for the terminal UNLOADED after a finish/unload request - a lost
 * event must expire, never hang. State 1 is the user typing, so its
 * cap is generous; it exists so no state of this machine can wait
 * forever. */
#define OSK_STATE2_TIMEOUT_MS 10000ull
#define OSK_STATE1_TIMEOUT_MS 300000ull

static sys_mem_container_t osk_container;
static int osk_container_ready;
static int osk_state;   /* 0 closed, 1 waiting for input event, 2 unloading */
static unsigned long long osk_deadline; /* osk_now_ms() expiry of the current state */
static int osk_unload_called;           /* we called oskUnloadAsync ourselves */
static int osk_abort_called;            /* we answered a cancel with oskAbort() */
static u16 osk_message[64];
static u16 osk_initial[OSK_MAX_CHARS + 1];
static u16 osk_result[OSK_MAX_CHARS + 1];
static oskCallbackReturnParam osk_out;
static char osk_error[128];

static unsigned long long osk_now_ms(void)
{
#ifdef NEON_PS3
    u64 sec, nsec;
    if (sysGetCurrentTime(&sec, &nsec)) return 0;
    return (unsigned long long)sec * 1000ull + (unsigned long long)(nsec / 1000000u);
#else
    return yt_osk_host_now_ms();
#endif
}

static void osk_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    yt_log_sink("osk", buf);
}

static int osk_fail(const char *step, int rc)
{
    snprintf(osk_error, sizeof(osk_error), "Keyboard failed: %s (rc %d / 0x%x)",
             step, rc, (unsigned)rc);
    osk_log("osk: %s", osk_error);
    return -1;
}

const char *yt_osk_last_error(void) { return osk_error; }

static void to_u16(u16 *dst, const char *src, int max)
{
    int i = 0;
    if (src) {
        while (src[i] && i < max) {
            dst[i] = (u16)(unsigned char)src[i];
            i++;
        }
    }
    dst[i] = 0;
}

int yt_osk_open(const char *initial)
{
    if (osk_state) return -1;
    osk_error[0] = 0;
    memset(&osk_out, 0, sizeof(osk_out));
    osk_out.str = osk_result;
    /* len is capacity-in / count-out (SEARCH2-DIAGNOSIS.md): offer
     * the full buffer here and again before every retrieval call. */
    osk_out.len = OSK_MAX_CHARS;
    osk_result[0] = 0;
    to_u16(osk_message, "Search YouTube", 63);
    to_u16(osk_initial, initial, OSK_MAX_CHARS);

    oskParam param;
    memset(&param, 0, sizeof(param));
    /* A real panel mask + first panel (PSL1GHT sample pattern).
     * OSK_PANEL_TYPE_DEFAULT is 0 - no panels at all - which made
     * oskLoadAsync reject the dialog instantly in the M1 build. */
    param.allowedPanels = OSK_PANEL_TYPE_ENGLISH | OSK_PANEL_TYPE_ALPHABET
                        | OSK_PANEL_TYPE_NUMERAL | OSK_PANEL_TYPE_LATIN;
    param.firstViewPanel = OSK_PANEL_TYPE_ENGLISH;
    param.controlPoint.x = 0.0f;
    param.controlPoint.y = 0.0f;
    param.prohibitFlags = 0;

    oskInputFieldInfo info;
    memset(&info, 0, sizeof(info));
    info.message = osk_message;
    info.startText = osk_initial;
    info.maxLength = OSK_MAX_CHARS;

    /* The container is created once and reused for every open -
     * creating/destroying it per search was the second suspect in
     * the diagnosis, and reuse removes it from the equation. It is
     * released only by yt_osk_shutdown() at app exit. */
    if (!osk_container_ready) {
        int crc = sysMemContainerCreate(&osk_container, 4 * 1024 * 1024);
        osk_log("osk: container rc=%d", crc);
        if (crc != 0) return osk_fail("memory container", crc);
        osk_container_ready = 1;
    }
    int krc = oskSetKeyLayoutOption(OSK_FULLKEY_PANEL);
    osk_log("osk: keylayout rc=%d", krc);
    /* The ONLY reset of the runtime OSK signals: the finish and
     * UNLOADED latches are never cleared during the close sequence,
     * where the firmware may already have delivered them. */
    runtime_osk_reset();
    int lrc = oskLoadAsync(osk_container, &param, &info);
    osk_log("osk: load rc=0x%x", (unsigned)lrc);
    if (lrc != 0) return osk_fail("oskLoadAsync", lrc);
    osk_state = 1;
    osk_unload_called = 0;
    osk_abort_called = 0;
    osk_deadline = osk_now_ms() + OSK_STATE1_TIMEOUT_MS;
    /* Arm the raw sysutil event trace (runtime_ps3) for this dialog
     * session: while it is armed the callback appends every status
     * it receives to youtube.log, so one hardware run shows the true
     * event sequence (FREEZE2-DIAGNOSIS.md fix item 3). */
    runtime_osk_trace(1);
    osk_log("osk: dialog loaded");
    return 0;
}

int yt_osk_is_open(void) { return osk_state != 0; }

/* Convert the result buffer into out: a bounded scan over
 * OSK_MAX_CHARS (the buffer is not trusted to be NUL-terminated
 * within it), control/non-Latin characters dropped, Latin-1 kept
 * as UTF-8 pairs - the conversion the search box has always used.
 * Returns the number of characters written (out NUL-terminated). */
static size_t osk_convert(char *out, size_t cap)
{
    size_t n = 0;
    if (!out || cap < 2) return 0;
    for (int i = 0; i < OSK_MAX_CHARS && osk_result[i] && n + 1 < cap; i++) {
        u16 c = osk_result[i];
        if (c >= 0x20 && c < 0x7f) out[n++] = (char)c;
        else if (c >= 0xa0 && c < 0x100) { /* Latin-1 supplement: keep as UTF-8 pair */
            if (n + 2 >= cap) break;
            out[n++] = (char)(0xc0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 0x3f));
        }
        /* anything else is dropped: the search box is ASCII-driven */
    }
    out[n] = 0;
    return n;
}

/* The head of the result buffer as plain text, for the finalize
 * log lines (SEARCH2-DIAGNOSIS.md fix item 4): if retrieval ever
 * comes back empty again, one log pull shows whether the buffer
 * held the typed text and what its first characters were. */
static void osk_result_head(char *dst, size_t cap)
{
    size_t n = 0;
    if (cap < 2) return;
    for (int i = 0; i < OSK_MAX_CHARS && osk_result[i] && n + 1 < cap; i++) {
        u16 c = osk_result[i];
        dst[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    dst[n] = 0;
}

/* The dialog is gone (UNLOADED seen): produce the final result.
 * Retrieval follows SEARCH2-DIAGNOSIS.md: oskCallbackReturnParam.len
 * is capacity-in / count-out, so it is preset to OSK_MAX_CHARS
 * immediately before every retrieval call (1.12 left it 0 and the
 * OSK returned zero characters); acceptance is res == OSK_OK plus a
 * non-empty scan of the buffer, with len kept as a logged
 * cross-check only; and when the unload parameter yields no text,
 * oskGetInputText is tried as a fallback on every path (it is the
 * primary retrieval when the firmware closed the dialog itself and
 * we never got to unload it). */
static int osk_finalize(char *out, size_t cap)
{
    osk_state = 0;
    runtime_osk_trace(0);
    if (!osk_unload_called) {
        osk_out.len = OSK_MAX_CHARS;
        int grc = oskGetInputText(&osk_out);
        char head[33];
        osk_result_head(head, sizeof(head));
        osk_log("osk: %s finalize: getInputText rc=0x%x res=%d len=%d text=\"%s\"",
                osk_abort_called ? "abort-close" : "self-close",
                (unsigned)grc, (int)osk_out.res, (int)osk_out.len, head);
    } else {
        char head[33];
        osk_result_head(head, sizeof(head));
        osk_log("osk: finalize after unload: res=%d len=%d text=\"%s\"",
                (int)osk_out.res, (int)osk_out.len, head);
    }
    size_t n = 0;
    if (osk_out.res == OSK_OK) {
        n = osk_convert(out, cap);
        if (n == 0 && osk_unload_called) {
            osk_out.len = OSK_MAX_CHARS;
            int grc = oskGetInputText(&osk_out);
            char head[33];
            osk_result_head(head, sizeof(head));
            osk_log("osk: finalize fallback: getInputText rc=0x%x res=%d len=%d text=\"%s\"",
                    (unsigned)grc, (int)osk_out.res, (int)osk_out.len, head);
            if (osk_out.res == OSK_OK) n = osk_convert(out, cap);
        }
    }
    if (n > 0) {
        osk_log("osk: result text (%u chars)", (unsigned)n);
        return 1;
    }
    osk_log("osk: no text - cancelled or empty");
    return -1;
}

/* A state's deadline expired: the finish sequence was lost. Abort
 * the dialog, force the machine closed, hand the pad back, and let
 * the caller surface the failure in the YouTube view (-2). */
static int osk_timeout(void)
{
    osk_log("osk: TIMEOUT in state %d - aborting the dialog", osk_state);
    int arc = oskAbort();
    osk_log("osk: abort rc=0x%x", (unsigned)arc);
    osk_state = 0;
    runtime_osk_trace(0);
    snprintf(osk_error, sizeof(osk_error), "Keyboard closed unexpectedly");
    return -2;
}

/* Deadline enforcement for the main loop (FREEZE2-DIAGNOSIS.md fix
 * item 4): main calls this on EVERY frame while the dialog is open,
 * unconditionally - before the draw-suspended early-out and outside
 * the poll path - so no branch can starve the bound the way the
 * suspended branch starved yt_osk_poll in 1.11. Returns -2 (and
 * force-closes the dialog, osk_error set) when the current state's
 * deadline has expired, 0 otherwise. Once a finish/cancel has
 * latched, the state-1 typing cap no longer applies: the close
 * sequence owns the bound from there (state 2's 10 s, entered when
 * poll consumes the latch on the same frame), so an expired typing
 * cap with a finish pending is not an expiry. */
int yt_osk_enforce_deadline(void)
{
    if (!osk_state) return 0;
    if (osk_state == 1 && runtime_osk_finish_peek()) return 0;
    if (osk_now_ms() < osk_deadline) return 0;
    return osk_timeout();
}

int yt_osk_poll(char *out, size_t cap)
{
    if (!osk_state) return -1;
    if (osk_state == 1) {
        /* UNLOADED first: on confirm the firmware closes the dialog
         * itself and the finish batch arrives back-to-back, so by the
         * time this poll runs the dialog may already be gone, with
         * UNLOADED having overwritten the finish event in the
         * last-event-wins atomic. Accept it here and finalize on the
         * spot instead of waiting for an event that already fired. */
        if (runtime_osk_unloaded_take() || runtime_osk_event() == SYSUTIL_OSK_UNLOADED) {
            osk_log("osk: state 1 saw UNLOADED (dialog self-closed) - finalizing");
            return osk_finalize(out, cap);
        }
        int fev = runtime_osk_finish_take();
        if (fev) {
            if (fev == SYSUTIL_OSK_INPUT_CANCELED) {
                /* The cancel contract (FREEZE2-DIAGNOSIS.md): a
                 * user cancel only NOTIFIES the app; the dialog then
                 * waits for the app to abort it. Only the abort
                 * produces DONE, only then is oskUnloadAsync valid,
                 * and only the unload produces UNLOADED. Answer with
                 * oskAbort() - never oskUnloadAsync, which 1.11
                 * wrongly sent here. State 2 below completes the
                 * sequence when DONE arrives. */
                int arc = oskAbort();
                osk_abort_called = 1;
                osk_log("osk: finish ev=0x%x (cancel), abort rc=0x%x -> state 2",
                        fev, (unsigned)arc);
            } else {
                osk_out.len = OSK_MAX_CHARS; /* capacity in (SEARCH2-DIAGNOSIS.md) */
                int urc = oskUnloadAsync(&osk_out);
                osk_unload_called = 1;
                osk_log("osk: finish ev=0x%x, unload rc=0x%x -> state 2", fev, (unsigned)urc);
            }
            osk_state = 2;
            osk_deadline = osk_now_ms() + OSK_STATE2_TIMEOUT_MS;
            /* Fall through to the state-2 code below: an UNLOADED
             * delivered in the same batch is already latched and is
             * consumed on this same poll. The latch is NOT cleared
             * here - runtime_osk_reset() runs only in yt_osk_open. */
        } else {
            if (osk_now_ms() >= osk_deadline) return osk_timeout();
            return 0;
        }
    }
    /* state 2: the dialog is unloading; the result is final at
     * UNLOADED. Read the sticky latch, not the last-event-wins
     * atomic - a fast ENTERED -> DONE -> UNLOADED sequence could
     * overwrite UNLOADED there before this poll ran. */
    if (runtime_osk_unloaded_take()) {
        osk_log("osk: state 2 saw UNLOADED - finalizing");
        return osk_finalize(out, cap);
    }
    /* Cancel sequence, second half: after our abort the firmware
     * answers DONE, and only that makes oskUnloadAsync valid - the
     * unload is what produces the UNLOADED waited for above. (On the
     * ENTERED path the unload already happened in state 1, so this
     * runs only when osk_abort_called is set and no unload has been
     * issued yet.) */
    if (osk_abort_called && !osk_unload_called
            && runtime_osk_finish_take() == SYSUTIL_OSK_DONE) {
        osk_out.len = OSK_MAX_CHARS; /* capacity in (SEARCH2-DIAGNOSIS.md) */
        int urc = oskUnloadAsync(&osk_out);
        osk_unload_called = 1;
        osk_log("osk: state 2 post-abort DONE, unload rc=0x%x", (unsigned)urc);
        if (runtime_osk_unloaded_take()) {
            osk_log("osk: state 2 saw UNLOADED - finalizing");
            return osk_finalize(out, cap);
        }
    }
    if (osk_now_ms() >= osk_deadline) return osk_timeout();
    return 0;
}

void yt_osk_close(void)
{
    if (!osk_state) return;
    int arc = oskAbort();
    osk_log("osk: close: abort rc=0x%x", (unsigned)arc);
    if (osk_state == 1) {
        osk_out.len = OSK_MAX_CHARS; /* capacity in (SEARCH2-DIAGNOSIS.md) */
        int urc = oskUnloadAsync(&osk_out);
        osk_log("osk: close: unload rc=0x%x", (unsigned)urc);
    }
    osk_state = 0;
    runtime_osk_trace(0);
    /* the container is deliberately kept - see yt_osk_open */
}

void yt_osk_shutdown(void)
{
    yt_osk_close();
    if (osk_container_ready) {
        sysMemContainerDestroy(osk_container);
        osk_container_ready = 0;
    }
}
