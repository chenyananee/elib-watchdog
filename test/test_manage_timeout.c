/* test_manage_timeout.c --- timeout reporting: no hang, sticky state, no feed */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_wdt.h"

#define MAX_TASKS 4u
static elib_wdt_ctx_t ctx;
static elib_wdt_task_t tasks[MAX_TASKS];

static uint32_t feed_count;
static uint32_t timeout_count;
static void *timeout_ud;
static void feed_cb(void) { feed_count++; }
static void timeout_cb(void *ud) { timeout_count++; timeout_ud = ud; }

static void reset(void) {
    elib_wdt_cfg_t cfg = {
        .feed_dog = feed_cb,
        .on_timeout = timeout_cb,
        .timeout_ms = 100u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = NULL,
    };
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    feed_count = 0u;
    timeout_count = 0u;
    timeout_ud = NULL;
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
}

void test_timeout_returns_error(void) {
    printf("Test: an expired window returns ERR_TIMEOUT instead of hanging... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(ctx.state == ELIB_WDT_STATE_TIMEOUT);
    assert(timeout_count == 1u);
    printf("PASSED\n");
}

void test_timeout_does_not_feed(void) {
    printf("Test: the timing-out tick does not feed the hardware watchdog... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(feed_count == 0u);

    /* nor do the following ticks while the timeout is sticky */
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_ERR_TIMEOUT);
    assert(feed_count == 0u);
    printf("PASSED\n");
}

void test_timeout_is_sticky(void) {
    printf("Test: the timeout callback fires once, state stays TIMEOUT... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(timeout_count == 1u);

    for (uint32_t i = 0u; i < 5u; i++) {
        assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_ERR_TIMEOUT);
    }
    assert(timeout_count == 1u);
    assert(ctx.state == ELIB_WDT_STATE_TIMEOUT);
    printf("PASSED\n");
}

void test_only_the_expired_task_is_zeroed(void) {
    printf("Test: a short window expiring leaves the long window intact... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "short", 100u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 2u, "long", 1000u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    assert(elib_wdt_manage(&ctx, 200u) == ELIB_WDT_ERR_TIMEOUT);
    assert(tasks[0].remaining_ms == 0u);
    assert(tasks[1].remaining_ms == 800u);
    printf("PASSED\n");
}

void test_timeout_user_data(void) {
    printf("Test: the timeout callback receives the configured user_data... ");
    int marker = 0;
    elib_wdt_cfg_t cfg = {
        .feed_dog = feed_cb,
        .on_timeout = timeout_cb,
        .timeout_ms = 100u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = &marker,
    };
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    feed_count = 0u;
    timeout_count = 0u;
    timeout_ud = NULL;
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);

    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(timeout_count == 1u);
    assert(timeout_ud == &marker);
    printf("PASSED\n");
}

void test_null_timeout_callback_safe(void) {
    printf("Test: a NULL timeout callback is harmless... ");
    reset();
    ctx.on_timeout = NULL;
    assert(elib_wdt_register(&ctx, 1u, NULL, 50u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 50u) == ELIB_WDT_ERR_TIMEOUT);
    assert(ctx.state == ELIB_WDT_STATE_TIMEOUT);
    printf("PASSED\n");
}

void test_null_feed_dog_safe(void) {
    printf("Test: a NULL feed_dog is harmless while healthy... ");
    reset();
    ctx.feed_dog = NULL;
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    printf("PASSED\n");
}

void test_recovery_after_timeout(void) {
    printf("Test: start after a timeout resumes normal feeding... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(feed_count == 0u);

    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_OK);
    assert(feed_count == 1u);
    assert(timeout_count == 1u);
    printf("PASSED\n");
}

void test_timeout_invalid(void) {
    printf("Test: manage rejects NULL ctx and an uninitialized ctx... ");
    reset();
    assert(elib_wdt_manage(NULL, 1u) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_manage(&ctx, 1u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_manage_timeout ===\n");
    test_timeout_returns_error();
    test_timeout_does_not_feed();
    test_timeout_is_sticky();
    test_only_the_expired_task_is_zeroed();
    test_timeout_user_data();
    test_null_timeout_callback_safe();
    test_null_feed_dog_safe();
    test_recovery_after_timeout();
    test_timeout_invalid();
    printf("=== test_manage_timeout: all PASSED ===\n");
    return 0;
}
