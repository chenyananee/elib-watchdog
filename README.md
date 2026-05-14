# elib-watchdog

嵌入式软件看门狗管理库 — 多任务监控，零动态分配，纯 C99。

## 特性

- 多任务独立监控：每个任务通过 ID 注册和喂狗
- 零动态分配：所有内存由用户预分配（栈/静态）
- 定时中断驱动：在定时器 ISR 中调用 `elib_wdt_manage`，确保主循环卡死时仍能检测超时
- 可配置超时：超时后停止喂硬件狗，调用复位回调，进入 `while(1)`
- 任务资源由用户提供：task 数组大小由用户决定

## 状态机

```
IDLE --start()--> RUNNING --timeout--> TIMEOUT (while(1), 不返回)
RUNNING --stop()--> IDLE
```

## 快速上手

```c
#include "elib_wdt.h"

/* 1. 实现底层回调 */
void hw_feed_dog(void) { /* 喂硬件看门狗 */ }
void hw_on_reset(void)  { /* 复位前处理 */ }

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

/* 4. 各任务在自己的循环中喂狗 */
elib_wdt_feed(&wdt_ctx, 0);  /* sensor 任务 */
elib_wdt_feed(&wdt_ctx, 1);  /* comm 任务 */

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
| `elib_wdt_feed(ctx, task_id)` | 喂狗 |
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
