/* test_start_stop.c --- start/stop/reset transitions and window re-arming */
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

void test_start_from_idle(void) {
    printf("Test: start moves IDLE to RUNNING... ");
    reset();
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    printf("PASSED\n");
}

void test_start_twice(void) {
    printf("Test: a second start is ALREADY_RUNNING... ");
    reset();
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_ERR_ALREADY_RUNNING);
    printf("PASSED\n");
}

void test_stop_to_idle(void) {
    printf("Test: stop moves RUNNING back to IDLE... ");
    reset();
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_stop(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    printf("PASSED\n");
}

void test_stop_when_idle(void) {
    printf("Test: stop while idle is NOT_RUNNING... ");
    reset();
    assert(elib_wdt_stop(&ctx) == ELIB_WDT_ERR_NOT_RUNNING);
    printf("PASSED\n");
}

void test_start_rearms_windows(void) {
    printf("Test: start reloads every window and clears the markers... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "a", 300u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 2u, "b", 500u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 120u) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 180u);
    assert(tasks[1].remaining_ms == 380u);
    assert(tasks[0].checked_in == 1u);

    assert(elib_wdt_stop(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 300u);
    assert(tasks[1].remaining_ms == 500u);
    assert(tasks[0].checked_in == 0u);
    printf("PASSED\n");
}

void test_manage_in_idle_is_noop(void) {
    printf("Test: manage while idle neither counts down nor feeds... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 5000u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 100u);
    assert(feed_count == 0u);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    printf("PASSED\n");
}

void test_reset_keeps_registrations(void) {
    printf("Test: reset re-arms and returns to IDLE but keeps registrations... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "keep", 200u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 50u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 150u);

    assert(elib_wdt_reset(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    assert(tasks[0].remaining_ms == 200u);
    assert(tasks[0].registered == 1u);
    assert(strcmp(tasks[0].name, "keep") == 0);
    printf("PASSED\n");
}

void test_start_from_timeout(void) {
    printf("Test: start recovers from TIMEOUT and re-arms... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(ctx.state == ELIB_WDT_STATE_TIMEOUT);

    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    assert(tasks[0].remaining_ms == 100u);
    assert(elib_wdt_manage(&ctx, 50u) == ELIB_WDT_OK);
    printf("PASSED\n");
}

void test_reset_from_timeout(void) {
    printf("Test: reset recovers from TIMEOUT into IDLE... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);

    assert(elib_wdt_reset(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    assert(tasks[0].remaining_ms == 100u);
    assert(elib_wdt_manage(&ctx, 999u) == ELIB_WDT_OK);   /* idle no-op */
    printf("PASSED\n");
}

void test_stop_from_timeout(void) {
    printf("Test: stop leaves TIMEOUT and enters IDLE... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(elib_wdt_stop(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    printf("PASSED\n");
}

void test_control_invalid(void) {
    printf("Test: control APIs reject NULL ctx and an uninitialized ctx... ");
    reset();
    assert(elib_wdt_start(NULL) == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_stop(NULL) == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_reset(NULL) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_stop(&ctx) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_reset(&ctx) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_start_stop ===\n");
    test_start_from_idle();
    test_start_twice();
    test_stop_to_idle();
    test_stop_when_idle();
    test_start_rearms_windows();
    test_manage_in_idle_is_noop();
    test_reset_keeps_registrations();
    test_start_from_timeout();
    test_reset_from_timeout();
    test_stop_from_timeout();
    test_control_invalid();
    printf("=== test_start_stop: all PASSED ===\n");
    return 0;
}
