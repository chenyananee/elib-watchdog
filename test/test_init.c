/* test_init.c --- init / deinit / hook-pair validation / array wipe / re-init */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_wdt.h"

#define MAX_TASKS 4u
static elib_wdt_ctx_t ctx;
static elib_wdt_task_t tasks[MAX_TASKS];

static uint32_t feed_count;
static uint32_t timeout_count;
static void feed_cb(void) { feed_count++; }
static void timeout_cb(void *ud) { (void)ud; timeout_count++; }

static uint32_t hook_enter, hook_exit;
static void enter_hook(void) { hook_enter++; }
static void exit_hook(void) { hook_exit++; }

static elib_wdt_cfg_t make_cfg(void) {
    elib_wdt_cfg_t cfg = {
        .feed_dog = feed_cb,
        .on_timeout = timeout_cb,
        .timeout_ms = 1000u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = NULL,
    };
    return cfg;
}

static void reset(void) {
    elib_wdt_cfg_t cfg = make_cfg();
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    feed_count = 0u;
    timeout_count = 0u;
    hook_enter = 0u;
    hook_exit = 0u;
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
}

void test_version(void) {
    printf("Test: version macros... ");
    assert(strcmp(ELIB_WDT_VERSION, "2.0.0") == 0);
    assert(ELIB_WDT_VERSION_MAJOR == 2);
    assert(ELIB_WDT_VERSION_MINOR == 0);
    assert(ELIB_WDT_VERSION_PATCH == 0);
    printf("PASSED\n");
}

void test_init_valid(void) {
    printf("Test: init fills the context from cfg... ");
    reset();
    assert(ctx.initialized == 1u);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    assert(ctx.feed_dog == feed_cb);
    assert(ctx.on_timeout == timeout_cb);
    assert(ctx.default_timeout_ms == 1000u);
    assert(ctx.tasks == tasks);
    assert(ctx.max_tasks == MAX_TASKS);
    assert(ctx.user_data == NULL);
    assert(ctx.enter_critical == NULL);
    assert(ctx.exit_critical == NULL);
    printf("PASSED\n");
}

void test_init_wipes_task_array(void) {
    printf("Test: init wipes stale registrations in the task array... ");
    memset(&ctx, 0, sizeof(ctx));
    for (uint32_t i = 0u; i < MAX_TASKS; i++) {
        tasks[i].id = 7u;
        tasks[i].registered = 1u;
        tasks[i].remaining_ms = 123u;
        tasks[i].checked_in = 1u;
    }

    elib_wdt_cfg_t cfg = make_cfg();
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
    for (uint32_t i = 0u; i < MAX_TASKS; i++) {
        assert(tasks[i].registered == 0u);
        assert(tasks[i].id == 0u);
        assert(tasks[i].remaining_ms == 0u);
        assert(tasks[i].checked_in == 0u);
    }

    /* the wiped slots must be usable again */
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    printf("PASSED\n");
}

void test_init_invalid_params(void) {
    printf("Test: init rejects null/zero parameters... ");
    elib_wdt_cfg_t cfg = make_cfg();

    assert(elib_wdt_init(NULL, &cfg, NULL, NULL) == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_init(&ctx, NULL, NULL, NULL) == ELIB_WDT_ERR_INVALID_PARAM);

    elib_wdt_cfg_t bad = cfg;
    bad.tasks = NULL;
    assert(elib_wdt_init(&ctx, &bad, NULL, NULL) == ELIB_WDT_ERR_INVALID_PARAM);

    bad = cfg;
    bad.max_tasks = 0u;
    assert(elib_wdt_init(&ctx, &bad, NULL, NULL) == ELIB_WDT_ERR_INVALID_PARAM);

    bad = cfg;
    bad.timeout_ms = 0u;
    assert(elib_wdt_init(&ctx, &bad, NULL, NULL) == ELIB_WDT_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

void test_init_hook_pair(void) {
    printf("Test: init rejects a one-sided critical-hook pair... ");
    elib_wdt_cfg_t cfg = make_cfg();

    assert(elib_wdt_init(&ctx, &cfg, enter_hook, NULL)
           == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_init(&ctx, &cfg, NULL, exit_hook)
           == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_init(&ctx, &cfg, enter_hook, exit_hook) == ELIB_WDT_OK);
    assert(ctx.enter_critical == enter_hook);
    assert(ctx.exit_critical == exit_hook);
    printf("PASSED\n");
}

void test_init_accepts_optional_callbacks(void) {
    printf("Test: init accepts NULL feed_dog and NULL on_timeout... ");
    elib_wdt_cfg_t cfg = make_cfg();
    cfg.feed_dog = NULL;
    cfg.on_timeout = NULL;
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
    assert(ctx.feed_dog == NULL);
    assert(ctx.on_timeout == NULL);
    printf("PASSED\n");
}

void test_init_reinitializes_dirty_ctx(void) {
    printf("Test: re-init overwrites a running context... ");
    reset();
    assert(elib_wdt_register(&ctx, 5u, "a", 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);

    elib_wdt_cfg_t cfg = make_cfg();
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    assert(tasks[0].registered == 0u);
    printf("PASSED\n");
}

void test_deinit(void) {
    printf("Test: deinit clears the flags and blocks every API... ");
    reset();
    assert(elib_wdt_register(&ctx, 3u, "x", 0u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    assert(elib_wdt_deinit(&ctx) == ELIB_WDT_OK);
    assert(ctx.initialized == 0u);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);

    elib_wdt_task_info_t info;
    elib_wdt_state_t state;
    assert(elib_wdt_register(&ctx, 4u, NULL, 0u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_unregister(&ctx, 3u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_checkin(&ctx, 3u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_feed(&ctx, 3u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_stop(&ctx) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_reset(&ctx) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_manage(&ctx, 1u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_get_info(&ctx, 3u, &info) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_get_state(&ctx, &state) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

void test_deinit_null(void) {
    printf("Test: deinit(NULL) is rejected... ");
    assert(elib_wdt_deinit(NULL) == ELIB_WDT_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_init ===\n");
    test_version();
    test_init_valid();
    test_init_wipes_task_array();
    test_init_invalid_params();
    test_init_hook_pair();
    test_init_accepts_optional_callbacks();
    test_init_reinitializes_dirty_ctx();
    test_deinit();
    test_deinit_null();
    printf("=== test_init: all PASSED ===\n");
    return 0;
}
