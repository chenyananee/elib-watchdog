/* test_register.c --- register validation / inheritance / pool / slot reuse */
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

void test_register_basic(void) {
    printf("Test: register fills the first free slot... ");
    reset();
    assert(elib_wdt_register(&ctx, 42u, "sensor", 500u) == ELIB_WDT_OK);

    elib_wdt_task_t *t = &tasks[0];
    assert(t->registered == 1u);
    assert(t->id == 42u);
    assert(t->name != NULL && strcmp(t->name, "sensor") == 0);
    assert(t->timeout_ms == 500u);
    assert(t->remaining_ms == 500u);
    assert(t->checked_in == 0u);
    printf("PASSED\n");
}

void test_register_inherits_default(void) {
    printf("Test: register with a zero window inherits the global default... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_OK);
    assert(tasks[0].timeout_ms == 1000u);
    assert(tasks[0].remaining_ms == 1000u);
    printf("PASSED\n");
}

void test_register_name_null(void) {
    printf("Test: register accepts a NULL name... ");
    reset();
    assert(elib_wdt_register(&ctx, 1u, NULL, 10u) == ELIB_WDT_OK);
    assert(tasks[0].name == NULL);
    printf("PASSED\n");
}

void test_register_duplicate_id(void) {
    printf("Test: register rejects a duplicate ID... ");
    reset();
    assert(elib_wdt_register(&ctx, 9u, "a", 0u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 9u, "b", 0u)
           == ELIB_WDT_ERR_ALREADY_REGISTERED);
    assert(tasks[0].name != NULL && strcmp(tasks[0].name, "a") == 0);
    printf("PASSED\n");
}

void test_register_pool_full(void) {
    printf("Test: register rejects when the pool is full... ");
    reset();
    for (uint32_t i = 0u; i < MAX_TASKS; i++) {
        assert(elib_wdt_register(&ctx, i, NULL, 0u) == ELIB_WDT_OK);
    }
    assert(elib_wdt_register(&ctx, 100u, NULL, 0u) == ELIB_WDT_ERR_FULL);
    printf("PASSED\n");
}

void test_register_slot_reuse(void) {
    printf("Test: register reuses a slot freed by unregister... ");
    reset();
    for (uint32_t i = 0u; i < MAX_TASKS; i++) {
        assert(elib_wdt_register(&ctx, i, NULL, 0u) == ELIB_WDT_OK);
    }
    assert(elib_wdt_unregister(&ctx, 2u) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 77u, "new", 250u) == ELIB_WDT_OK);

    assert(tasks[2].id == 77u);
    assert(tasks[2].timeout_ms == 250u);
    assert(tasks[2].remaining_ms == 250u);
    assert(tasks[2].registered == 1u);
    printf("PASSED\n");
}

void test_register_invalid(void) {
    printf("Test: register rejects NULL ctx and an uninitialized ctx... ");
    reset();
    assert(elib_wdt_register(NULL, 1u, NULL, 0u) == ELIB_WDT_ERR_INVALID_PARAM);

    (void)elib_wdt_deinit(&ctx);
    assert(elib_wdt_register(&ctx, 1u, NULL, 0u) == ELIB_WDT_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

void test_register_while_running(void) {
    printf("Test: register while running seeds a full window... ");
    reset();
    assert(elib_wdt_start(&ctx) == ELIB_WDT_OK);
    assert(elib_wdt_register(&ctx, 5u, "late", 300u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 300u);

    assert(elib_wdt_manage(&ctx, 100u) == ELIB_WDT_OK);
    assert(tasks[0].remaining_ms == 200u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_register ===\n");
    test_register_basic();
    test_register_inherits_default();
    test_register_name_null();
    test_register_duplicate_id();
    test_register_pool_full();
    test_register_slot_reuse();
    test_register_invalid();
    test_register_while_running();
    printf("=== test_register: all PASSED ===\n");
    return 0;
}
