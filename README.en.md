[简体中文](README.md) | **English**

# elib-watchdog

A multi-task software watchdog for resource-constrained embedded systems: zero dynamic memory, no global variables, pure C99, single umbrella-header API.

Current version: **2.0.0** (`ELIB_WDT_VERSION`), single header `include/elib_wdt.h`.

## Features

- **Zero dynamic memory / no globals**: all state lives in the `ctx + task array` that the user supplies statically
- **Single umbrella-header API**: all public types and functions live in `elib_wdt.h`; no sub-headers
- **Independent window per task**: every monitored task has its own countdown timer and timeout window
- **The library never blocks**: a timeout only sets the state to `TIMEOUT` and returns an error code, **no more spinning inside the ISR**
- **Stop feeding on timeout**: after a timeout the library stops petting the hardware watchdog, so the hardware watchdog resets the whole system by itself
- **Root-cause diagnosis**: `checked_in` separates "the root-cause task stuck in an iteration" from "tasks that were blocked as a result"
- **Observable**: `get_info` / `get_state` report the remaining time and the state, with no access to internal fields
- **Optional critical-section hooks**: `init` rejects a one-sided hook (hooks must be paired)
- **Pure C99 implementation**, C++ compatible (`extern "C"`)

## Quick Start

### 1. Statically Define the Task Array, Configuration, and Context

```c
#include "elib_wdt.h"   /* add -I<repo>/include at build time */

#define MAX_TASKS 4

static elib_wdt_task_t g_wdt_tasks[MAX_TASKS];
static elib_wdt_ctx_t  g_wdt_ctx;

static void hw_feed_dog(void)
{
    /* pet the hardware watchdog, e.g. HAL_IWDG_Refresh(&hiwdg) */
}

static volatile uint8_t g_timeout_flag;

static void on_timeout(void *user_data)
{
    (void)user_data;
    /* runs in the context of the manage() caller (usually the timer ISR):
     * only set the flag, never block */
    g_timeout_flag = 1u;
}

static void app_wdt_init(void)
{
    elib_wdt_cfg_t cfg = {
        .feed_dog   = hw_feed_dog,   /* may be NULL: pure software watchdog */
        .on_timeout = on_timeout,    /* may be NULL: return codes only */
        .timeout_ms = 1000u,         /* global default window */
        .tasks      = g_wdt_tasks,
        .max_tasks  = MAX_TASKS,
        .user_data  = NULL,
    };

    /* cfg is copied by value and may live on the stack; the task array must
     * outlive it. For hooks see "Concurrency Model"; with a single context
     * pass NULL, NULL */
    elib_wdt_init(&g_wdt_ctx, &cfg, crit_enter, crit_exit);
}
```

### 2. Register Tasks and Start Monitoring

```c
static void app_wdt_start(void)
{
    elib_wdt_register(&g_wdt_ctx, 1u, "sensor", 500u);   /* 500 ms window */
    elib_wdt_register(&g_wdt_ctx, 2u, "comm",   0u);     /* 0 = inherit the global 1000 ms */
    elib_wdt_start(&g_wdt_ctx);                          /* reload the windows and enter RUNNING */
}
```

### 3. Each Task Checks In and Feeds in Its Own Loop

```c
static void sensor_task(void)
{
    for (;;) {
        elib_wdt_checkin(&g_wdt_ctx, 1u);   /* mark "this iteration started" (diagnostics only) */
        read_sensor();                      /* stuck here -> this task is the root cause */
        process_sensor();
        elib_wdt_feed(&g_wdt_ctx, 1u);      /* mark "this iteration finished" */
    }
}

static void comm_task(void)
{
    for (;;) {
        communicate();
        elib_wdt_feed(&g_wdt_ctx, 2u);      /* timeouts are detected fine without checkin too */
    }
}
```

### 4. Drive from the Timer ISR, Clean Up in the Main Loop

```c
/* 10 ms timer interrupt */
void timer_isr(void)
{
    if (elib_wdt_manage(&g_wdt_ctx, 10u) == ELIB_WDT_ERR_TIMEOUT) {
        /* the hardware watchdog is no longer petted: it will reset the system
         * after its own timeout. Only do the lightest recording here; leave the
         * heavy work to the main loop (if it is still alive). */
        g_timeout_flag = 1u;
    }
}

/* main loop */
for (;;) {
    if (g_timeout_flag) {
        wdt_dump_diagnosis();     /* see "Root-Cause Diagnosis" below */
    }
    /* ... other background logic ... */
}
```

> `manage()` **performs no blocking action**. If your policy is "reset immediately", call `NVIC_SystemReset()` explicitly here or inside `on_timeout` — the policy belongs to the application, the library only reports.

## API Overview

| Group | Functions | Description |
|------|------|------|
| Lifetime | `elib_wdt_init(ctx, cfg, enter_critical, exit_critical)` | Initialize (memsets ctx and the task array; copies cfg by value; rejects illegal configurations / one-sided hooks) |
| | `elib_wdt_deinit(ctx)` | Deinitialize (NULL-safe; afterwards everything except init is rejected) |
| | `elib_wdt_reset(ctx)` | Reload all windows + clear the diagnostic flags + go back to IDLE, **keeping registrations** |
| Tasks | `elib_wdt_register(ctx, task_id, name, timeout_ms)` | Register; `timeout_ms == 0` inherits the global window; the ID must be unique |
| | `elib_wdt_unregister(ctx, task_id)` | Unregister; the slot becomes free and reusable |
| | `elib_wdt_checkin(ctx, task_id)` | Check in: sets `checked_in`, **does not touch the timer** |
| | `elib_wdt_feed(ctx, task_id)` | Feed: reload this task's window + clear `checked_in` |
| Scheduler | `elib_wdt_start(ctx)` | Reload all windows and enter RUNNING (can recover from IDLE / TIMEOUT) |
| | `elib_wdt_stop(ctx)` | Enter IDLE (windows are not reloaded) |
| Driving | `elib_wdt_manage(ctx, elapsed_ms)` | Called from the ISR: decrements all windows + pets the hardware watchdog while healthy; returns an error code on timeout |
| Query | `elib_wdt_get_info(ctx, task_id, &info)` | Task snapshot (window, remaining time, flags) |
| | `elib_wdt_get_state(ctx, &state)` | Scheduler state |

Error codes (9, `elib_wdt_err_t`):

| Error code | Level | Meaning |
|--------|------|------|
| `ELIB_WDT_OK` | — | Success |
| `ELIB_WDT_ERR_INVALID_PARAM` | General | NULL pointer / zero capacity / zero timeout / one-sided hook |
| `ELIB_WDT_ERR_NOT_INITIALIZED` | Scheduler | Not init'ed, or already deinit'ed |
| `ELIB_WDT_ERR_NOT_FOUND` | Task | ID is not registered |
| `ELIB_WDT_ERR_FULL` | Task | The task pool is full |
| `ELIB_WDT_ERR_ALREADY_REGISTERED` | Task | Duplicate registration of the ID |
| `ELIB_WDT_ERR_NOT_RUNNING` | Scheduler | `stop` called while IDLE |
| `ELIB_WDT_ERR_ALREADY_RUNNING` | Scheduler | `start` called while RUNNING |
| `ELIB_WDT_ERR_TIMEOUT` | Scheduler | A task window reached zero (`manage` returns it) |

## State Machine

```
  IDLE ──start()──► RUNNING ──any window hits 0──► TIMEOUT
    ▲                  │                             │
    │                  │stop()                       │
    │                  ▼                             │
    └───────────────  IDLE  ◄───stop()/reset()───────┘
                       ▲
                       │reset()
        (IDLE / RUNNING / TIMEOUT can all reset back to IDLE)
```

| Transition | Trigger | Side effect |
|------|------|--------|
| `IDLE → RUNNING` | `start()` | Reload all registered task windows, clear `checked_in` |
| `RUNNING → IDLE` | `stop()` | Windows are not reloaded; remaining values are kept |
| `RUNNING → TIMEOUT` | `manage()` finds a window at zero | Stop petting the hardware watchdog, fire `on_timeout` once |
| `TIMEOUT → RUNNING` | `start()` | Reload the windows, resume petting |
| `TIMEOUT → IDLE` | `stop()` / `reset()` | `reset` additionally reloads the windows and clears the flags |
| `IDLE → IDLE` | `reset()` | Reload the windows, clear the flags |

> `TIMEOUT` is a **sticky** state: until `start()`/`reset()` is called, every `manage()` returns `ELIB_WDT_ERR_TIMEOUT`; the callback does not fire again and petting does **not** resume.

## Semantic Contract

### Time-Driven Semantics, Boundaries, and Granularity

`elapsed_ms` in `elib_wdt_manage(ctx, elapsed_ms)` is the **number of milliseconds elapsed since the previous call**, supplied by the timer ISR.

- Rule: `remaining_ms <= elapsed_ms` → that task's window reaches zero and times out immediately (the **exactly equal** boundary included)
- Time beyond a window is discarded and never "made up" later, so the timer period should be far smaller than the smallest window
- `elapsed_ms == 0` is a **pure no-op**: no time is deducted and the watchdog is **not** petted either (so a busy loop calling `manage(0)` cannot mask a stuck main loop)
- When not `start()`ed, `manage()` returns `OK`, deducts nothing and pets nothing — the ISR can call it unconditionally

### Feeding Model and Hardware Watchdog

```
Healthy: every manage() → decrement the windows → all > 0 → feed_dog() is called
Timeout: some window == 0 → state TIMEOUT → feed_dog() is no longer called
```

The library itself **never resets the system**. Its job is "pet the hardware watchdog while the system is healthy, and stop petting as soon as any task times out". The hardware watchdog then resets the system on its own — so there is no spinning inside the ISR, and the `on_timeout` callback context is not the only way out.

`feed_dog` may be `NULL`: the library then degrades to a pure software watchdog that only reports timeouts through the return value of `manage()`.

### Timeout Callback

`on_timeout(user_data)` runs in the context of the `manage()` caller (usually the timer ISR):

- **Fires once per timeout**; it does not fire again while the state is sticky
- Holds no critical section, so query APIs can safely be called inside it
- Must be short and non-blocking: only set flags / record the root cause, and hand the heavy work back to the main loop
- May be `NULL`

### Root-Cause Diagnosis (`checked_in`)

The lifecycle convention for a single task:

```
checkin() → iteration starts → business logic → feed() → iteration ends
   │                                                       │
   └── checked_in = 1                                      └── checked_in = 0
       the window is reloaded to timeout_ms
```

At the moment of a timeout, read `checked_in`:

| `remaining_ms` | `checked_in` | Reading |
|----------------|--------------|------|
| `0` | `1` | **Root cause**: the task started an iteration but did not finish it (stuck in the middle) |
| `0` | `0` | **Blocked as a result**: the task never even got to start an iteration (resources were held by someone else) |

```c
static void wdt_dump_diagnosis(void)
{
    for (uint32_t i = 0u; i < g_wdt_ctx.max_tasks; i++) {
        if (!g_wdt_ctx.tasks[i].registered) {
            continue;
        }
        elib_wdt_task_info_t info;
        if (elib_wdt_get_info(&g_wdt_ctx, g_wdt_ctx.tasks[i].id, &info) != ELIB_WDT_OK) {
            continue;
        }
        if (info.remaining_ms != 0u) {
            continue;
        }
        log(info.checked_in ? "STUCK: %s" : "BLOCKED: %s",
            g_wdt_ctx.tasks[i].name);
    }
}
```

`checkin()` **only sets the flag and never reloads the window**, so "checked in but not fed" is an observable failure state. Without `checkin()` the diagnostic precision drops to "BLOCKED only", but timeout detection is not affected at all.

### Register / Unregister / Reload Rules

| Operation | Window | `checked_in` | Description |
|------|------|--------------|------|
| `register` | loaded as this task's window | cleared to 0 | Registering while RUNNING also loads a full window |
| `feed` | reloaded to this task's window | cleared to 0 | Allowed during TIMEOUT too, but does not clear the timeout state |
| `start` | reloaded for all registered tasks | all cleared to 0 | Can be called from IDLE / TIMEOUT |
| `reset` | reloaded for all registered tasks | all cleared to 0 | Returns to IDLE, keeps registrations |
| `stop` | **not reloaded**, remaining values kept | unchanged | Only stops monitoring |
| `unregister` | slot zeroed | cleared to 0 | The slot can be reused by the next `register` at once |

A window argument of `0` to `register` inherits the global default from `init`; the global default itself must be `>= 1`.

### Lifetime and Memory

- `init` **unconditionally** memsets `ctx` and the whole task array: any previous state can be reinitialized, and a stale `registered` in dirty RAM cannot make a slot "look occupied"
- `cfg` is a **value struct**: `init` copies it field by field and keeps no pointers, so `cfg` can live on the stack
- The array pointed to by `cfg->tasks` must **outlive** the context (lifetime ≥ the context's), and the strings pointed to by `name` must stay valid just as long
- `deinit` only stops monitoring and clears the flags; afterwards every API except `init` returns `ELIB_WDT_ERR_NOT_INITIALIZED`
- Re-running `init` clears the task array, so all previous registrations are gone

## Concurrency Model (ISR + Main Loop)

`elib_wdt_manage()` is called from the timer ISR, while `feed` / `checkin` / `register` / `unregister` are called from the main loop. `remaining_ms` is declared `volatile`, and window decrements and reloads are wrapped by the **user-supplied critical-section hooks** — the hooks are passed as a pair to `elib_wdt_init()`, and `init` rejects a one-sided hook:

```c
/* Example: save/restore PRIMASK on Cortex-M */
static uint32_t g_crit_save;

static void crit_enter(void)
{
    g_crit_save = __get_PRIMASK();
    __disable_irq();
}

static void crit_exit(void)
{
    __set_PRIMASK(g_crit_save);
}

elib_wdt_init(&g_wdt_ctx, &cfg, crit_enter, crit_exit);
```

- Hooks must use **save/restore** interrupt masking and must support nesting
- Passing `NULL, NULL` declares single-context use (for example `manage` is also driven by the main loop in software)
- The callbacks (`feed_dog`, `on_timeout`) hold **no** critical section, so query APIs can safely be called inside them

## Building

Add `include/` to the header search path and compile the source file as an ordinary C99 object file:

```bash
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude \
    -c src/elib_wdt_core.c -o elib_wdt_core.o
```

The source file includes the umbrella header directly (`#include "elib_wdt.h"`), depends on no relative paths, and ships no build system of its own.

## Testing

`test/` holds 11 independent test files (84 cases in total); each has its own `main()` and can be compiled and run separately:

```bash
for t in test/test_*.c; do
    gcc -std=c99 -Wall -Wextra -Werror -Iinclude "$t" src/elib_wdt_core.c -o "${t%.c}.exe"
    "./${t%.c}.exe"
done
```

| File | Coverage |
|------|------|
| `test_init.c` | Version macros, cfg copied by value, dirty task array cleared, illegal configurations / one-sided hooks rejected, optional callbacks as NULL, reinitialization while running, all APIs rejected after deinit |
| `test_register.c` | Slot filling, window inheritance and override, NULL name, duplicate ID, pool full, slot reuse after unregister, registering while running, parameter validation |
| `test_unregister.c` | Every field of the slot zeroed, double unregister, unknown ID, no longer participating in timeouts after unregister, the system stays healthy after removing a slow task |
| `test_checkin.c` | Setting the flag, not touching the timer, idempotence, feed clears the flag, unknown ID, usable in IDLE and after a timeout, parameter validation |
| `test_feed.c` | Reloading its own window (not the global one), clearing the flag, usable in IDLE, staying healthy with periodic feeding, feeding after a timeout does not clear TIMEOUT |
| `test_start_stop.c` | State gating, start reloads the windows, stop keeps the remaining values, reset keeps registrations, manage is a no-op in IDLE, recovery from TIMEOUT via start/stop/reset |
| `test_manage_window.c` | Decrement accumulation, the `remaining == elapsed` boundary, 1 ms short does not time out, large steps saturate, `manage(0)` is a no-op and does not pet, petting while healthy, petting with no tasks, independent timing per task, empty slots ignored |
| `test_manage_timeout.c` | Return codes instead of spinning, no petting on the timeout tick, sticky state does not refire the callback, a short window reaching zero does not affect a long window's remaining time, user_data pass-through, NULL callback safety, petting resumes after start |
| `test_diagnosis.c` | Root cause vs. blocked as a result, no leftover flags after a complete iteration, the flag survives decrements, start/reset clear the flag |
| `test_get_info.c` | Snapshots initially and after advancing, three-state queries, unknown/unregistered IDs, NULL pointers and stale contexts |
| `test_critical.c` | Hooks stored as a pair, enter/exit balanced for every operation, `feed_dog` and `on_timeout` run outside the critical section, APIs reentrant inside the callbacks, NULL hooks work |

Behavior consistency is additionally cross-checked against an independent Python reference model (25 scenarios / 223 assertions, 0 failures); the model was rewritten independently from the umbrella-header contract and reuses no C code structure.

## License

MIT License (see `LICENSE`).
