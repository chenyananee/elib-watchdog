/* elib_wdt.h - Software Watchdog Main Header */
#ifndef ELIB_WDT_H
#define ELIB_WDT_H

#include "elib_wdt_err.h"
#include "elib_wdt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize software watchdog manager
 * @param ctx User-allocated context pointer
 * @param cfg Watchdog configuration (feed_dog, on_reset, timeout_ms)
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_init(elib_wdt_ctx_t *ctx, const elib_wdt_cfg_t *cfg);

/**
 * @brief Deinitialize software watchdog manager
 * @param ctx Context pointer
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_deinit(elib_wdt_ctx_t *ctx);

/**
 * @brief Reset runtime state but keep configuration and task registrations
 * @param ctx Context pointer
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_reset(elib_wdt_ctx_t *ctx);

/**
 * @brief Register a task for monitoring
 * @param ctx Context pointer
 * @param task_id Task identifier
 * @param name Optional task name string (can be NULL)
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_register(elib_wdt_ctx_t *ctx, uint8_t task_id,
                                  const char *name);

/**
 * @brief Unregister a task from monitoring
 * @param ctx Context pointer
 * @param task_id Task identifier
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_unregister(elib_wdt_ctx_t *ctx, uint8_t task_id);

/**
 * @brief Feed (kick) a task's software watchdog
 * @param ctx Context pointer
 * @param task_id Task identifier
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_feed(elib_wdt_ctx_t *ctx, uint8_t task_id);

/**
 * @brief Start watchdog monitoring (IDLE -> RUNNING)
 * @param ctx Context pointer
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_start(elib_wdt_ctx_t *ctx);

/**
 * @brief Stop watchdog monitoring (RUNNING -> IDLE)
 * @param ctx Context pointer
 * @return elib_wdt_err_t error code
 */
elib_wdt_err_t elib_wdt_stop(elib_wdt_ctx_t *ctx);

/**
 * @brief Manage watchdog - call from timer ISR.
 *        When running: checks all tasks, feeds hardware watchdog if healthy,
 *        accumulates time if not. On timeout, stops feeding hardware watchdog,
 *        calls on_reset callback, and enters while(1).
 * @param ctx Context pointer
 * @param elapsed_ms Milliseconds since last call
 * @return elib_wdt_err_t error code (never returns on timeout)
 */
elib_wdt_err_t elib_wdt_manage(elib_wdt_ctx_t *ctx, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_WDT_H */
