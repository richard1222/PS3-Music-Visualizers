/* test_yt_osk.c - host test for the yt_osk poll state machine
 * (FREEZE-DIAGNOSIS.md, FREEZE2-DIAGNOSIS.md). yt_osk.c is compiled
 * without NEON_PS3, so its PSL1GHT surface comes from the shim block
 * in yt_osk.c; this harness implements those functions plus the
 * runtime_ps3 OSK signal functions, mirroring runtime_ps3.c's
 * system_event() semantics: a last-event-wins atomic, a sticky
 * first-finish-wins latch, and a sticky UNLOADED latch.
 *
 * The headline scenarios are the two hardware freezes: 1.10 (the
 * firmware delivers ENTERED -> DONE -> UNLOADED back-to-back before
 * any poll runs, UNLOADED overwrites the finish event in the atomic,
 * and the old machine waited in state 1 forever) and 1.11 (a user
 * cancel delivers INPUT_CANCELED and waits for the app to answer
 * with oskAbort() - the contract 1.11 never implemented, answering
 * with oskUnloadAsync instead). S14 is the 1.12 search bug: the
 * keyboard worked but returned zero characters because
 * oskCallbackReturnParam.len is capacity-in / count-out and the
 * app offered 0 (SEARCH2-DIAGNOSIS.md); the fake now honors the
 * offered capacity, so that bug fails here the way it failed on
 * hardware. */
#include <stdio.h>
#include <string.h>
#include "yt_osk.h"

/* Types mirrored from the host shim block in yt_osk.c (same layout;
 * the linker only sees the function symbols). */
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

#define EV_DONE      0x0503
#define EV_UNLOADED  0x0504
#define EV_ENTERED   0x0505
#define EV_CANCELED  0x0506

/* ---- fake runtime_ps3 OSK signals ---- */
static int fake_event, fake_unloaded, fake_finish, reset_calls;
static int fake_trace;
int runtime_osk_event(void) { return fake_event; }
void runtime_osk_reset(void)
{
    fake_event = 0;
    fake_unloaded = 0;
    fake_finish = 0;
    reset_calls++;
}
int runtime_osk_unloaded_take(void)
{
    int v = fake_unloaded;
    fake_unloaded = 0;
    return v;
}
int runtime_osk_finish_take(void)
{
    int v = fake_finish;
    fake_finish = 0;
    return v;
}
int runtime_osk_finish_peek(void) { return fake_finish; }
void runtime_osk_trace(int on) { fake_trace = on ? 1 : 0; }

/* Mirror of runtime_ps3.c system_event() for OSK statuses. */
static void deliver(int status)
{
    fake_event = status;
    if ((status == EV_ENTERED || status == EV_CANCELED || status == EV_DONE) && !fake_finish)
        fake_finish = status;
    if (status == EV_UNLOADED) fake_unloaded = 1;
}

/* ---- fake clock ---- */
static unsigned long long fake_now;
unsigned long long yt_osk_host_now_ms(void) { return fake_now; }

/* ---- fake OSK library ---- */
static oskInputFieldResult script_res;
static const char *script_text;
static int unload_calls, gettext_calls, abort_calls;
/* The capacity the app offered at entry to each retrieval call -
 * recorded so scenarios can assert the app presets it
 * (SEARCH2-DIAGNOSIS.md). */
static int unload_in_len, gettext_in_len;

static void fill_out(oskCallbackReturnParam *p)
{
    /* Model the platform contract (SEARCH2-DIAGNOSIS.md): len is
     * capacity in, count out. Write at most the offered capacity
     * and report the count written; capacity 0 writes nothing and
     * returns count 0 - exactly what hardware did to the 1.12 app,
     * and exactly what the old fake (which ignored the incoming
     * len) could not see. */
    int capacity = p->len;
    p->res = script_res;
    p->len = 0;
    if (script_text && capacity > 0) {
        int i = 0;
        while (script_text[i] && i < capacity) {
            p->str[i] = (u16)(unsigned char)script_text[i];
            i++;
        }
        p->str[i] = 0;
        p->len = i;
    }
}
int sysMemContainerCreate(sys_mem_container_t *id, unsigned int size)
{
    (void)size;
    *id = 7;
    return 0;
}
int sysMemContainerDestroy(sys_mem_container_t id) { (void)id; return 0; }
int oskSetKeyLayoutOption(int option) { (void)option; return 0; }
int oskLoadAsync(sys_mem_container_t id, oskParam *p, oskInputFieldInfo *i)
{
    (void)id; (void)p; (void)i;
    return 0;
}
int oskUnloadAsync(oskCallbackReturnParam *p)
{
    unload_calls++;
    unload_in_len = p->len;
    fill_out(p);
    return 0;
}
int oskGetInputText(oskCallbackReturnParam *p)
{
    gettext_calls++;
    gettext_in_len = p->len;
    fill_out(p);
    return 0;
}
int oskAbort(void) { abort_calls++; return 0; }

void yt_log_sink(const char *level, const char *line)
{
    (void)level;
    (void)line;
}

/* ---- scenarios ---- */
static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } \
} while (0)

static void fresh(void)
{
    yt_osk_close(); /* no-op when already closed */
    fake_event = fake_unloaded = fake_finish = 0;
    fake_trace = 0;
    reset_calls = 0;
    unload_calls = gettext_calls = abort_calls = 0;
    unload_in_len = gettext_in_len = -1;
    fake_now = 100000;
    script_res = OSK_OK;
    script_text = "zelda";
}

int main(void)
{
    char out[128];

    /* S1: classic path - ENTERED, we unload, UNLOADED finalizes. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    CHECK(yt_osk_is_open());
    CHECK(reset_calls == 1);
    CHECK(fake_trace == 1); /* the raw event trace is armed for the session */
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    deliver(EV_ENTERED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(unload_calls == 1);
    CHECK(reset_calls == 1); /* the transition must not clear the latches */
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(strcmp(out, "zelda") == 0);
    CHECK(gettext_calls == 0);
    CHECK(!yt_osk_is_open());
    CHECK(fake_trace == 0); /* disarmed at finalize */

    /* S2: THE 1.10 FREEZE - the whole finish batch (ENTERED, DONE,
     * UNLOADED) lands before any poll; UNLOADED overwrites ENTERED
     * in the atomic. The machine must finalize, not wait forever. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    deliver(EV_ENTERED);
    deliver(EV_DONE);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(strcmp(out, "zelda") == 0);
    CHECK(unload_calls == 0);  /* dialog self-closed; we never unloaded */
    CHECK(gettext_calls == 1); /* text fetched via oskGetInputText */
    CHECK(!yt_osk_is_open());

    /* S3: pure self-close - UNLOADED alone in state 1. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(strcmp(out, "zelda") == 0);
    CHECK(gettext_calls == 1);
    CHECK(!yt_osk_is_open());

    /* S4: THE 1.11 FREEZE - the cancel contract. INPUT_CANCELED only
     * notifies the app; the dialog waits for oskAbort(). Only the
     * abort produces DONE, only then is oskUnloadAsync valid, and
     * only the unload produces UNLOADED. 1.11 answered CANCELED with
     * oskUnloadAsync (wrong verb) and froze. */
    fresh();
    script_res = OSK_CANCELED;
    script_text = NULL;
    CHECK(yt_osk_open("") == 0);
    deliver(EV_CANCELED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(abort_calls == 1);   /* cancel is answered with abort... */
    CHECK(unload_calls == 0);  /* ...never with an unload */
    CHECK(yt_osk_is_open());
    deliver(EV_DONE);          /* the abort produces DONE */
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(unload_calls == 1);  /* only now is the unload valid */
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == -1);
    CHECK(!yt_osk_is_open());
    CHECK(fake_trace == 0);

    /* S5: cancel as a self-close (UNLOADED only). */
    fresh();
    script_res = OSK_CANCELED;
    script_text = NULL;
    CHECK(yt_osk_open("") == 0);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == -1);
    CHECK(gettext_calls == 1);
    CHECK(!yt_osk_is_open());

    /* S6: DONE alone is a finish too. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    deliver(EV_DONE);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(unload_calls == 1);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(strcmp(out, "zelda") == 0);

    /* S7: entered but empty text finalizes as -1 (1.10 semantics). */
    fresh();
    script_text = "";
    CHECK(yt_osk_open("") == 0);
    deliver(EV_ENTERED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == -1);
    CHECK(!yt_osk_is_open());

    /* S8: state 2 deadline - UNLOADED never comes after the finish.
     * The machine must abort, close, and report -2, never hang. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    deliver(EV_ENTERED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    fake_now += 9999;
    CHECK(yt_osk_poll(out, sizeof(out)) == 0); /* still within the bound */
    fake_now += 2;
    CHECK(yt_osk_poll(out, sizeof(out)) == -2);
    CHECK(abort_calls == 1);
    CHECK(!yt_osk_is_open());
    CHECK(strcmp(yt_osk_last_error(), "Keyboard closed unexpectedly") == 0);

    /* S9: state 1 deadline - dialog up, no events at all, past the
     * generous typing cap. Bounded like every other state. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    fake_now += 299999;
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    fake_now += 2;
    CHECK(yt_osk_poll(out, sizeof(out)) == -2);
    CHECK(abort_calls == 1);
    CHECK(!yt_osk_is_open());

    /* S10: cancel where UNLOADED arrives with no DONE in between
     * (abort -> UNLOADED directly). State 2's UNLOADED check fires
     * first and finalizes through oskGetInputText as a cancel. */
    fresh();
    script_res = OSK_CANCELED;
    script_text = NULL;
    CHECK(yt_osk_open("") == 0);
    deliver(EV_CANCELED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(abort_calls == 1);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == -1);
    CHECK(unload_calls == 0);
    CHECK(gettext_calls == 1);
    CHECK(!yt_osk_is_open());

    /* S11: deadline enforcement OUTSIDE poll (the main loop's
     * unconditional check): state 1 past the typing cap with no
     * finish latched force-closes with -2, no poll involved. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    CHECK(yt_osk_enforce_deadline() == 0);
    fake_now += 300001;
    CHECK(yt_osk_enforce_deadline() == -2);
    CHECK(abort_calls == 1);
    CHECK(!yt_osk_is_open());
    CHECK(strcmp(yt_osk_last_error(), "Keyboard closed unexpectedly") == 0);
    CHECK(yt_osk_enforce_deadline() == 0); /* closed: nothing to enforce */

    /* S12: the typing cap is dropped once a finish has latched -
     * past the cap with ENTERED pending, enforcement must NOT fire;
     * poll consumes the finish and the close proceeds normally. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    fake_now += 300001;
    deliver(EV_ENTERED);
    CHECK(yt_osk_enforce_deadline() == 0);
    CHECK(yt_osk_is_open());
    CHECK(abort_calls == 0);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(unload_calls == 1);
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(strcmp(out, "zelda") == 0);

    /* S13: enforcement in post-abort state 2 - the cancel was
     * answered, but DONE/UNLOADED never come; the 10 s state-2
     * bound fires from outside poll. */
    fresh();
    script_res = OSK_CANCELED;
    script_text = NULL;
    CHECK(yt_osk_open("") == 0);
    deliver(EV_CANCELED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(abort_calls == 1);
    fake_now += 9999;
    CHECK(yt_osk_enforce_deadline() == 0);
    fake_now += 2;
    CHECK(yt_osk_enforce_deadline() == -2);
    CHECK(abort_calls == 2); /* cancel abort + timeout abort */
    CHECK(!yt_osk_is_open());

    /* S14: REGRESSION (SEARCH2-DIAGNOSIS.md) - the capacity
     * contract. Under the contract-honoring fake, an app that
     * offers len=0 at a retrieval call gets zero characters back,
     * so the 1.12 app fails S1-S13 above outright. The fix presets
     * the capacity before every retrieval call; assert the offered
     * capacity at both call sites (100 = OSK_MAX_CHARS in yt_osk.c)
     * and that the typed text survives each round trip. */
    fresh();
    CHECK(yt_osk_open("") == 0);
    deliver(EV_ENTERED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 0);
    CHECK(unload_in_len == 100); /* capacity offered to the unload */
    deliver(EV_UNLOADED);
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(strcmp(out, "zelda") == 0);

    fresh();
    CHECK(yt_osk_open("") == 0);
    deliver(EV_UNLOADED); /* self-close: retrieval is oskGetInputText */
    CHECK(yt_osk_poll(out, sizeof(out)) == 1);
    CHECK(gettext_in_len == 100); /* capacity offered to getInputText */
    CHECK(strcmp(out, "zelda") == 0);
    CHECK(!yt_osk_is_open());

    yt_osk_shutdown();
    if (failures) {
        printf("test_yt_osk: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("test_yt_osk: PASS\n");
    return 0;
}
