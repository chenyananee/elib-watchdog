/* test_unregister.c --- unregister / slot release / monitoring effect */
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

void test_unregister_clears_slot(void) {
    printf("Test: unregister clears every field of the slot... ");
    reset();
    assert(elib_wdt_register(&ctx, 3u, "comm", 50u) == ELIB_WDT_OK);
    assert(elib_wdt_unregister(&ctx, 3u) == ELIB_WDT_OK);

    assert(tasks[0].registered == 0u);
    assert(tasks[0].id == 0u);
    assert(tasks[0].name == NULL);
    assert(tasks[0].timeout_ms == 0u);
    assert(tasks[0].remaining_ms == 0u);
    printf("PASSED\n");
}

void test_unregister_twice(void) {
    printf("Test: a second unregister is NOT_FOUND... ");
    reset();
    assert(elib_wdt_register(&ctx, 3u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_unregister(&ctx, 3u) == ELIB_WDT_OK);
    assert(elib_wdt_unregister(&ctx, 3u) == ELIB_WDT_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_unregister_unknown_id(void) {
    printf("Test: unregister rejects an unknown ID... ");
    reset();
    assert(elib_wdt_unregister(&ctx, 99u) == ELIB_WDT_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_unregistered_task_is_not_monitored(void) {
    printf("Test: an unregistered task no longer expires... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_unregister(&ctx, 1u) == ELIB_WDT_OK);

    /* would have expired long ago if it were still registered */
    assert(elib_wdt_manage(&ctx, 10000u) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    assert(feed_count == 1u);
    printf("PASSED\n");
}

void test_unregister_invalid(void) {
    printf("Test: unregister rejects NULL ctx and an uninitialized ctx... ");
    reset();
    assert(elib_wdt_unregister(NULL, 1u) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_unregister(&ctx, 1u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

void test_unregister_stops_timeout_contribution(void) {
    printf("Test: removing the slow task keeps the manager healthy... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "fast", 50u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 2u, "slow", 500u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    /* keep the fast task alive, drop the slow one */
    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_unregister(&ctx, 2u) == ELIB_WDT_OK);

    assert(elib_wdt_manage(&ctx, 40u) == ELIB_WDT_OK);
    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 400u) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_unregister ===\n");
    test_unregister_clears_slot();
    test_unregister_twice();
    test_unregister_unknown_id();
    test_unregistered_task_is_not_monitored();
    test_unregister_invalid();
    test_unregister_stops_timeout_contribution();
    printf("=== test_unregister: all PASSED ===\n");
    return 0;
}
