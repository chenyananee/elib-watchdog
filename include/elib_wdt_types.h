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

/* Callbacks */
typedef void (*elib_wdt_feed_dog_fn)(void);
typedef void (*elib_wdt_on_reset_fn)(void);

/* Per-task entry */
typedef struct {
    uint8_t     task_id;
    const char *name;
    uint8_t     fed;
    uint8_t     registered;
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
typedef struct {
    const elib_wdt_cfg_t *cfg;
    uint8_t               task_count;
    elib_wdt_state_e      state;
    uint32_t              elapsed_ms;
    int                   initialized;
} elib_wdt_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_WDT_TYPES_H */
