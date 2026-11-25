/**
 * @file    test_tick.c
 * @brief   验证 TIM3 毫秒时基的寄存器配置算法。
 *
 * @details 本测试不依赖真实硬件，只验证：
 *
 *   1. PSC/ARR 计算结果是否为 1ms 周期
 *   2. 72MHz 时钟下 PSC=71、ARR=999 的组合确实得到 1000Hz
 *   3. 毫秒累加与回绕行为正确
 *   4. 中断标志清除位操作正确（SR 是写 0 有效，容易搞错）
 *   5. NVIC 位运算正确（ISER 与 IP 下标）
 *
 *   【为什么值得测】SR 寄存器的"写 0 清除"是一个非常容易写错的点：
 *   如果写成 SR |= 0x01（常见直觉），标志永远清不掉，
 *   中断会无休止地重复进入，主循环被饿死 —— 表现为"程序卡死"。
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int g_run = 0, g_fail = 0;

#define CHECK(cond, msg, ...)                                          \
    do {                                                               \
        g_run++;                                                       \
        if (!(cond)) { g_fail++; printf("  [FAIL] " msg "\n", ##__VA_ARGS__); } \
        else { printf("  [ OK ] " msg "\n", ##__VA_ARGS__); }          \
    } while (0)

/* ---------- 被测算法（与 tick.c 保持一致） ---------- */

#define SYSTEM_CORE_CLOCK  72000000u

static uint16_t calc_psc(uint32_t core_clock)
{
    return (uint16_t)((core_clock / 1000000u) - 1u);
}

static uint16_t calc_arr(void)
{
    return (uint16_t)(1000u - 1u);
}

/** 计算实际产生的中断频率（Hz） */
static double calc_freq(uint32_t core_clock, uint16_t psc, uint16_t arr)
{
    double timer_clk = (double)core_clock / ((double)psc + 1.0);
    return timer_clk / ((double)arr + 1.0);
}

/** 模拟清 SR 的位操作：写 0 有效 */
static uint16_t clear_uif(uint16_t sr)
{
    return (uint16_t)(sr & ~0x0001u);
}

/* ---------- 测试 ---------- */

int main(void)
{
    printf("=== TIM3 毫秒时基配置验证 ===\n\n");

    /* 1. PSC / ARR 取值 */
    printf("[分频参数]\n");
    {
        uint16_t psc = calc_psc(SYSTEM_CORE_CLOCK);
        uint16_t arr = calc_arr();

        CHECK(psc == 71, "PSC = 71（实际 %u）", psc);
        CHECK(arr == 999, "ARR = 999（实际 %u）", arr);

        /* 2. 频率必须正好 1000Hz = 1ms */
        double f = calc_freq(SYSTEM_CORE_CLOCK, psc, arr);
        printf("  实际频率 = %.3f Hz\n", f);
        CHECK(f > 999.9 && f < 1000.1, "频率 = 1000Hz = 1ms/次（实际 %.3f）", f);

        /* 3. 计数频率检查 */
        double cnt = (double)SYSTEM_CORE_CLOCK / ((double)psc + 1.0);
        printf("  计数频率 = %.0f Hz\n", cnt);
        CHECK(cnt == 1000000.0, "计数频率 = 1MHz（1us 一次）");
    }

    /* 4. 中断标志清除：写 0 有效 */
    printf("\n[SR 标志清除]\n");
    {
        uint16_t sr_with_uif = 0x0021;   /* UIF(bit0) + CC1IF(bit1) */

        /* 正确做法：直接赋值，UIF 位写 0 */
        uint16_t after = clear_uif(sr_with_uif);
        CHECK((after & 0x0001) == 0, "UIF 已清除");
        CHECK((after & 0x0020) == 0x0020, "其他位不受影响");

        /* 反证：|= 系列写法都无法清除已置位的标志
         *
         * C 语言里 `x |= mask` 只能把位**置 1**，永远无法清 0。
         * 所以任何 |= 写法都不可能清掉 UIF —— 这是最容易犯的错。
         *
         * 另外注意整型提升：~0x01u 是 0xFFFFFFFE（unsigned int），
         * sr_with_uif(0x0021) | 0xFFFFFFFE = 0xFFFFFFFF，
         * 截断成 uint16_t 后是 0xFFFF，bit0 仍为 1，且其他位全被污染。 */
        uint16_t wrong = (uint16_t)(sr_with_uif | ~0x0001u);
        CHECK(wrong == 0xFFFF, "|= ~0x01 结果是 0xFFFF（实际 0x%04X）", wrong);
        CHECK((wrong & 0x0001) == 0x0001, "|= ~0x01 无法清除 bit0（|= 只能置位）");
        CHECK((wrong & 0x0020) == 0x0020, "|= ~0x01 把 bit5 也污染了");

        /* 关键：正确实现应该只清 bit0，其他位保持原值 */
        CHECK((after & 0x0001) == 0, "直接赋值清除 bit0");
        CHECK((after & 0xFFFE) == (sr_with_uif & 0xFFFE),
              "直接赋值不动其余位（正确做法）");

        /* 最直观的错误写法 */
        uint16_t bad = (uint16_t)(sr_with_uif | 0x0001u);
        CHECK((bad & 0x0001) == 0x0001, "|= 0x01 也无法清除 UIF");
        CHECK(bad == sr_with_uif, "|= 0x01 是空操作（标志永远清不掉）");
    }

    /* 5. NVIC 位运算 */
    printf("\n[NVIC 配置]\n");
    {
        /* TIM3_IRQn = 29，落在 ISER[0]（覆盖 0~31） */
        int irqn = 29;
        int reg_idx = irqn / 32;
        int bit_idx = irqn % 32;

        CHECK(reg_idx == 0, "ISER 下标 = 0（实际 %d）", reg_idx);
        CHECK(bit_idx == 29, "位号 = 29（实际 %d）", bit_idx);

        uint32_t iser0 = (1u << bit_idx);
        CHECK(iser0 == 0x20000000u, "ISER[0] 写入 0x20000000（实际 0x%08X）", iser0);

        /* 优先级：抢占 3，子 0 → 高 4 位 = 0x30 */
        uint8_t ip = (uint8_t)(3u << 4);
        CHECK(ip == 0x30, "NVIC->IP[29] = 0x30（实际 0x%02X）", ip);
    }

    /* 6. 毫秒累加与回绕 */
    printf("\n[毫秒累加]\n");
    {
        uint32_t ticks = 0;
        /* 累加 1000 次 = 1 秒 */
        for (int i = 0; i < 1000; i++) ticks++;
        CHECK(ticks == 1000, "1000 次中断 = 1000ms");

        /* 回绕：32 位约 49.7 天 */
        uint32_t wrap = 0xFFFFFFFFu;
        wrap++;
        CHECK(wrap == 0, "0xFFFFFFFF + 1 回绕到 0");

        /* 回绕后的时间差计算必须用无符号减法 */
        uint32_t before = 0xFFFFFF00u;
        uint32_t after  = 0x00000100u;
        uint32_t diff = after - before;   /* 无符号运算自动处理回绕 */
        CHECK(diff == 512, "跨回绕的时间差正确（实际 %u）", diff);
    }

    /* 7. 72MHz 下的实际精度 */
    printf("\n[精度分析]\n");
    {
        uint16_t psc = 71, arr = 999;
        /* 每 ms 的计数值 */
        uint32_t per_ms = ((SYSTEM_CORE_CLOCK / (psc + 1)) / 1000) * (arr + 1) / 1000;
        printf("  每毫秒计数 = %u\n", per_ms);
        CHECK(per_ms == 1000, "每 ms 正好 1000 个计数");

        /* 误差：72MHz 整除 1MHz，无误差 */
        CHECK(SYSTEM_CORE_CLOCK % 1000000u == 0, "72MHz 能整除 1MHz，无分频误差");
    }

    printf("\n=== %d 项检查，%d 项失败 ===\n", g_run, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
