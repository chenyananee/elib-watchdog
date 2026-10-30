# elib-watchdog

资源受限嵌入式系统用的多任务软件看门狗：零动态内存、零全局变量、纯 C99，单头伞 API。

当前版本：**2.0.0**（`ELIB_WDT_VERSION`），单头 `include/elib_wdt.h`。

## 特性

- **零动态内存 / 零全局**：全部状态由用户静态提供的 `ctx + 任务数组` 承载
- **单头伞 API**：全部公共类型与函数集中在 `elib_wdt.h`，无子头
- **每任务独立窗口**：每个被监控任务有自己的倒计时器与超时窗口
- **库永不阻塞**：超时只把状态置 `TIMEOUT` 并返回错误码，**不再在 ISR 里死循环**
- **停喂即复位**：超时后库停止喂硬件狗，硬件看门狗自然复位整机
- **根因诊断**：`checked_in` 区分"卡在迭代中的根因任务"与"被连累的任务"
- **可观察**：`get_info` / `get_state` 查询剩余时间与状态，无需访问内部字段
- **可选临界区钩子**：`init` 拒收单边钩子（必须成对）
- **纯 C99 实现**，C++ 兼容（`extern "C"`）

## 快速开始

### 1. 静态定义任务数组、配置与上下文

```c
#include "elib_wdt.h"   /* 构建时加 -I<repo>/include */

#define MAX_TASKS 4

static elib_wdt_task_t g_wdt_tasks[MAX_TASKS];
static elib_wdt_ctx_t  g_wdt_ctx;

static void hw_feed_dog(void)
{
    /* 喂硬件看门狗，例如 HAL_IWDG_Refresh(&hiwdg) */
}

static volatile uint8_t g_timeout_flag;

static void on_timeout(void *user_data)
{
    (void)user_data;
    /* 运行在 manage() 调用方上下文（通常是定时器 ISR）：只置标志，别阻塞 */
    g_timeout_flag = 1u;
}

static void app_wdt_init(void)
{
    elib_wdt_cfg_t cfg = {
        .feed_dog   = hw_feed_dog,   /* 可 NULL：纯软件看门狗 */
        .on_timeout = on_timeout,    /* 可 NULL：只要返回码 */
        .timeout_ms = 1000u,         /* 全局默认窗口 */
        .tasks      = g_wdt_tasks,
        .max_tasks  = MAX_TASKS,
        .user_data  = NULL,
    };

    /* cfg 按值拷贝，可以放在栈上；任务数组必须长期存在
     * 钩子见"并发模型"一节，单上下文使用时传 NULL, NULL */
    elib_wdt_init(&g_wdt_ctx, &cfg, crit_enter, crit_exit);
}
```

### 2. 注册任务并启动监控

```c
static void app_wdt_start(void)
{
    elib_wdt_register(&g_wdt_ctx, 1u, "sensor", 500u);   /* 500ms 窗口 */
    elib_wdt_register(&g_wdt_ctx, 2u, "comm",   0u);     /* 0 = 继承全局 1000ms */
    elib_wdt_start(&g_wdt_ctx);                          /* 重装窗口并进入 RUNNING */
}
```

### 3. 各任务在自己的循环里签到 + 喂狗

```c
static void sensor_task(void)
{
    for (;;) {
        elib_wdt_checkin(&g_wdt_ctx, 1u);   /* 标记"本迭代开始"（仅诊断） */
        read_sensor();                      /* 卡在这里 → 该任务是根因 */
        process_sensor();
        elib_wdt_feed(&g_wdt_ctx, 1u);      /* 标记"本迭代完成" */
    }
}

static void comm_task(void)
{
    for (;;) {
        communicate();
        elib_wdt_feed(&g_wdt_ctx, 2u);      /* 不用 checkin 也能正常检测超时 */
    }
}
```

### 4. 定时器中断里驱动，主循环里善后

```c
/* 10ms 定时器中断 */
void timer_isr(void)
{
    if (elib_wdt_manage(&g_wdt_ctx, 10u) == ELIB_WDT_ERR_TIMEOUT) {
        /* 已停喂硬件狗：硬件看门狗将在其自身超时后复位整机。
         * 这里只做最轻量的记录，重活留给主循环（如果它还活着）。 */
        g_timeout_flag = 1u;
    }
}

/* 主循环 */
for (;;) {
    if (g_timeout_flag) {
        wdt_dump_diagnosis();     /* 见下文"根因诊断" */
    }
    /* ... 其它后台逻辑 ... */
}
```

> `manage()` **不做任何阻塞动作**。若你把"立刻复位"作为策略，就在这里或 `on_timeout` 里显式调用 `NVIC_SystemReset()`——策略由应用决定，库只负责报告。

## API 一览

| 分组 | 函数 | 说明 |
|------|------|------|
| 生命周期 | `elib_wdt_init(ctx, cfg, enter_critical, exit_critical)` | 初始化（memset 覆盖 ctx 与任务数组；cfg 按值拷贝；非法配置/单边钩子拒收） |
| | `elib_wdt_deinit(ctx)` | 反初始化（NULL 安全；之后除 init 外全部拒收） |
| | `elib_wdt_reset(ctx)` | 重装所有窗口 + 清诊断标记 + 回 IDLE，**保留注册** |
| 任务 | `elib_wdt_register(ctx, task_id, name, timeout_ms)` | 注册；`timeout_ms == 0` 继承全局窗口；ID 必须唯一 |
| | `elib_wdt_unregister(ctx, task_id)` | 注销；槽位回归空闲可复用 |
| | `elib_wdt_checkin(ctx, task_id)` | 签到：置 `checked_in`，**不动计时器** |
| | `elib_wdt_feed(ctx, task_id)` | 喂狗：重装该任务窗口 + 清 `checked_in` |
| 调度器 | `elib_wdt_start(ctx)` | 重装所有窗口并进入 RUNNING（可从 IDLE / TIMEOUT 恢复） |
| | `elib_wdt_stop(ctx)` | 进入 IDLE（不重装窗口） |
| 驱动 | `elib_wdt_manage(ctx, elapsed_ms)` | ISR 调用：扣减所有窗口 + 健康时喂硬件狗；超时返回错误码 |
| 查询 | `elib_wdt_get_info(ctx, task_id, &info)` | 任务快照（窗口、剩余、标记） |
| | `elib_wdt_get_state(ctx, &state)` | 调度器状态 |

错误码（9 个，`elib_wdt_err_t`）：

| 错误码 | 层级 | 含义 |
|--------|------|------|
| `ELIB_WDT_OK` | — | 成功 |
| `ELIB_WDT_ERR_INVALID_PARAM` | 通用 | 空指针 / 零容量 / 零超时 / 单边钩子 |
| `ELIB_WDT_ERR_NOT_INITIALIZED` | 调度器级 | 未 init，或已 deinit |
| `ELIB_WDT_ERR_NOT_FOUND` | 任务级 | ID 未注册 |
| `ELIB_WDT_ERR_FULL` | 任务级 | 任务池已满 |
| `ELIB_WDT_ERR_ALREADY_REGISTERED` | 任务级 | ID 重复注册 |
| `ELIB_WDT_ERR_NOT_RUNNING` | 调度器级 | IDLE 时调用 `stop` |
| `ELIB_WDT_ERR_ALREADY_RUNNING` | 调度器级 | RUNNING 时调用 `start` |
| `ELIB_WDT_ERR_TIMEOUT` | 调度器级 | 有任务窗口归零（`manage` 返回） |

## 状态机

```
  IDLE ──start()──► RUNNING ──任一窗口归零──► TIMEOUT
    ▲                  │                        │
    │                  │stop()                  │
    │                  ▼                        │
    └───────────────  IDLE  ◄───stop()/reset()──┘
                       ▲
                       │reset()
        （IDLE / RUNNING / TIMEOUT 均可 reset 回 IDLE）
```

| 迁移 | 触发 | 副作用 |
|------|------|--------|
| `IDLE → RUNNING` | `start()` | 重装所有注册任务窗口、清 `checked_in` |
| `RUNNING → IDLE` | `stop()` | 不重装窗口，剩余值保留 |
| `RUNNING → TIMEOUT` | `manage()` 发现窗口归零 | 停喂硬件狗，触发一次 `on_timeout` |
| `TIMEOUT → RUNNING` | `start()` | 重装窗口，恢复喂狗 |
| `TIMEOUT → IDLE` | `stop()` / `reset()` | `reset` 额外重装窗口并清标记 |
| `IDLE → IDLE` | `reset()` | 重装窗口、清标记 |

> `TIMEOUT` 是**粘滞**状态：未经 `start()`/`reset()` 之前，每次 `manage()` 都返回 `ELIB_WDT_ERR_TIMEOUT`，不会重复触发回调，也**不会**恢复喂狗。

## 语义契约

### 时间驱动、边界与粒度

`elib_wdt_manage(ctx, elapsed_ms)` 中的 `elapsed_ms` 是**距上次调用经过的毫秒数**，由定时器 ISR 提供。

- 判定规则：`remaining_ms <= elapsed_ms` → 该任务窗口归零并立即超时（含**恰好相等**的边界）
- 超出窗口的部分直接丢弃，不会"补账"，因此定时器周期应远小于最小窗口
- `elapsed_ms == 0` 是**纯空操作**：不扣时间、**也不喂狗**（防止忙循环里反复 `manage(0)` 掩盖主循环卡死）
- 未 `start()` 时 `manage()` 返回 `OK` 且不扣时间、不喂狗——ISR 里可以无条件调用

### 喂狗模型与硬件看门狗

```
健康：每个 manage() → 扣减窗口 → 全部 > 0 → feed_dog() 被调用
超时：某个窗口 == 0 → 状态 TIMEOUT → feed_dog() 不再被调用
```

库本身**不主动复位**。它的职责是"在系统健康时喂硬件狗，一旦某任务超时就停喂"。硬件看门狗随后自行复位——这样既不会在 ISR 里死循环，也不会让 `on_timeout` 的回调上下文成为唯一出路。

`feed_dog` 可以为 `NULL`：此时库退化为纯软件看门狗，仅通过 `manage()` 的返回值报告超时。

### 超时回调

`on_timeout(user_data)` 在 `manage()` 调用方上下文执行（通常是定时器 ISR）：

- **每次超时只触发一次**；粘滞期间不重复触发
- 不持有临界区，可在回调内安全调用查询类 API
- 必须短小、非阻塞：只做置标志 / 记根因，重活交回主循环
- 可为 `NULL`

### 根因诊断（`checked_in`）

单个任务的生命周期约定：

```
checkin() → 迭代开始 → 业务处理 → feed() → 迭代完成
   │                                        │
   └── checked_in = 1                       └── checked_in = 0
       窗口重装为 timeout_ms
```

超时瞬间用 `checked_in` 判读：

| `remaining_ms` | `checked_in` | 判读 |
|----------------|--------------|------|
| `0` | `1` | **根因**：该任务开始了一次迭代却没完成（卡在中间） |
| `0` | `0` | **被连累**：该任务根本没轮到开始迭代（资源被别人占住） |

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

`checkin()` **只置标记、绝不重装窗口**，所以"签到了但没喂狗"是个可观测的失败态。不用 `checkin()` 时诊断精度下降为"只有 BLOCKED"，但超时检测完全不受影响。

### 注册 / 注销 / 重装规则

| 操作 | 窗口 | `checked_in` | 说明 |
|------|------|--------------|------|
| `register` | 装载为该任务窗口 | 清 0 | RUNNING 期间注册同样装载满窗口 |
| `feed` | 重装为该任务窗口 | 清 0 | TIMEOUT 期间也允许，但不解除超时状态 |
| `start` | 所有注册任务重装 | 全清 0 | 可从 IDLE / TIMEOUT 调用 |
| `reset` | 所有注册任务重装 | 全清 0 | 回 IDLE，保留注册 |
| `stop` | **不重装**，保留剩余值 | 不变 | 只是停止监控 |
| `unregister` | 槽位清零 | 清 0 | 槽位立即可被下一次 `register` 复用 |

`register` 的窗口参数为 `0` 时继承 `init` 的全局默认值；全局默认值本身必须 `>= 1`。

### 生命周期与内存

- `init` **无条件** memset `ctx` 与整个任务数组：任意历史状态都能重初始化，脏 RAM 里的旧 `registered` 不会让槽位"看起来被占用"
- `cfg` 是**值结构**：`init` 逐字段拷贝，不保存指针，因此 `cfg` 可以放栈上
- `cfg->tasks` 指向的数组必须**长期存在**（寿命 ≥ `ctx` 的使用期），`name` 指向的字符串同样必须长期有效
- `deinit` 只停止监控并清标志；之后除 `init` 外全部 API 返回 `ELIB_WDT_ERR_NOT_INITIALIZED`
- 重新 `init` 会清空任务数组，此前的注册全部失效

## 并发模型（ISR + 主循环）

`elib_wdt_manage()` 供定时器 ISR 调用，`feed` / `checkin` / `register` / `unregister` 等供主循环调用。`remaining_ms` 声明为 `volatile`，窗口的扣减与重装由**用户提供的临界区钩子**包裹——钩子通过 `elib_wdt_init()` 成对传入，`init` 会拒收单边钩子：

```c
/* 例：Cortex-M 下用 PRIMASK 保存/恢复 */
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

- 钩子必须**保存/恢复**式中断屏蔽，且支持嵌套
- 传 `NULL, NULL` 即声明单上下文使用（例如 `manage` 也由主循环软件驱动）
- 回调（`feed_dog`、`on_timeout`）**不持有**临界区，可以在其中安全调用查询类 API

## 编译

将 `include/` 加入头文件搜索路径，源文件编译为普通 C99 目标文件：

```bash
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude \
    -c src/elib_wdt_core.c -o elib_wdt_core.o
```

源文件直连伞头（`#include "elib_wdt.h"`），不依赖任何相对路径，无随库构建系统。

## 测试

`test/` 下 11 个独立测试文件（共 84 个用例），每件自带 `main()`，可分别编译运行：

```bash
for t in test/test_*.c; do
    gcc -std=c99 -Wall -Wextra -Werror -Iinclude "$t" src/elib_wdt_core.c -o "${t%.c}.exe"
    "./${t%.c}.exe"
done
```

| 文件 | 覆盖 |
|------|------|
| `test_init.c` | 版本宏、cfg 按值拷贝、脏任务数组被清空、非法配置/单边钩子拒收、可选回调为 NULL、运行中重初始化、deinit 后全 API 拒收 |
| `test_register.c` | 槽位填充、窗口继承与覆盖、NULL 名称、重复 ID、池满、注销后槽位复用、运行中注册、参数校验 |
| `test_unregister.c` | 槽位全字段清零、二次注销、未知 ID、注销后不再参与超时、慢任务移除后系统保持健康 |
| `test_checkin.c` | 置标记、不动计时器、幂等、feed 清标记、未知 ID、IDLE 与超时后均可用、参数校验 |
| `test_feed.c` | 重装自身窗口（非全局）、清标记、IDLE 可用、周期喂狗长期健康、超时后喂狗不解除 TIMEOUT |
| `test_start_stop.c` | 状态门禁、start 重装窗口、stop 保留剩余、reset 保留注册、manage 在 IDLE 为空操作、从 TIMEOUT 经 start/stop/reset 恢复 |
| `test_manage_window.c` | 扣减累加、`remaining == elapsed` 边界、差 1ms 不超时、大步长饱和、`manage(0)` 空操作且不喂狗、健康时喂狗、无任务只喂狗、多任务独立计时、空槽位忽略 |
| `test_manage_timeout.c` | 返回错误码而非死循环、超时当拍不喂狗、粘滞不重复回调、短窗口归零不影响长窗口剩余、user_data 透传、NULL 回调安全、start 后恢复喂狗 |
| `test_diagnosis.c` | 根因 vs 被连累、完整迭代无残留标记、标记跨扣减存活、start/reset 清标记 |
| `test_get_info.c` | 初始与推进后的快照、三态查询、未知/已注销 ID、空指针与失效上下文 |
| `test_critical.c` | 钩子成对存储、每个操作 enter/exit 配平、`feed_dog` 与 `on_timeout` 在临界区之外执行、回调内可重入 API、NULL 钩子可用 |

行为一致性另用独立 Python 参考模型交叉验证（25 个场景 / 223 项断言，0 失败），模型按伞头契约独立重写，不复用 C 代码结构。

## 许可证

MIT License（见 `LICENSE`）。
