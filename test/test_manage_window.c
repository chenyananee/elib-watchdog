/* test_manage_window.c --- count-down arithmetic, boundaries, feeding policy */
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
        .timeout_ms = 1000u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = NULL,
    };
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    feed_count = 0u;
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
}

void test_decrement_accumulates(void) {
    printf("Test: manage subtracts the elapsed time... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    assert(elib_wdt_manage(&ctx, 300u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 700u);
    assert(elib_wdt_manage(&ctx, 200u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 500u);
    printf("PASSED\n");
}

void test_boundary_one_short(void) {
    printf("Test: remaining 1 ms before the boundary stays healthy... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 1000u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 999u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 1u);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    printf("PASSED\n");
}

void test_boundary_exact(void) {
    printf("Test: remaining == elapsed expires immediately... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 1000u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 400u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 600u) == ELIB_WDT_ERR_TIMEOUT);
    assert(tasks[0].remaining_ms == 0u);
    printf("PASSED\n");
}

void test_overshoot_saturates(void) {
    printf("Test: a large elapsed saturates the window at zero... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100000u) == ELIB_WDT_ERR_TIMEOUT);
    assert(tasks[0].remaining_ms == 0u);
    printf("PASSED\n");
}

void test_zero_elapsed_is_noop(void) {
    printf("Test: manage(0) changes nothing and does not feed... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 0u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 100u);
    assert(feed_count == 0u);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    printf("PASSED\n");
}

void test_feeds_when_healthy(void) {
    printf("Test: manage feeds the hardware watchdog while healthy... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(feed_count == 0u);
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_OK);
    assert(feed_count == 1u);
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_OK);
    assert(feed_count == 2u);
    printf("PASSED\n");
}

void test_no_tasks_still_feeds(void) {
    printf("Test: with no tasks registered manage only feeds... ");
    reset();
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 5000u) == ELIB_WDT_OK);
    assert(feed_count == 1u);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    printf("PASSED\n");
}

void test_each_task_counts_independently(void) {
    printf("Test: every registered task keeps its own window... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "a", 100u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 2u, "b", 400u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 3u, "c", 900u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    assert(elib_wdt_manage(&ctx, 50u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 50u);
    assert(tasks[1].remaining_ms == 350u);
    assert(tasks[2].remaining_ms == 850u);

    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 100u);
    assert(tasks[1].remaining_ms == 350u);
    printf("PASSED\n");
}

void test_unused_slots_ignored(void) {
    printf("Test: unused slots are never counted down... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 30u) == ELIB_WDT_OK);
    for (uint32_t i = 1u; i < MAX_TASKS; i++) {
        assert(tasks[i].registered == 0u);
        assert(tasks[i].remaining_ms == 0u);
    }
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_manage_window ===\n");
    test_decrement_accumulates();
    test_boundary_one_short();
    test_boundary_exact();
    test_overshoot_saturates();
    test_zero_elapsed_is_noop();
    test_feeds_when_healthy();
    test_no_tasks_still_feeds();
    test_each_task_counts_independently();
    test_unused_slots_ignored();
    printf("=== test_manage_window: all PASSED ===\n");
    return 0;
}
