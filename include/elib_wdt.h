/* ============================================================================
 * elib_wdt.h --- elib watchdog (elib-watchdog) unified umbrella header
 * ============================================================================
 * The complete public API and public types of this library live in this single
 * header. All sub-headers were removed as of v2.
 * Version: ELIB_WDT_VERSION (currently 2.0.0)
 *
 * Game rules (elib family v2 common principles):
 *   1. C99; must compile warning-free with
 *      arm-gcc -std=c99 -Wall -Wextra -Werror -Iinclude.
 *   2. Zero dynamic memory, zero globals: the ctx and the task array are
 *      provided statically by the user.
 *   3. init memset-overwrites and re-initializes unconditionally; a ctx in any
 *      historical state is accepted; illegal configurations (NULL / zero
 *      capacity / zero timeout / a one-sided critical-hook pair) are rejected.
 *   4. Sources include this umbrella header directly: #include "elib_wdt.h",
 *      built with -Iinclude; relative-path cross-layer includes are forbidden.
 *   5. No build system ships with the library.
 *
 * Watchdog model:
 *   Every registered task owns an independent down-counter (remaining_ms).
 *   The task calls elib_wdt_feed() when it completes one iteration, which
 *   reloads the counter to its window. The ISR side, elib_wdt_manage(),
 *   subtracts the elapsed milliseconds from every registered counter and, while
 *   the system is still healthy, feeds the hardware watchdog.
 *
 *   When any counter reaches zero the scheduler-side state becomes TIMEOUT and
 *   manage() STOPS feeding the hardware watchdog, so the hardware eventually
 *   resets the board. manage() itself never blocks and never executes an
 *   infinite loop: it returns ELIB_WDT_ERR_TIMEOUT so the caller decides what
 *   to do (log, save state, force a reset, keep running degraded).
 *
 *   A timeout is sticky: while the state is TIMEOUT every manage() call returns
 *   ELIB_WDT_ERR_TIMEOUT without re-firing the callback, and the hardware
 *   watchdog stays unfed. elib_wdt_start() or elib_wdt_reset() re-arms the
 *   windows and leaves the TIMEOUT state.
 *
 * Root-cause diagnosis:
 *   checked_in is set by elib_wdt_checkin() and cleared by elib_wdt_feed().
 *   A task that expires with checked_in == 1 was executing its iteration but
 *   never completed it: that task is the ROOT CAUSE. A task that expires with
 *   checked_in == 0 never got to start its iteration at all: it was merely
 *   BLOCKED behind the root cause. checkin() never touches the timer, so a task
 *   can use it purely for diagnostics.
 *
 * Windowing:
 *   The window is per task: elib_wdt_register(..., timeout_ms) with 0 meaning
 *   "inherit the global default" given to init. A larger elapsed_ms than the
 *   remaining window saturates at zero and expires immediately; there is no
 *   catch-up bookkeeping. Keep the manager period far below the smallest
 *   window.
 *
 * Concurrency model:
 *   elib_wdt_manage() runs in the timer ISR; feed/checkin/register/unregister
 *   run in the main loop. remaining_ms is volatile and the counter operations
 *   are fenced by the optional critical-hook pair passed to init (both hooks
 *   NULL or both non-NULL; init rejects a one-sided pair). The timeout callback
 *   and the feed callback never run with the critical section held.
 * ==========================================================================*/
#ifndef ELIB_WDT_H
#define ELIB_WDT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- version */
#define ELIB_WDT_VERSION        "2.0.0"
#define ELIB_WDT_VERSION_MAJOR  2
#define ELIB_WDT_VERSION_MINOR  0
#define ELIB_WDT_VERSION_PATCH  0

/* --------------------------------------------------------------- error codes */
typedef enum {
    ELIB_WDT_OK = 0,                   /* Success */
    ELIB_WDT_ERR_INVALID_PARAM,        /* Illegal parameter */
    ELIB_WDT_ERR_NOT_INITIALIZED,      /* Scheduler not initialized (or deinitialized) */
    ELIB_WDT_ERR_NOT_FOUND,            /* Task ID is not registered */
    ELIB_WDT_ERR_FULL,                 /* Task pool is full */
    ELIB_WDT_ERR_ALREADY_REGISTERED,   /* Task ID is already registered */
    ELIB_WDT_ERR_NOT_RUNNING,          /* Scheduler is not running */
    ELIB_WDT_ERR_ALREADY_RUNNING,      /* Scheduler is already running */
    ELIB_WDT_ERR_TIMEOUT,              /* A task window expired (returned by manage) */
} elib_wdt_err_t;

/* -------------------------------------------------------------- state machine */
/* IDLE -(start)-> RUNNING -(window expires)-> TIMEOUT
 * STOPPED/RUNNING/TIMEOUT can all return to IDLE through elib_wdt_stop or
 * elib_wdt_reset; elib_wdt_start re-arms and enters RUNNING from any state
 * except RUNNING itself. */
typedef enum {
    ELIB_WDT_STATE_IDLE = 0,           /* Not monitoring */
    ELIB_WDT_STATE_RUNNING,            /* Monitoring, hardware watchdog fed */
    ELIB_WDT_STATE_TIMEOUT,            /* A window expired, hardware watchdog unfed */
} elib_wdt_state_t;

/* ------------------------------------------------------------------- types */
/* Optional hardware watchdog feeder (NULL = software-only watchdog) */
typedef void (*elib_wdt_feed_dog_fn)(void);

/* Timeout notification, invoked from the manage() caller context (usually the
 * timer ISR) with the user_data given to init. Keep it short and non-blocking;
 * NULL is allowed. */
typedef void (*elib_wdt_timeout_fn)(void *user_data);

/* Per-task entry. Readable for diagnosis; never write these fields from
 * application code (that is undefined behaviour). */
typedef struct {
    uint32_t id;                       /* Application task ID */
    const char *name;                  /* Optional label, may be NULL */
    uint32_t timeout_ms;               /* Effective window (never 0) */
    volatile uint32_t remaining_ms;    /* Down-counter, ISR-written */
    uint8_t registered;                /* Slot in use */
    uint8_t checked_in;                /* checkin sets, feed clears */
} elib_wdt_task_t;

/* Configuration (value struct; init copies every field, no pointer is kept) */
typedef struct {
    elib_wdt_feed_dog_fn feed_dog;     /* Optional, may be NULL */
    elib_wdt_timeout_fn on_timeout;    /* Optional, may be NULL */
    uint32_t timeout_ms;               /* Default window for tasks, must be >= 1 */
    elib_wdt_task_t *tasks;            /* User-allocated task array (outlives ctx) */
    uint32_t max_tasks;                /* Array capacity (must be >= 1) */
    void *user_data;                   /* Opaque pointer passed to on_timeout */
} elib_wdt_cfg_t;

/* Scheduler context (statically allocated by the user, initialized by init) */
typedef struct {
    /* Filled in by init (copied from cfg) */
    elib_wdt_feed_dog_fn feed_dog;
    elib_wdt_timeout_fn on_timeout;
    uint32_t default_timeout_ms;
    elib_wdt_task_t *tasks;
    uint32_t max_tasks;
    void *user_data;
    void (*enter_critical)(void);      /* Optional critical-section hooks; */
    void (*exit_critical)(void);       /* both NULL or both non-NULL */

    /* Runtime state */
    volatile elib_wdt_state_t state;   /* IDLE / RUNNING / TIMEOUT */
    volatile uint8_t initialized;      /* init/deinit flag */
} elib_wdt_ctx_t;

/* Task info (query output, snapshot) */
typedef struct {
    uint32_t timeout_ms;               /* Effective window */
    uint32_t remaining_ms;             /* Time left before expiry */
    uint8_t registered;                /* Always 1 for a successful query */
    uint8_t checked_in;                /* Root-cause marker */
} elib_wdt_task_info_t;

/* --------------------------------------------------------------- lifecycle */
/**
 * @brief Initialize the watchdog manager
 * @param ctx User-provided context pointer (any historical state; memset-overwritten)
 * @param cfg Watchdog configuration (copied, no pointer kept)
 * @param enter_critical Optional critical-section entry hook (pair with exit_critical)
 * @param exit_critical Optional critical-section exit hook (pair with enter_critical)
 * @return OK / INVALID_PARAM (NULL, zero capacity, zero timeout, one-sided hooks)
 */
elib_wdt_err_t elib_wdt_init(elib_wdt_ctx_t *ctx,
                             const elib_wdt_cfg_t *cfg,
                             void (*enter_critical)(void),
                             void (*exit_critical)(void));

/**
 * @brief Deinitialize the manager (NULL-safe; configuration and tasks are kept
 *        in place but every API except init rejects afterwards)
 * @return OK / INVALID_PARAM
 */
elib_wdt_err_t elib_wdt_deinit(elib_wdt_ctx_t *ctx);

/**
 * @brief Re-arm every registered window and return to IDLE, keeping registrations
 * @return OK / INVALID_PARAM / NOT_INITIALIZED
 */
elib_wdt_err_t elib_wdt_reset(elib_wdt_ctx_t *ctx);

/* ------------------------------------------------------------- task control */
/**
 * @brief Register a task for monitoring
 * @param ctx Context
 * @param task_id Application task ID (any value, must be unique)
 * @param name Optional label, may be NULL (string must outlive the registration)
 * @param timeout_ms Window in milliseconds (0 = inherit the global default)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / ALREADY_REGISTERED / FULL
 */
elib_wdt_err_t elib_wdt_register(elib_wdt_ctx_t *ctx,
                                 uint32_t task_id,
                                 const char *name,
                                 uint32_t timeout_ms);

/**
 * @brief Unregister a task (the slot becomes reusable)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_wdt_err_t elib_wdt_unregister(elib_wdt_ctx_t *ctx, uint32_t task_id);

/**
 * @brief Mark a task as having started its iteration (diagnostics only)
 *
 * Sets checked_in to 1 and never touches the timer, so a root-cause task that
 * hangs after checkin stays distinguishable from a task that never ran.
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_wdt_err_t elib_wdt_checkin(elib_wdt_ctx_t *ctx, uint32_t task_id);

/**
 * @brief Feed a task: reload its window and clear checked_in
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_wdt_err_t elib_wdt_feed(elib_wdt_ctx_t *ctx, uint32_t task_id);

/* ------------------------------------------------------- scheduler control */
/**
 * @brief Start (or re-arm) monitoring: reload every window, enter RUNNING
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / ALREADY_RUNNING
 */
elib_wdt_err_t elib_wdt_start(elib_wdt_ctx_t *ctx);

/**
 * @brief Stop monitoring: enter IDLE (windows are not reloaded)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_RUNNING
 */
elib_wdt_err_t elib_wdt_stop(elib_wdt_ctx_t *ctx);

/* ------------------------------------------------------------------ driver */
/**
 * @brief Manage the watchdog (call from the timer ISR)
 *
 * Subtracts elapsed_ms from every registered window, then:
 *   - any window reached zero: state becomes TIMEOUT, on_timeout runs, the
 *     hardware watchdog is NOT fed, and ELIB_WDT_ERR_TIMEOUT is returned;
 *   - otherwise: feed_dog runs and OK is returned.
 *
 * Never blocks. While TIMEOUT is active, every call returns
 * ELIB_WDT_ERR_TIMEOUT without re-firing the callback and without feeding.
 * @param elapsed_ms Milliseconds since the previous call (0 = no-op, not fed)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / TIMEOUT
 */
elib_wdt_err_t elib_wdt_manage(elib_wdt_ctx_t *ctx, uint32_t elapsed_ms);

/* ------------------------------------------------------------------ queries */
/**
 * @brief Query a task snapshot
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_wdt_err_t elib_wdt_get_info(elib_wdt_ctx_t *ctx,
                                 uint32_t task_id,
                                 elib_wdt_task_info_t *out_info);

/**
 * @brief Query the scheduler state
 * @return OK / INVALID_PARAM / NOT_INITIALIZED
 */
elib_wdt_err_t elib_wdt_get_state(elib_wdt_ctx_t *ctx,
                                  elib_wdt_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_WDT_H */
