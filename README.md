# elib-watchdog

嵌入式软件看门狗管理库 — 多任务监控，零动态分配，纯 C99。

## 特性

- 多任务独立监控：每个任务通过 ID 注册和喂狗
- 任务签到机制：区分超时根因（卡住的任务）与被连累的任务（未启动的任务）
- 零动态分配：所有内存由用户预分配（栈/静态）
- 定时中断驱动：在定时器 ISR 中调用 `elib_wdt_manage`，确保主循环卡死时仍能检测超时
- 可配置超时：超时后停止喂硬件狗，调用复位回调，进入 `while(1)`
- 任务资源由用户提供：task 数组大小由用户决定

## 状态机

```
IDLE --start()--> RUNNING --timeout--> TIMEOUT (while(1), 不返回)
RUNNING --stop()--> IDLE
```

## 任务状态

每个任务在监控窗口内有三种状态：

| 状态 | 值 | 含义 |
|------|---|------|
| `ELIB_WDT_TASK_IDLE` | 0 | 未启动 — 任务未签到也未喂狗 |
| `ELIB_WDT_TASK_CHECKIN` | 1 | 已签到 — 任务已启动但未完成 |
| `ELIB_WDT_TASK_FED` | 2 | 已喂狗 — 任务正常完成 |

超时时的诊断逻辑：

- `status == ELIB_WDT_TASK_CHECKIN` → 任务已启动但卡住 → **根因任务**
- `status == ELIB_WDT_TASK_IDLE` → 任务根本没启动 → **被连累的任务**
- `status == ELIB_WDT_TASK_FED` → 任务正常完成

## 快速上手

```c
#include "elib_wdt.h"

/* 1. 实现底层回调 */
void hw_feed_dog(void) { /* 喂硬件看门狗 */ }
void hw_on_reset(const elib_wdt_ctx_t *ctx) {
    /* 超时诊断：遍历任务状态 */
    for (uint8_t i = 0; i < ctx->cfg->max_tasks; i++) {
        if (!ctx->cfg->tasks[i].bit_flags.registered) continue;
        if (ctx->cfg->tasks[i].bit_flags.status == ELIB_WDT_TASK_CHECKIN)
            log("STUCK: %s", ctx->cfg->tasks[i].name);   /* 根因 */
        else if (ctx->cfg->tasks[i].bit_flags.status == ELIB_WDT_TASK_IDLE)
            log("BLOCKED: %s", ctx->cfg->tasks[i].name);  /* 被连累 */
    }
}

/* 2. 定义任务数组、配置和上下文 */
static elib_wdt_task_t wdt_tasks[4];
static elib_wdt_cfg_t wdt_cfg = {
    .feed_dog  = hw_feed_dog,
    .on_reset  = hw_on_reset,
    .timeout_ms = 1000,
    .tasks     = wdt_tasks,
    .max_tasks = 4,
};
static elib_wdt_ctx_t wdt_ctx;

/* 3. 初始化、注册任务、启动 */
elib_wdt_init(&wdt_ctx, &wdt_cfg);
elib_wdt_register(&wdt_ctx, 0, "sensor");
elib_wdt_register(&wdt_ctx, 1, "comm");
elib_wdt_start(&wdt_ctx);

/* 4. 各任务在自己的循环中签到和喂狗 */
void sensor_loop(void) {
    while (1) {
        elib_wdt_checkin(&wdt_ctx, 0);  /* 签到：任务已启动 */
        read_sensor();
        elib_wdt_feed(&wdt_ctx, 0);     /* 喂狗：任务已完成 */
    }
}

/* 不使用 checkin 时行为与之前一致 */
void comm_loop(void) {
    while (1) {
        communicate();
        elib_wdt_feed(&wdt_ctx, 1);     /* 直接喂狗 */
    }
}

/* 5. 在定时器中断中调用管理函数（如 10ms 定时器） */
void timer_isr(void) {
    elib_wdt_manage(&wdt_ctx, 10);
}
```

## API

| 函数 | 说明 |
|------|------|
| `elib_wdt_init(ctx, cfg)` | 初始化看门狗管理器 |
| `elib_wdt_deinit(ctx)` | 反初始化 |
| `elib_wdt_reset(ctx)` | 重置运行状态，保留配置和注册 |
| `elib_wdt_register(ctx, task_id, name)` | 注册监控任务 |
| `elib_wdt_unregister(ctx, task_id)` | 注销监控任务 |
| `elib_wdt_checkin(ctx, task_id)` | 签到：标记任务已启动 |
| `elib_wdt_feed(ctx, task_id)` | 喂狗：标记任务已完成 |
| `elib_wdt_start(ctx)` | 启动监控 (IDLE→RUNNING) |
| `elib_wdt_stop(ctx)` | 停止监控 (RUNNING→IDLE) |
| `elib_wdt_manage(ctx, elapsed_ms)` | 周期性管理调用（建议在定时中断中调用） |

## 编译

```bash
gcc -o app app.c src/elib_wdt_core.c -Iinclude
```

## 测试

```bash
gcc -o test_elib_wdt test/test_elib_wdt.c src/elib_wdt_core.c -Iinclude && ./test_elib_wdt
```

## 许可

MIT License, Copyright (c) 2026 ChenYanan
