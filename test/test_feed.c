/* test_feed.c --- feed reloads the window, clears the marker, works in idle */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_wdt.h"

#define MAX_TASKS 4u
static elib_wdt_ctx_t ctx;
static elib_wdt_task_t tasks[MAX_TASKS];

static uint32_t feed_count;
static void feed_cb(void) { feed_count++; }

static void reset(void) {
    elib_wdt_cfg_t cfg = {
        .feed_dog = feed_cb,
        .on_timeout = NULL,
        .timeout_ms = 100u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = NULL,
    };
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    feed_count = 0u;
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
}

void test_feed_reloads_window(void) {
    printf("Test: feed reloads the remaining window... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 400u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 150u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 250u);

    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 400u);
    printf("PASSED\n");
}

void test_feed_per_task_window(void) {
    printf("Test: feed restores the task's own window, not the global one... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "short", 20u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 2u, "long", 0u) == ELIB_WDT_OK);
    assert(tasks[0].timeout_ms == 20u);
    assert(tasks[1].timeout_ms == 100u);

    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 20u);
    assert(elib_wdt_feed(&ctx, 2u) == ELIB_WDT_OK);
    assert(tasks[1].remaining_ms == 100u);
    printf("PASSED\n");
}

void test_feed_clears_marker(void) {
    printf("Test: feed clears checked_in... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 0u);
    printf("PASSED\n");
}

void test_feed_in_idle(void) {
    printf("Test: feed is allowed while the manager is idle... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 100u);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    printf("PASSED\n");
}

void test_feed_unknown_id(void) {
    printf("Test: feed rejects an unknown ID... ");
    reset();
    assert(elib_wdt_feed(&ctx, 8u) == ELIB_WDT_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_feed_invalid(void) {
    printf("Test: feed rejects NULL ctx and an uninitialized ctx... ");
    reset();
    assert(elib_wdt_feed(NULL, 1u) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

void test_feed_keeps_system_healthy(void) {
    printf("Test: periodic feeding keeps the manager out of timeout... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    for (uint32_t i = 0u; i < 20u; i++) {
        assert(elib_wdt_manage(&ctx, 60u) == ELIB_WDT_OK);
        assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    }
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    assert(feed_count == 20u);
    printf("PASSED\n");
}

void test_feed_after_timeout_does_not_clear_state(void) {
    printf("Test: feed after a timeout reloads the window but stays TIMEOUT... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 200u) == ELIB_WDT_ERR_TIMEOUT);

    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 100u);
    assert(ctx.state == ELIB_WDT_STATE_TIMEOUT);

    /* sticky: manage still reports the timeout and does not decrement */
    assert(elib_wdt_manage(&ctx, 50u) == ELIB_WDT_ERR_TIMEOUT);
    assert(tasks[0].remaining_ms == 100u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_feed ===\n");
    test_feed_reloads_window();
    test_feed_per_task_window();
    test_feed_clears_marker();
    test_feed_in_idle();
    test_feed_unknown_id();
    test_feed_invalid();
    test_feed_keeps_system_healthy();
    test_feed_after_timeout_does_not_clear_state();
    printf("=== test_feed: all PASSED ===\n");
    return 0;
}
