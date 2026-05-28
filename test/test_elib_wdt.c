/* test_elib_wdt.c - Software Watchdog Unit Tests */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include "../include/elib_wdt.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do { \
    printf("Test: %s... ", #fn); \
    tests_run++; \
    fn(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define MAX_TASKS 8

static elib_wdt_ctx_t test_ctx;
static elib_wdt_task_t test_tasks[MAX_TASKS];
static int feed_dog_count;
static int on_reset_count;
static const elib_wdt_ctx_t *on_reset_ctx_arg;
static jmp_buf timeout_jmp;

/* Mock callbacks */
static void mock_feed_dog(void) {
    feed_dog_count++;
}

static void mock_on_reset(const elib_wdt_ctx_t *ctx) {
    on_reset_count++;
    on_reset_ctx_arg = ctx;
    longjmp(timeout_jmp, 1);
}

/* Factory helper for config */
static elib_wdt_cfg_t make_default_cfg(void) {
    elib_wdt_cfg_t cfg = {
        .feed_dog  = mock_feed_dog,
        .on_reset  = mock_on_reset,
        .timeout_ms = 1000,
        .tasks     = test_tasks,
        .max_tasks = MAX_TASKS,
    };
    return cfg;
}

/* Reset test state */
static void reset_test(void) {
    memset(test_tasks, 0, sizeof(test_tasks));
    memset(&test_ctx, 0, sizeof(test_ctx));
    feed_dog_count = 0;
    on_reset_count = 0;
    on_reset_ctx_arg = NULL;
    elib_wdt_cfg_t cfg = make_default_cfg();
    elib_wdt_init(&test_ctx, &cfg);
}

/* --- Init tests --- */

static void test_init_valid(void) {
    elib_wdt_task_t tasks[4];
    memset(tasks, 0, sizeof(tasks));
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = {
        .feed_dog  = mock_feed_dog,
        .on_reset  = mock_on_reset,
        .timeout_ms = 1000,
        .tasks     = tasks,
        .max_tasks = 4,
    };
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_OK);
    assert(ctx.bit_flags.initialized == 1);
    assert(ctx.cfg == &cfg);
    assert(ctx.state == ELIB_WDT_STATE_IDLE);
    assert(ctx.task_count == 0);
    assert(ctx.elapsed_ms == 0);
}

static void test_init_null_ctx(void) {
    elib_wdt_cfg_t cfg = make_default_cfg();
    elib_wdt_err_t err = elib_wdt_init(NULL, &cfg);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_null_cfg(void) {
    elib_wdt_ctx_t ctx;
    elib_wdt_err_t err = elib_wdt_init(&ctx, NULL);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_null_feed_dog(void) {
    elib_wdt_task_t tasks[4];
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = { .feed_dog = NULL, .on_reset = mock_on_reset, .timeout_ms = 1000, .tasks = tasks, .max_tasks = 4 };
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_null_on_reset(void) {
    elib_wdt_task_t tasks[4];
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = { .feed_dog = mock_feed_dog, .on_reset = NULL, .timeout_ms = 1000, .tasks = tasks, .max_tasks = 4 };
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_zero_timeout(void) {
    elib_wdt_task_t tasks[4];
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = { .feed_dog = mock_feed_dog, .on_reset = mock_on_reset, .timeout_ms = 0, .tasks = tasks, .max_tasks = 4 };
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_null_tasks(void) {
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = { .feed_dog = mock_feed_dog, .on_reset = mock_on_reset, .timeout_ms = 1000, .tasks = NULL, .max_tasks = 4 };
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_zero_max_tasks(void) {
    elib_wdt_task_t tasks[4];
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = { .feed_dog = mock_feed_dog, .on_reset = mock_on_reset, .timeout_ms = 1000, .tasks = tasks, .max_tasks = 0 };
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_init_already_initialized(void) {
    elib_wdt_task_t tasks[4];
    memset(tasks, 0, sizeof(tasks));
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = { .feed_dog = mock_feed_dog, .on_reset = mock_on_reset, .timeout_ms = 1000, .tasks = tasks, .max_tasks = 4 };
    elib_wdt_init(&ctx, &cfg);
    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_ERR_ALREADY_INITIALIZED);
}

/* --- Deinit tests --- */

static void test_deinit_valid(void) {
    elib_wdt_task_t tasks[4];
    memset(tasks, 0, sizeof(tasks));
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = make_default_cfg();
    elib_wdt_init(&ctx, &cfg);
    elib_wdt_err_t err = elib_wdt_deinit(&ctx);
    assert(err == ELIB_WDT_OK);
    assert(ctx.bit_flags.initialized == 0);
}

static void test_deinit_null(void) {
    elib_wdt_err_t err = elib_wdt_deinit(NULL);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

/* --- Reset tests --- */

static void test_reset_clears_runtime(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_start(&test_ctx);
    elib_wdt_feed(&test_ctx, 0);
    test_ctx.elapsed_ms = 500;

    elib_wdt_err_t err = elib_wdt_reset(&test_ctx);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.elapsed_ms == 0);
    assert(test_ctx.state == ELIB_WDT_STATE_IDLE);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_IDLE);
    assert(test_ctx.cfg->tasks[1].bit_flags.status == ELIB_WDT_TASK_IDLE);
    /* Registrations preserved */
    assert(test_ctx.task_count == 2);
    assert(test_ctx.cfg->tasks[0].bit_flags.registered == 1);
    assert(test_ctx.cfg->tasks[1].bit_flags.registered == 1);
}

static void test_reset_null_ctx(void) {
    elib_wdt_err_t err = elib_wdt_reset(NULL);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_reset_not_initialized(void) {
    elib_wdt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_wdt_err_t err = elib_wdt_reset(&ctx);
    assert(err == ELIB_WDT_ERR_NOT_INITIALIZED);
}

/* --- Register tests --- */

static void test_register_valid(void) {
    reset_test();
    elib_wdt_err_t err = elib_wdt_register(&test_ctx, 0, NULL);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.task_count == 1);
    assert(test_ctx.cfg->tasks[0].task_id == 0);
    assert(test_ctx.cfg->tasks[0].bit_flags.registered == 1);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_IDLE);
}

static void test_register_with_name(void) {
    reset_test();
    elib_wdt_err_t err = elib_wdt_register(&test_ctx, 1, "sensor");
    assert(err == ELIB_WDT_OK);
    assert(strcmp(test_ctx.cfg->tasks[0].name, "sensor") == 0);
}

static void test_register_duplicate(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_err_t err = elib_wdt_register(&test_ctx, 0, "task0_dup");
    assert(err == ELIB_WDT_ERR_ALREADY_REGISTERED);
    assert(test_ctx.task_count == 1);
}

static void test_register_full(void) {
    reset_test();
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
        elib_wdt_err_t err = elib_wdt_register(&test_ctx, i, NULL);
        assert(err == ELIB_WDT_OK);
    }
    elib_wdt_err_t err = elib_wdt_register(&test_ctx, MAX_TASKS, NULL);
    assert(err == ELIB_WDT_ERR_FULL);
}

static void test_register_null_ctx(void) {
    elib_wdt_err_t err = elib_wdt_register(NULL, 0, NULL);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_register_not_initialized(void) {
    elib_wdt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_wdt_err_t err = elib_wdt_register(&ctx, 0, NULL);
    assert(err == ELIB_WDT_ERR_NOT_INITIALIZED);
}

/* --- Unregister tests --- */

static void test_unregister_valid(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_err_t err = elib_wdt_unregister(&test_ctx, 0);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.task_count == 1);
    assert(test_ctx.cfg->tasks[0].bit_flags.registered == 0);
    /* Task 1 still registered */
    assert(test_ctx.cfg->tasks[1].bit_flags.registered == 1);
    assert(test_ctx.cfg->tasks[1].task_id == 1);
}

static void test_unregister_not_found(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_err_t err = elib_wdt_unregister(&test_ctx, 99);
    assert(err == ELIB_WDT_ERR_NOT_FOUND);
}

static void test_unregister_null_ctx(void) {
    elib_wdt_err_t err = elib_wdt_unregister(NULL, 0);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

/* --- Feed tests --- */

static void test_feed_valid(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_err_t err = elib_wdt_feed(&test_ctx, 0);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_FED);
}

static void test_feed_not_found(void) {
    reset_test();
    elib_wdt_err_t err = elib_wdt_feed(&test_ctx, 99);
    assert(err == ELIB_WDT_ERR_NOT_FOUND);
}

static void test_feed_null_ctx(void) {
    elib_wdt_err_t err = elib_wdt_feed(NULL, 0);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_feed_not_initialized(void) {
    elib_wdt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_wdt_err_t err = elib_wdt_feed(&ctx, 0);
    assert(err == ELIB_WDT_ERR_NOT_INITIALIZED);
}

/* --- Checkin tests --- */

static void test_checkin_valid(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_err_t err = elib_wdt_checkin(&test_ctx, 0);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_CHECKIN);
}

static void test_checkin_then_feed(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_checkin(&test_ctx, 0);
    elib_wdt_feed(&test_ctx, 0);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_FED);
}

static void test_checkin_not_found(void) {
    reset_test();
    elib_wdt_err_t err = elib_wdt_checkin(&test_ctx, 99);
    assert(err == ELIB_WDT_ERR_NOT_FOUND);
}

static void test_checkin_null_ctx(void) {
    elib_wdt_err_t err = elib_wdt_checkin(NULL, 0);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

static void test_checkin_not_initialized(void) {
    elib_wdt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_wdt_err_t err = elib_wdt_checkin(&ctx, 0);
    assert(err == ELIB_WDT_ERR_NOT_INITIALIZED);
}

static void test_checkin_idempotent(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_checkin(&test_ctx, 0);
    elib_wdt_err_t err = elib_wdt_checkin(&test_ctx, 0);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_CHECKIN);
}

/* Timeout: verify diagnostic status values distinguish stuck vs blocked */
static void test_timeout_diagnosis(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "stuck_task");
    elib_wdt_register(&test_ctx, 1, "blocked_task");
    elib_wdt_start(&test_ctx);

    /* Task 0 checks in (starts) but never feeds -> STUCK */
    elib_wdt_checkin(&test_ctx, 0);
    /* Task 1 never checks in -> BLOCKED (stays IDLE) */

    if (setjmp(timeout_jmp) == 0) {
        elib_wdt_manage(&test_ctx, 2000);
        assert(0);
    }
    assert(on_reset_count == 1);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_CHECKIN);
    assert(test_ctx.cfg->tasks[1].bit_flags.status == ELIB_WDT_TASK_IDLE);
}

/* --- Start/Stop tests --- */

static void test_start_transitions_to_running(void) {
    reset_test();
    elib_wdt_err_t err = elib_wdt_start(&test_ctx);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.state == ELIB_WDT_STATE_RUNNING);
    assert(test_ctx.elapsed_ms == 0);
}

static void test_start_idempotent(void) {
    reset_test();
    elib_wdt_start(&test_ctx);
    elib_wdt_err_t err = elib_wdt_start(&test_ctx);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.state == ELIB_WDT_STATE_RUNNING);
}

static void test_stop_transitions_to_idle(void) {
    reset_test();
    elib_wdt_start(&test_ctx);
    elib_wdt_err_t err = elib_wdt_stop(&test_ctx);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.state == ELIB_WDT_STATE_IDLE);
}

static void test_stop_idempotent(void) {
    reset_test();
    /* Already IDLE */
    elib_wdt_err_t err = elib_wdt_stop(&test_ctx);
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.state == ELIB_WDT_STATE_IDLE);
}

/* --- Manage tests --- */

static void test_manage_not_running(void) {
    reset_test();
    elib_wdt_err_t err = elib_wdt_manage(&test_ctx, 10);
    assert(err == ELIB_WDT_ERR_NOT_RUNNING);
}

static void test_manage_not_initialized(void) {
    elib_wdt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_wdt_err_t err = elib_wdt_manage(&ctx, 10);
    assert(err == ELIB_WDT_ERR_NOT_INITIALIZED);
}

static void test_manage_null_ctx(void) {
    elib_wdt_err_t err = elib_wdt_manage(NULL, 10);
    assert(err == ELIB_WDT_ERR_INVALID_PARAM);
}

/* All tasks fed: feed_dog called, elapsed reset, fed cleared */
static void test_manage_all_fed(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_start(&test_ctx);

    elib_wdt_feed(&test_ctx, 0);
    elib_wdt_feed(&test_ctx, 1);

    feed_dog_count = 0;
    elib_wdt_err_t err = elib_wdt_manage(&test_ctx, 10);

    assert(err == ELIB_WDT_OK);
    assert(feed_dog_count == 1);
    assert(test_ctx.elapsed_ms == 0);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_IDLE);
    assert(test_ctx.cfg->tasks[1].bit_flags.status == ELIB_WDT_TASK_IDLE);
}

/* Not all fed, within timeout: feed_dog called, elapsed accumulates */
static void test_manage_partial_fed_within_timeout(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_start(&test_ctx);

    /* Feed only task 0 */
    elib_wdt_feed(&test_ctx, 0);

    feed_dog_count = 0;
    elib_wdt_err_t err = elib_wdt_manage(&test_ctx, 100);

    assert(err == ELIB_WDT_OK);
    assert(feed_dog_count == 1);
    assert(test_ctx.elapsed_ms == 100);
}

/* Timeout: on_reset called with ctx, while(1) escaped via longjmp */
static void test_manage_timeout_triggers_reset(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_start(&test_ctx);

    /* Do NOT feed task 0, accumulate to just under timeout */
    elib_wdt_manage(&test_ctx, 500);

    /* This call pushes past timeout -> on_reset + while(1) */
    if (setjmp(timeout_jmp) == 0) {
        elib_wdt_manage(&test_ctx, 600);
        /* Should not reach here */
        assert(0);
    }
    assert(on_reset_count == 1);
    assert(on_reset_ctx_arg == &test_ctx);
    assert(test_ctx.state == ELIB_WDT_STATE_TIMEOUT);
}

/* No tasks registered: feed_dog called */
static void test_manage_no_tasks(void) {
    reset_test();
    elib_wdt_start(&test_ctx);

    feed_dog_count = 0;
    elib_wdt_err_t err = elib_wdt_manage(&test_ctx, 10);

    assert(err == ELIB_WDT_OK);
    assert(feed_dog_count == 1);
}

/* Multiple manage calls with gradual feeding */
static void test_manage_multi_cycle(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_start(&test_ctx);

    /* Cycle 1: only task 0 fed */
    elib_wdt_feed(&test_ctx, 0);
    elib_wdt_manage(&test_ctx, 100);
    assert(test_ctx.elapsed_ms == 100);

    /* Cycle 2: both fed -> window resets */
    elib_wdt_feed(&test_ctx, 0);
    elib_wdt_feed(&test_ctx, 1);
    feed_dog_count = 0;
    elib_wdt_manage(&test_ctx, 100);
    assert(test_ctx.elapsed_ms == 0);
    assert(feed_dog_count == 1);

    /* Cycle 3: no feeding -> accumulates again */
    elib_wdt_manage(&test_ctx, 200);
    assert(test_ctx.elapsed_ms == 200);
}

/* Feed task after partial accumulation -> full reset of window */
static void test_manage_late_feed_resets_window(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_start(&test_ctx);

    /* Accumulate 500ms without feeding */
    elib_wdt_manage(&test_ctx, 500);
    assert(test_ctx.elapsed_ms == 500);

    /* Now feed the task */
    elib_wdt_feed(&test_ctx, 0);
    elib_wdt_manage(&test_ctx, 10);
    assert(test_ctx.elapsed_ms == 0);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_IDLE);
}

/* Unregister a task, then verify manage works with remaining tasks */
static void test_manage_after_unregister(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_start(&test_ctx);

    /* Feed only task 0, unregister task 1 */
    elib_wdt_feed(&test_ctx, 0);
    elib_wdt_unregister(&test_ctx, 1);

    feed_dog_count = 0;
    elib_wdt_err_t err = elib_wdt_manage(&test_ctx, 10);
    assert(err == ELIB_WDT_OK);
    assert(feed_dog_count == 1);
    assert(test_ctx.elapsed_ms == 0);
}

/* Re-init after deinit should succeed */
static void test_reinit_after_deinit(void) {
    elib_wdt_task_t tasks[4];
    memset(tasks, 0, sizeof(tasks));
    elib_wdt_ctx_t ctx;
    elib_wdt_cfg_t cfg = make_default_cfg();
    elib_wdt_init(&ctx, &cfg);
    elib_wdt_register(&ctx, 0, "task0");
    elib_wdt_deinit(&ctx);

    elib_wdt_err_t err = elib_wdt_init(&ctx, &cfg);
    assert(err == ELIB_WDT_OK);
    assert(ctx.bit_flags.initialized == 1);
    assert(ctx.task_count == 0);
}

/* Register after unregister reuses slot */
static void test_register_after_unregister(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_register(&test_ctx, 1, "task1");
    elib_wdt_unregister(&test_ctx, 0);

    elib_wdt_err_t err = elib_wdt_register(&test_ctx, 2, "task2");
    assert(err == ELIB_WDT_OK);
    assert(test_ctx.task_count == 2);
}

/* Start clears fed flags */
static void test_start_clears_fed_flags(void) {
    reset_test();
    elib_wdt_register(&test_ctx, 0, "task0");
    elib_wdt_feed(&test_ctx, 0);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_FED);

    elib_wdt_start(&test_ctx);
    assert(test_ctx.cfg->tasks[0].bit_flags.status == ELIB_WDT_TASK_IDLE);
}

int main(void) {
    printf("=== elib-watchdog tests ===\n\n");

    /* Init tests */
    RUN_TEST(test_init_valid);
    RUN_TEST(test_init_null_ctx);
    RUN_TEST(test_init_null_cfg);
    RUN_TEST(test_init_null_feed_dog);
    RUN_TEST(test_init_null_on_reset);
    RUN_TEST(test_init_zero_timeout);
    RUN_TEST(test_init_null_tasks);
    RUN_TEST(test_init_zero_max_tasks);
    RUN_TEST(test_init_already_initialized);

    /* Deinit tests */
    RUN_TEST(test_deinit_valid);
    RUN_TEST(test_deinit_null);

    /* Reset tests */
    RUN_TEST(test_reset_clears_runtime);
    RUN_TEST(test_reset_null_ctx);
    RUN_TEST(test_reset_not_initialized);

    /* Register tests */
    RUN_TEST(test_register_valid);
    RUN_TEST(test_register_with_name);
    RUN_TEST(test_register_duplicate);
    RUN_TEST(test_register_full);
    RUN_TEST(test_register_null_ctx);
    RUN_TEST(test_register_not_initialized);

    /* Unregister tests */
    RUN_TEST(test_unregister_valid);
    RUN_TEST(test_unregister_not_found);
    RUN_TEST(test_unregister_null_ctx);

    /* Feed tests */
    RUN_TEST(test_feed_valid);
    RUN_TEST(test_feed_not_found);
    RUN_TEST(test_feed_null_ctx);
    RUN_TEST(test_feed_not_initialized);

    /* Checkin tests */
    RUN_TEST(test_checkin_valid);
    RUN_TEST(test_checkin_then_feed);
    RUN_TEST(test_checkin_not_found);
    RUN_TEST(test_checkin_null_ctx);
    RUN_TEST(test_checkin_not_initialized);
    RUN_TEST(test_checkin_idempotent);
    RUN_TEST(test_timeout_diagnosis);

    /* Start/Stop tests */
    RUN_TEST(test_start_transitions_to_running);
    RUN_TEST(test_start_idempotent);
    RUN_TEST(test_stop_transitions_to_idle);
    RUN_TEST(test_stop_idempotent);

    /* Manage tests */
    RUN_TEST(test_manage_not_running);
    RUN_TEST(test_manage_not_initialized);
    RUN_TEST(test_manage_null_ctx);
    RUN_TEST(test_manage_all_fed);
    RUN_TEST(test_manage_partial_fed_within_timeout);
    RUN_TEST(test_manage_timeout_triggers_reset);
    RUN_TEST(test_manage_no_tasks);
    RUN_TEST(test_manage_multi_cycle);
    RUN_TEST(test_manage_late_feed_resets_window);
    RUN_TEST(test_manage_after_unregister);

    /* Lifecycle tests */
    RUN_TEST(test_reinit_after_deinit);
    RUN_TEST(test_register_after_unregister);
    RUN_TEST(test_start_clears_fed_flags);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}