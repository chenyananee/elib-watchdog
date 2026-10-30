/* test_diagnosis.c --- root-cause vs blocked distinction through checked_in */
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

void test_root_cause_vs_blocked(void) {
    printf("Test: checked_in separates the root cause from blocked tasks... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, "sensor", 100u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 2u, "comm", 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    /* sensor started its iteration and hung; comm never got to start */
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_ERR_TIMEOUT);
    assert(timeout_count == 1u);

    elib_wdt_task_info_t culprit;
    elib_wdt_task_info_t blocked;
    assert(elib_wdt_get_info(&ctx, 1u, &culprit) == ELIB_WDT_OK);
    assert(elib_wdt_get_info(&ctx, 2u, &blocked) == ELIB_WDT_OK);

    assert(culprit.remaining_ms == 0u);
    assert(culprit.checked_in == 1u);   /* ROOT CAUSE: started, never finished */
    assert(blocked.remaining_ms == 0u);
    assert(blocked.checked_in == 0u);   /* BLOCKED: never started */
    printf("PASSED\n");
}

void test_completed_iteration_is_clean(void) {
    printf("Test: a task that checks in and feeds leaves no marker... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    for (uint32_t i = 0u; i < 3u; i++) {
        assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
        assert(elib_wdt_feed(&ctx, 1u) == ELIB_WDT_OK);
        assert(tasks[0].checked_in == 0u);
        assert(elib_wdt_manage(&ctx, 50u) == ELIB_WDT_OK);
    }
    assert(ctx.state == ELIB_WDT_STATE_RUNNING);
    assert(timeout_count == 0u);
    printf("PASSED\n");
}

void test_marker_survives_the_countdown(void) {
    printf("Test: checked_in survives the countdown until the expiry... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);

    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 40u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);
    assert(elib_wdt_manage(&ctx, 60u) == ELIB_WDT_ERR_TIMEOUT);
    assert(tasks[0].checked_in == 1u);
    printf("PASSED\n");
}

void test_start_clears_markers(void) {
    printf("Test: start clears the diagnosis markers for the next run... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 1u);

    assert(elib_wdt_start(&ctx) == ELIB_WDT_ERR_ALREADY_RUNNING);
    assert(elib_wdt_stop(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 0u);
    printf("PASSED\n");
}

void test_reset_clears_markers(void) {
    printf("Test: reset clears the markers and re-arms... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 100u) == ELIB_WDT_OK);
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_checkin(&ctx, 1u) == ELIB_WDT_OK);
    assert(elib_wdt_manage(&ctx, 30u) == ELIB_WDT_OK);

    assert(elib_wdt_reset(&ctx) == ELIB_WDT_OK);
    assert(tasks[0].checked_in == 0u);
    assert(tasks[0].remaining_ms == 100u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_diagnosis ===\n");
    test_root_cause_vs_blocked();
    test_completed_iteration_is_clean();
    test_marker_survives_the_countdown();
    test_start_clears_markers();
    test_reset_clears_markers();
    printf("=== test_diagnosis: all PASSED ===\n");
    return 0;
}
