/* test_checkin.c --- diagnostics marker: sets checked_in, never touches time */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_wdt.h"

#define MAX_TASKS 4u
static elib_wdt_ctx_t ctx;
static elib_wdt_task_t tasks[MAX_TASKS];

static uint32_t timeout_count;
static void timeout_cb(void *ud) { (void)ud; timeout_count++; }

static void reset(void) {
    elib_wdt_cfg_t cfg = {
        .feed_dog = NULL,
        .on_timeout = timeout_cb,
        .timeout_ms = 100u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = NULL,
    };
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    timeout_count = 0u;
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
}

void test_checkin_sets_flag(void) {
    printf("Test: checkin sets checked_in... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "sensor", 0u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 0u);

    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);
    printf("PASSED\n");
}

void test_checkin_keeps_timer(void) {
    printf("Test: checkin does not touch the remaining window... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 30u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 70u);

    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 70u);
    assert(tasks[0].timeout_ms == 100u);
    printf("PASSED\n");
}

void test_checkin_idempotent(void) {
    printf("Test: checkin is idempotent... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);
    assert(tasks[0].remaining_ms == 100u);
    printf("PASSED\n");
}

void test_feed_clears_flag(void) {
    printf("Test: feed clears checked_in... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);

    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 0u);
    printf("PASSED\n");
}

void test_checkin_unknown_id(void) {
    printf("Test: checkin rejects an unknown ID... ");
    reset();
    assert(elib_wdt_checkin(&ctx, 55u) == ELIB_WDT_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_checkin_invalid(void) {
    printf("Test: checkin rejects NULL ctx and an uninitialized ctx... ");
    reset();
    assert(elib_wdt_checkin(NULL, 1u) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

void test_checkin_allowed_in_idle(void) {
    printf("Test: checkin is allowed while the manager is idle... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    printf("PASSED\n");
}

void test_checkin_after_timeout(void) {
    printf("Test: checkin still works after a timeout... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 1000u) == ELIB_WDT_ERR_TIMEOUT);
    assert(timeout_count == 1u);

    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_checkin ===\n");
    test_checkin_sets_flag();
    test_checkin_keeps_timer();
    test_checkin_idempotent();
    test_feed_clears_flag();
    test_checkin_unknown_id();
    test_checkin_invalid();
    test_checkin_allowed_in_idle();
    test_checkin_after_timeout();
    printf("=== test_checkin: all PASSED ===\n");
    return 0;
}
