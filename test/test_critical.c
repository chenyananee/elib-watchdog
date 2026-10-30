/* test_critical.c --- critical-hook pairing, balance, callbacks outside the lock */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_wdt.h"

#define MAX_TASKS 4u
static elib_wdt_ctx_t ctx;
static elib_wdt_task_t tasks[MAX_TASKS];

static uint32_t hook_enter;
static uint32_t hook_exit;
static void enter_hook(void) { hook_enter++; }
static void exit_hook(void) { hook_exit++; }

static uint32_t feed_count;
static uint32_t feed_balance_at_call;
static void feed_cb(void) {
    feed_count++;
    feed_balance_at_call = hook_enter - hook_exit;
}

static uint32_t timeout_count;
static uint32_t timeout_balance_at_call;
static void timeout_cb(void *ud) {
    (void)ud;
    timeout_count++;
    timeout_balance_at_call = hook_enter - hook_exit;
}

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
    hook_enter = 0u;
    hook_exit = 0u;
    feed_count = 0u;
    timeout_count = 0u;
    feed_balance_at_call = 0u;
    timeout_balance_at_call = 0u;
    assert(elib_wdt_init(&ctx, &cfg, enter_hook, exit_hook) == ELIB_WDT_OK);
}

void test_hooks_are_stored(void) {
    printf("Test: init stores the hook pair... ");
    reset();
    assert(ctx.enter_critical == enter_hook);
    assert(ctx.exit_critical == exit_hook);
    printf("PASSED\n");
}

void test_hooks_balanced_per_operation(void) {
    printf("Test: every hooked operation enters and leaves exactly once... ");
    reset();

    uint32_t before = hook_enter;
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(hook_enter > before);
    assert(hook_enter == hook_exit);

    before = hook_enter;
    assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
    assert(hook_enter > before);
    assert(hook_enter == hook_exit);

    before = hook_enter;
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(hook_enter > before);
    assert(hook_enter == hook_exit);

    before = hook_enter;
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_OK);
    assert(hook_enter > before);
    assert(hook_enter == hook_exit);

    before = hook_enter;
    assert(elib_wdt_reset(&ctx) == ELIB_WDT_OK);
    assert(hook_enter > before);
    assert(hook_enter == hook_exit);

    before = hook_enter;
    assert(elib_wdt_unregister(&ctx, 1u) == ELIB_WDT_OK);
    assert(hook_enter > before);
    assert(hook_enter == hook_exit);
    printf("PASSED\n");
}

void test_feed_callback_runs_outside_lock(void) {
    printf("Test: feed_dog is called with the critical section released... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 10u) == ELIB_WDT_OK);

    assert(feed_count == 1u);
    assert(feed_balance_at_call == 0u);   /* hooks balanced inside the callback */
    assert(hook_enter == hook_exit);
    printf("PASSED\n");
}

void test_timeout_callback_runs_outside_lock(void) {
    printf("Test: on_timeout is called with the critical section released... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);

    assert(timeout_count == 1u);
    assert(timeout_balance_at_call == 0u);
    assert(hook_enter == hook_exit);
    printf("PASSED\n");
}

void test_timeout_callback_can_call_api(void) {
    printf("Test: the timeout callback may re-enter the API safely... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);

    /* diagnose from inside the callback: no deadlock, hooks still balanced */
    elib_wdt_task_info_t info;
    assert(elib_wdt_get_info(&ctx, 1u, &info) == ELIB_WDT_OK);
    assert(info.checked_in == 1u);
    assert(hook_enter == hook_exit);
    printf("PASSED\n");
}

void test_no_hooks_still_works(void) {
    printf("Test: a NULL hook pair leaves the API functional... ");
    reset();
    ctx.enter_critical = NULL;
    ctx.exit_critical = NULL;
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(hook_enter == hook_exit);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_critical ===\n");
    test_hooks_are_stored();
    test_hooks_balanced_per_operation();
    test_feed_callback_runs_outside_lock();
    test_timeout_callback_runs_outside_lock();
    test_timeout_callback_can_call_api();
    test_no_hooks_still_works();
    printf("=== test_critical: all PASSED ===\n");
    return 0;
}
