/* elib_wdt_types.h - Software Watchdog Type Definitions */
#ifndef ELIB_WDT_TYPES_H
#define ELIB_WDT_TYPES_H

#include <stdint.h>
#include "elib_wdt_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Manager state machine states */
typedef enum {
    ELIB_WDT_STATE_IDLE = 0,
    ELIB_WDT_STATE_RUNNING,
    ELIB_WDT_STATE_TIMEOUT,
} elib_wdt_state_e;

/* Forward declaration */
typedef struct elib_wdt_ctx elib_wdt_ctx_t;

/* Callbacks */
typedef void (*elib_wdt_feed_dog_fn)(void);
typedef void (*elib_wdt_on_reset_fn)(const elib_wdt_ctx_t *ctx);

/* Per-task entry (counter is down-counter, 0 = timeout) */
typedef struct {
    uint8_t     task_id;
    const char *name;
    struct {
        uint32_t counter    : 31;
        uint32_t registered : 1;
    } bits;
} elib_wdt_task_t;

/* Configuration (user provides, stored as const pointer) */
typedef struct {
    elib_wdt_feed_dog_fn  feed_dog;
    elib_wdt_on_reset_fn  on_reset;
    uint32_t              timeout_ms;
    elib_wdt_task_t      *tasks;       /* User-allocated task array */
    uint8_t               max_tasks;   /* Capacity of task array */
} elib_wdt_cfg_t;

/* Main context (user-allocated) */
typedef struct elib_wdt_ctx {
    const elib_wdt_cfg_t *cfg;
    uint8_t               task_count;
    elib_wdt_state_e      state;
    struct {
        uint8_t initialized : 1;
    } bit_flags;
} elib_wdt_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_WDT_TYPES_H */