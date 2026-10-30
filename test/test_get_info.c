/* test_get_info.c --- task/state snapshots and parameter validation */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_wdt.h"

#define MAX_TASKS 4u
static elib_wdt_ctx_t ctx;
static elib_wdt_task_t tasks[MAX_TASKS];

static void reset(void) {
    elib_wdt_cfg_t cfg = {
        .feed_dog = NULL,
        .on_timeout = NULL,
        .timeout_ms = 1000u,
        .tasks = tasks,
        .max_tasks = MAX_TASKS,
        .user_data = NULL,
    };
    memset(&ctx, 0, sizeof(ctx));
    memset(tasks, 0, sizeof(tasks));
    assert(elib_wdt_init(&ctx, &cfg, NULL, NULL) == ELIB_WDT_OK);
}

void test_info_initial(void) {
    printf("Test: a fresh task snapshot... ");
    reset();
    assert(elib_wdt_register(&ctx, 7u, "alpha", 500u) == ELIB_WDT_OK);

    elib_wdt_task_info_t info;
    assert(elib_wdt_get_info(&ctx, 7u, &info) == ELIB_WDT_OK);
    assert(info.timeout_ms == 500u);
    assert(info.remaining_ms == 500u);
    assert(info.registered == 1u);
    assert(info.checked_in == 0u);
    printf("PASSED\n");
}

void test_info_after_progress(void) {
    printf("Test: the snapshot follows the countdown... ");
    reset();
    assert(elib_wdt_register(&ctx, 7u, NULL, 500u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    assert(elib_wdt_manage(&ctx, 125u) == ELIB_WDT_OK);
    elib_wdt_task_info_t info;
    assert(elib_wdt_get_info(&ctx, 7u, &info) == ELIB_WDT_OK);
    assert(info.remaining_ms == 375u);
    assert(info.timeout_ms == 500u);

    assert(elib_wdt_checkin(&ctx, 7u) == ELIB_WDT_OK);
    assert(elib_wdt_get_info(&ctx, 7u, &info) == ELIB_WDT_OK);
    assert(info.checked_in == 1u);
    printf("PASSED\n");
}

void test_state_query(void) {
    printf("Test: get_state reports IDLE / RUNNING / TIMEOUT... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);

    elib_wdt_state_t state = ELIB_WDT_STATE_TIMEOUT;
    assert(elib_wdt_get_state(&ctx, &state) == ELIB_WDT_OK);
    assert(state == ELIB_WDT_STATE_IDLE);

    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_get_state(&ctx, &state) == ELIB_WDT_OK);
    assert(state == ELIB_WDT_STATE_RUNNING);

    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(elib_wdt_get_state(&ctx, &state) == ELIB_WDT_OK);
    assert(state == ELIB_WDT_STATE_TIMEOUT);
    printf("PASSED\n");
}

void test_info_unknown_id(void) {
    printf("Test: get_info rejects an unknown or unregistered ID... ");
    reset();
    elib_wdt_task_info_t info;
    assert(elib_wdt_get_info(&ctx, 1u, &info) == ELIB_WDT_ERR_NOT_FOUND);

    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(elib_wdt_get_info(&ctx, 1u, &info) == ELIB_WDT_OK);

    assert(elib_wdt_unregister(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_get_info(&ctx, 1u, &info) == ELIB_WDT_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_query_invalid(void) {
    printf("Test: query APIs reject NULL and stale contexts... ");
    reset();
    elib_wdt_task_info_t info;
    elib_wdt_state_t state;

    assert(elib_wdt_get_info(NULL, 1u, &info) == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_get_info(&ctx, 1u, NULL) == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_get_state(NULL, &state) == ELIB_WDT_ERR_INVALID_PARAM);
    assert(elib_wdt_get_state(&ctx, NULL) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_get_info(&ctx, 1u, &info) == ELIB_WDT_ERR_NOT_INITIALIZED);
    assert(elib_wdt_get_state(&ctx, &state) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_get_info ===\n");
    test_info_initial();
    test_info_after_progress();
    test_state_query();
    test_info_unknown_id();
    test_query_invalid();
    printf("=== test_get_info: all PASSED ===\n");
    return 0;
}
