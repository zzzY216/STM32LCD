/**
 * @file    tick.c
 * @brief   毫秒时基（TIM3 独立定时器，不占用 SysTick）。
 *
 * @details 【为什么必须换掉 SysTick —— 这是一个真实踩到的坑】
 *
 *   原工程的 System/Delay.c 独占使用 SysTick，且用法很特殊：
 *
 *   1. `delay_init()` 把 SysTick 时钟源设成 **HCLK/8**
 *   2. `delay_us()` / `delay_ms()` 每次结束时都执行
 *      `SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;`（关闭计数器）
 *   3. 下次调用时重新 LOAD 并 ENABLE
 *
 *   早期版本本文件也用 SysTick（HCLK），结果三处冲突：
 *
 *   - 时钟源冲突：Delay 要 HCLK/8，本文件要 HCLK，
 *     后配置的覆盖前面的，另一个模块的延时时长完全错误
 *   - 状态冲突：delay 结束关了 SysTick，本文件以为在跑，
 *     实际停止计数 → 时间不走 → GIF 卡死、串口超时判断失效
 *   - 自愈逻辑互相打架：本文件检测到被关就重开，
 *     delay 下次又关掉，来回争抢，行为不可预测
 *
 *   结论：**不要和 Delay.c 抢 SysTick**。改用 TIM3 做独立毫秒时基，
 *   两边互不干扰，问题彻底消失。
 *
 * @details 【为什么用寄存器而不是 StdPeriph 的 TIM_xxx() 函数】
 *
 *   本工程有 Keil 免费版 32KB 镜像限制，当前余量只有约 5.9KB。
 *   引入 `stm32f10x_tim.c`（源码 106KB）会把 TIM_Init/TIM_Cmd 等
 *   一串函数链接进来。虽然只用到几个，但为了节省宝贵的 Flash，
 *   这里直接操作寄存器。
 *
 *   TIM3 只用到 4 个寄存器，逻辑简单，直接写更可控：
 *       CR1   —— 计数方向（向上）
 *       DIER  —— 更新中断使能
 *       PSC   —— 预分频
 *       ARR   —— 自动重装值
 *       SR    —— 状态（清中断标志）
 *
 * @details 【TIM3 频率计算】
 *
 *   STM32F103 的 APB1 = 36MHz，但 APB1 预分频不为 1 时定时器时钟翻倍
 *   → TIM3 实际时钟 = **72MHz**
 *
 *       PSC = 72 - 1  = 71    → 计数频率 72MHz / 72 = 1MHz（1us 一次）
 *       ARR = 1000 - 1 = 999  → 1000 次计数 = 1ms
 */
#include "tick.h"
#include "stm32f10x.h"

/* ==================== 内部状态 ==================== */

/** 自初始化以来的毫秒数（在中断里累加） */
static volatile u32 s_ticks = 0u;

/** 是否已初始化 */
static u8 s_started = 0u;

/* ==================== 中断服务 ==================== */

/**
 * @brief TIM3 更新中断：每 1ms 累加一次毫秒计数。
 *
 * @note 启动文件 startup_stm32f10x_md.s 里 TIM3_IRQHandler 是 [WEAK]，
 *       这里的强定义会覆盖它。
 *
 * @note 中断优先级设为 3（低于 USART1 的 1），
 *       保证串口接收不被时基中断耽搁。
 */
void TIM3_IRQHandler(void)
{
    /* 检查更新中断标志（UIF = bit0） */
    if ((TIM3->SR & 0x0001u) != 0u) {
        s_ticks++;
        /* 写 0 清除 UIF（该位写 0 有效，写 1 无效） */
        TIM3->SR = (uint16_t)~0x0001u;
    }
}

/* ==================== 对外接口 ==================== */

void tick_init(void)
{
    s_ticks = 0u;

    /* 1. 开 TIM3 时钟
     *
     *    TIM3 挂在 APB1 上，需要 RCC_APB1ENR 的 TIM3EN（bit1）。
     *    这里直接写寄存器，不依赖 RCC_APB1PeriphClockCmd。
     */
    RCC->APB1ENR |= (1u << 1);      /* TIM3EN */

    /* 读一次确保时钟稳定（对 APB1 外设，写后需短暂等待） */
    (void)RCC->APB1ENR;

    /* 2. 停止计数，准备配置 */
    TIM3->CR1 = 0u;

    /* 3. 预分频与重装值
     *
     *    SystemCoreClock = 72,000,000
     *    PSC = 71  → 1MHz 计数
     *    ARR = 999 → 1ms 周期
     */
    TIM3->PSC = (uint16_t)((SystemCoreClock / 1000000u) - 1u);   /* 71 */
    TIM3->ARR = (uint16_t)(1000u - 1u);                          /* 999 */

    /* 4. 产生一次更新事件，把 PSC/ARR 立即装载进影子寄存器
     *    （否则要等到第一次溢出才生效，首次计时会偏长） */
    TIM3->EGR = 0x0001u;            /* UG = bit0 */

    /* 5. 清掉 UG 产生的更新标志，避免刚启动就进一次中断 */
    TIM3->SR = (uint16_t)~0x0001u;

    /* 6. 使能更新中断（DIER 的 UIE = bit0） */
    TIM3->DIER |= 0x0001u;

    /* 7. NVIC 配置：优先级 3，最低（让串口优先）
     *
     *    NVIC_ISER0 覆盖 IRQ 0~31，TIM3_IRQn = 29。
     *    优先级寄存器 IPR[29] 在 NVIC_IPR 数组里。
     */
    {
        /* 子优先级 0，抢占优先级 3 → 高 4 位 = 0x30 */
        NVIC->IP[29] = (uint8_t)(3u << 4);

        /* 使能 IRQ 29 */
        NVIC->ISER[0] = (1u << 29);
    }

    /* 8. 启动：使能计数器（CEN = bit0） */
    TIM3->CR1 |= 0x0001u;

    s_started = 1u;
}

void tick_poll(void)
{
    /* 【说明】时间在 TIM3 中断里累加，这里无需做任何事。
     *
     * 保留此函数是为了接口稳定（app_link_poll 会调用它），
     * 也方便以后换别的时基方案。
     *
     * 与早期 SysTick 方案的关键区别：
     * 不再需要"检测是否被关掉并自愈"，因为 TIM3 是独立外设，
     * Delay.c 完全碰不到它，不存在争抢。
     */
    if (!s_started) {
        tick_init();
    }
}

u32 tick_get_ms(void)
{
    return s_ticks;
}
