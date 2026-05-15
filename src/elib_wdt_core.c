/* elib_wdt_core.c - Software Watchdog Core Implementation */
#include "elib_wdt_core.h"
#include <string.h>

/* Find task slot by task_id, returns index or -1 */
static int find_task_index(const elib_wdt_ctx_t *ctx, uint8_t task_id) {
    for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
        if (ctx->cfg->tasks[i].registered && ctx->cfg->tasks[i].task_id == task_id) {
            return (int)i;
        }
    }
    return -1;
}

/* Find first free task slot, returns index or -1 */
static int find_free_slot(const elib_wdt_ctx_t *ctx) {
    for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
        if (!ctx->cfg->tasks[i].registered) {
            return (int)i;
        }
    }
    return -1;
}

/* Initialize software watchdog manager */
elib_wdt_err_t elib_wdt_init(elib_wdt_ctx_t *ctx, const elib_wdt_cfg_t *cfg) {
    if (ctx == NULL || cfg == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (cfg->feed_dog == NULL || cfg->on_reset == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (cfg->timeout_ms == 0) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (cfg->tasks == NULL || cfg->max_tasks == 0) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (ctx->initialized) {
        return ELIB_WDT_ERR_ALREADY_INITIALIZED;
    }

    memset(ctx, 0, sizeof(elib_wdt_ctx_t));
    ctx->cfg = cfg;
    ctx->initialized = 1;

    return ELIB_WDT_OK;
}

/* Deinitialize software watchdog manager */
elib_wdt_err_t elib_wdt_deinit(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    ctx->initialized = 0;
    return ELIB_WDT_OK;
}

/* Reset runtime state but keep configuration and registrations */
elib_wdt_err_t elib_wdt_reset(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->cfg == NULL || ctx->cfg->tasks == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
        ctx->cfg->tasks[i].status = ELIB_WDT_TASK_IDLE;
    }
    ctx->elapsed_ms = 0;
    ctx->state = ELIB_WDT_STATE_IDLE;

    return ELIB_WDT_OK;
}

/* Register a task for monitoring */
elib_wdt_err_t elib_wdt_register(elib_wdt_ctx_t *ctx, uint8_t task_id,
                                  const char *name) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->cfg == NULL || ctx->cfg->tasks == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    if (find_task_index(ctx, task_id) >= 0) {
        return ELIB_WDT_ERR_ALREADY_REGISTERED;
    }

    if (ctx->task_count >= ctx->cfg->max_tasks) {
        return ELIB_WDT_ERR_FULL;
    }

    int slot = find_free_slot(ctx);
    if (slot < 0) {
        return ELIB_WDT_ERR_FULL;
    }

    ctx->cfg->tasks[slot].task_id = task_id;
    ctx->cfg->tasks[slot].name = name;
    ctx->cfg->tasks[slot].status = ELIB_WDT_TASK_IDLE;
    ctx->cfg->tasks[slot].registered = 1;
    ctx->task_count++;

    return ELIB_WDT_OK;
}

/* Unregister a task */
elib_wdt_err_t elib_wdt_unregister(elib_wdt_ctx_t *ctx, uint8_t task_id) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->cfg == NULL || ctx->cfg->tasks == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    int idx = find_task_index(ctx, task_id);
    if (idx < 0) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    ctx->cfg->tasks[idx].registered = 0;
    ctx->cfg->tasks[idx].task_id = 0;
    ctx->cfg->tasks[idx].name = NULL;
    ctx->cfg->tasks[idx].status = ELIB_WDT_TASK_IDLE;
    ctx->task_count--;

    return ELIB_WDT_OK;
}

/* Feed a task's software watchdog */
elib_wdt_err_t elib_wdt_feed(elib_wdt_ctx_t *ctx, uint8_t task_id) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->cfg == NULL || ctx->cfg->tasks == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    int idx = find_task_index(ctx, task_id);
    if (idx < 0) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    ctx->cfg->tasks[idx].status = ELIB_WDT_TASK_FED;
    return ELIB_WDT_OK;
}

/* Check in a task to signal it has started */
elib_wdt_err_t elib_wdt_checkin(elib_wdt_ctx_t *ctx, uint8_t task_id) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->cfg == NULL || ctx->cfg->tasks == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    int idx = find_task_index(ctx, task_id);
    if (idx < 0) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    if (ctx->cfg->tasks[idx].status == ELIB_WDT_TASK_IDLE) {
        ctx->cfg->tasks[idx].status = ELIB_WDT_TASK_CHECKIN;
    }

    return ELIB_WDT_OK;
}

/* Start watchdog monitoring */
elib_wdt_err_t elib_wdt_start(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->cfg == NULL || ctx->cfg->tasks == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    if (ctx->state == ELIB_WDT_STATE_RUNNING) {
        return ELIB_WDT_OK;
    }

    for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
        if (ctx->cfg->tasks[i].registered) {
            ctx->cfg->tasks[i].status = ELIB_WDT_TASK_IDLE;
        }
    }
    ctx->elapsed_ms = 0;
    ctx->state = ELIB_WDT_STATE_RUNNING;

    return ELIB_WDT_OK;
}

/* Stop watchdog monitoring */
elib_wdt_err_t elib_wdt_stop(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    ctx->state = ELIB_WDT_STATE_IDLE;
    return ELIB_WDT_OK;
}

/* Manage watchdog - call periodically */
elib_wdt_err_t elib_wdt_manage(elib_wdt_ctx_t *ctx, uint32_t elapsed_ms) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->state != ELIB_WDT_STATE_RUNNING) {
        return ELIB_WDT_ERR_NOT_RUNNING;
    }
    if (ctx->cfg == NULL || ctx->cfg->feed_dog == NULL || ctx->cfg->on_reset == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    /* No tasks registered: just feed and return */
    if (ctx->task_count == 0) {
        ctx->cfg->feed_dog();
        return ELIB_WDT_OK;
    }

    /* Check if all registered tasks have been fed */
    uint8_t all_fed = 1;
    for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
        if (ctx->cfg->tasks[i].registered
            && ctx->cfg->tasks[i].status != ELIB_WDT_TASK_FED) {
            all_fed = 0;
            break;
        }
    }

    if (all_fed) {
        /* All tasks healthy: reset window, feed hardware dog */
        ctx->elapsed_ms = 0;
        for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
            if (ctx->cfg->tasks[i].registered) {
                ctx->cfg->tasks[i].status = ELIB_WDT_TASK_IDLE;
            }
        }
        ctx->cfg->feed_dog();
    } else {
        /* Not all tasks fed: accumulate time */
        ctx->elapsed_ms += elapsed_ms;
        if (ctx->elapsed_ms >= ctx->cfg->timeout_ms) {
            /* Timeout: stop feeding, call reset, halt */
            ctx->state = ELIB_WDT_STATE_TIMEOUT;
            ctx->cfg->on_reset(ctx);
            while (1) {}
        } else {
            /* Within grace period: keep hardware alive */
            ctx->cfg->feed_dog();
        }
    }

    return ELIB_WDT_OK;
}
