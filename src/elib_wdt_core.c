/* ============================================================================
 * elib_wdt_core.c --- elib watchdog implementation (v2)
 * ============================================================================
 * The single source file; includes the umbrella header directly. The semantic
 * contract lives in the header banner of elib_wdt.h:
 *   - every registered task owns an independent down-counter;
 *   - manage() only subtracts time and feeds the hardware watchdog; it never
 *     blocks, and on expiry it returns ELIB_WDT_ERR_TIMEOUT with the hardware
 *     watchdog left unfed (the board resets itself);
 *   - the TIMEOUT state is sticky until start() or reset() re-arms the windows;
 *   - checked_in distinguishes the root-cause task from the blocked ones.
 * ==========================================================================*/
#include "elib_wdt.h"
#include <string.h>

/* -------------------------------------------------------------- internal helpers */

/* Enter the optional user critical section (init guarantees a matched pair) */
static void wdt_lock(elib_wdt_ctx_t *ctx) {
    if (ctx->enter_critical != NULL) {
        ctx->enter_critical();
    }
}

/* Leave the optional user critical section */
static void wdt_unlock(elib_wdt_ctx_t *ctx) {
    if (ctx->exit_critical != NULL) {
        ctx->exit_critical();
    }
}

/* Look up a registered task by application ID; NULL when not registered */
static elib_wdt_task_t *wdt_find_task(elib_wdt_ctx_t *ctx, uint32_t task_id) {
    for (uint32_t i = 0u; i < ctx->max_tasks; i++) {
        elib_wdt_task_t *task = &ctx->tasks[i];
        if (task->registered && task->id == task_id) {
            return task;
        }
    }
    return NULL;
}

/* Reload every registered window and clear the diagnosis flags */
static void wdt_rearm_all(elib_wdt_ctx_t *ctx) {
    for (uint32_t i = 0u; i < ctx->max_tasks; i++) {
        elib_wdt_task_t *task = &ctx->tasks[i];
        if (task->registered) {
            task->remaining_ms = task->timeout_ms;
            task->checked_in = 0u;
        }
    }
}

/* --------------------------------------------------------------- lifecycle */

elib_wdt_err_t elib_wdt_init(elib_wdt_ctx_t *ctx,
                             const elib_wdt_cfg_t *cfg,
                             void (*enter_critical)(void),
                             void (*exit_critical)(void)) {
    if (ctx == NULL || cfg == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (cfg->tasks == NULL || cfg->max_tasks == 0u || cfg->timeout_ms == 0u) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    /* A one-sided hook pair would leave the interrupt state inconsistent */
    if ((enter_critical == NULL) != (exit_critical == NULL)) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    /* Unconditional re-initialization: the task array must be wiped too, since
     * its previous contents decide which slots look free */
    memset(ctx, 0, sizeof(*ctx));
    memset(cfg->tasks, 0, sizeof(elib_wdt_task_t) * cfg->max_tasks);

    ctx->feed_dog = cfg->feed_dog;
    ctx->on_timeout = cfg->on_timeout;
    ctx->default_timeout_ms = cfg->timeout_ms;
    ctx->tasks = cfg->tasks;
    ctx->max_tasks = cfg->max_tasks;
    ctx->user_data = cfg->user_data;
    ctx->enter_critical = enter_critical;
    ctx->exit_critical = exit_critical;
    ctx->state = ELIB_WDT_STATE_IDLE;
    ctx->initialized = 1u;

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_deinit(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }

    /* Stop monitoring; configuration and registrations are left in place but
     * become inaccessible because every other API checks initialized */
    ctx->state = ELIB_WDT_STATE_IDLE;
    ctx->initialized = 0u;

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_reset(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    wdt_lock(ctx);
    wdt_rearm_all(ctx);
    ctx->state = ELIB_WDT_STATE_IDLE;
    wdt_unlock(ctx);

    return ELIB_WDT_OK;
}

/* ------------------------------------------------------------- task control */

elib_wdt_err_t elib_wdt_register(elib_wdt_ctx_t *ctx,
                                 uint32_t task_id,
                                 const char *name,
                                 uint32_t timeout_ms) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (wdt_find_task(ctx, task_id) != NULL) {
        return ELIB_WDT_ERR_ALREADY_REGISTERED;
    }

    /* timeout_ms == 0 inherits the global default (init rejects a zero default) */
    uint32_t window = (timeout_ms != 0u) ? timeout_ms : ctx->default_timeout_ms;

    elib_wdt_task_t *slot = NULL;
    for (uint32_t i = 0u; i < ctx->max_tasks; i++) {
        if (!ctx->tasks[i].registered) {
            slot = &ctx->tasks[i];
            break;
        }
    }
    if (slot == NULL) {
        return ELIB_WDT_ERR_FULL;
    }

    wdt_lock(ctx);
    slot->id = task_id;
    slot->name = name;
    slot->timeout_ms = window;
    slot->remaining_ms = window;
    slot->checked_in = 0u;
    slot->registered = 1u;
    wdt_unlock(ctx);

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_unregister(elib_wdt_ctx_t *ctx, uint32_t task_id) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    elib_wdt_task_t *task = wdt_find_task(ctx, task_id);
    if (task == NULL) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    wdt_lock(ctx);
    memset(task, 0, sizeof(*task));   /* registered = 0, slot reusable */
    wdt_unlock(ctx);

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_checkin(elib_wdt_ctx_t *ctx, uint32_t task_id) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    elib_wdt_task_t *task = wdt_find_task(ctx, task_id);
    if (task == NULL) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    /* Diagnostics only: the timer is deliberately untouched */
    task->checked_in = 1u;

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_feed(elib_wdt_ctx_t *ctx, uint32_t task_id) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    elib_wdt_task_t *task = wdt_find_task(ctx, task_id);
    if (task == NULL) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    wdt_lock(ctx);
    task->remaining_ms = task->timeout_ms;
    task->checked_in = 0u;
    wdt_unlock(ctx);

    return ELIB_WDT_OK;
}

/* ------------------------------------------------------- scheduler control */

elib_wdt_err_t elib_wdt_start(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->state == ELIB_WDT_STATE_RUNNING) {
        return ELIB_WDT_ERR_ALREADY_RUNNING;
    }

    wdt_lock(ctx);
    wdt_rearm_all(ctx);
    ctx->state = ELIB_WDT_STATE_RUNNING;
    wdt_unlock(ctx);

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_stop(elib_wdt_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    if (ctx->state == ELIB_WDT_STATE_IDLE) {
        return ELIB_WDT_ERR_NOT_RUNNING;
    }

    wdt_lock(ctx);
    ctx->state = ELIB_WDT_STATE_IDLE;
    wdt_unlock(ctx);

    return ELIB_WDT_OK;
}

/* ------------------------------------------------------------------ driver */

elib_wdt_err_t elib_wdt_manage(elib_wdt_ctx_t *ctx, uint32_t elapsed_ms) {
    if (ctx == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }
    /* Sticky timeout: the hardware watchdog stays unfed and the callback is not
     * fired again until start()/reset() re-arms the windows */
    if (ctx->state == ELIB_WDT_STATE_TIMEOUT) {
        return ELIB_WDT_ERR_TIMEOUT;
    }
    /* Not monitoring: ISR-friendly no-op (no error, no feeding) */
    if (ctx->state != ELIB_WDT_STATE_RUNNING) {
        return ELIB_WDT_OK;
    }
    /* No time passed: nothing can expire and the dog is deliberately not fed,
     * so a busy loop calling manage(0) cannot mask a stalled main loop */
    if (elapsed_ms == 0u) {
        return ELIB_WDT_OK;
    }

    uint32_t expired = 0u;

    wdt_lock(ctx);
    for (uint32_t i = 0u; i < ctx->max_tasks; i++) {
        elib_wdt_task_t *task = &ctx->tasks[i];
        if (!task->registered) {
            continue;
        }
        uint32_t remaining = task->remaining_ms;
        if (remaining <= elapsed_ms) {
            task->remaining_ms = 0u;
            expired = 1u;
        } else {
            task->remaining_ms = remaining - elapsed_ms;
        }
    }
    if (expired) {
        ctx->state = ELIB_WDT_STATE_TIMEOUT;
    }
    wdt_unlock(ctx);

    if (expired) {
        /* Callback runs outside the critical section, in the manage() caller
         * context (usually the timer ISR) */
        if (ctx->on_timeout != NULL) {
            ctx->on_timeout(ctx->user_data);
        }
        return ELIB_WDT_ERR_TIMEOUT;
    }

    /* Healthy: keep the hardware watchdog alive */
    if (ctx->feed_dog != NULL) {
        ctx->feed_dog();
    }

    return ELIB_WDT_OK;
}

/* ------------------------------------------------------------------ queries */

elib_wdt_err_t elib_wdt_get_info(elib_wdt_ctx_t *ctx,
                                 uint32_t task_id,
                                 elib_wdt_task_info_t *out_info) {
    if (ctx == NULL || out_info == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    elib_wdt_task_t *task = wdt_find_task(ctx, task_id);
    if (task == NULL) {
        return ELIB_WDT_ERR_NOT_FOUND;
    }

    out_info->timeout_ms = task->timeout_ms;
    out_info->remaining_ms = task->remaining_ms;
    out_info->registered = task->registered;
    out_info->checked_in = task->checked_in;

    return ELIB_WDT_OK;
}

elib_wdt_err_t elib_wdt_get_state(elib_wdt_ctx_t *ctx,
                                  elib_wdt_state_t *out_state) {
    if (ctx == NULL || out_state == NULL) {
        return ELIB_WDT_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_WDT_ERR_NOT_INITIALIZED;
    }

    *out_state = ctx->state;

    return ELIB_WDT_OK;
}
