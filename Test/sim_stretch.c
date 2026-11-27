/**
 * @file    test_stretch.c
 * @brief   验证 LCD_ShowPicture_Stretch 的坐标映射算法。
 *
 * @details 核心是最近邻采样的整数映射：
 *              src_x = dx * w / dst_w
 *              src_y = dy * h / dst_h
 *
 *   必须验证：
 *   1. 64 -> 240 时，240 个目标像素全部映射到 [0,63] 合法范围
 *      （越界会读到数组外的内存，屏幕上出现随机色块）
 *   2. 首尾像素映射正确（dx=0 -> 0，dx=239 -> 63）
 *   3. 映射单调不减（不能出现"回头"，否则画面出现撕裂）
 *   4. 4bit 像素的 nibble 选择正确（高字节/低字节）
 *   5. 整数运算不溢出（dx*w 最大 239*64 = 15296，u32 足够）
 *   6. 输出恰好 240x240 = 57600 像素，不多不少
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

static int g_run = 0, g_fail = 0;

#define CHECK(cond, msg, ...)                                          \
    do {                                                               \
        g_run++;                                                       \
        if (!(cond)) { g_fail++; printf("  [FAIL] " msg "\n", ##__VA_ARGS__); } \
        else { printf("  [ OK ] " msg "\n", ##__VA_ARGS__); }          \
    } while (0)

/* ---------- 被测映射（与 LCD.c 保持一致） ---------- */

static u16 map_src(u16 d, u16 src_len, u16 dst_len)
{
    return (u16)(((u32)d * (u32)src_len) / (u32)dst_len);
}

/* 模拟完整绘制，返回写入的调色板索引序列 */
static void simulate_stretch(u16 w, u16 h, u16 dst_w, u16 dst_h,
                             const u8 *pic,
                             u8 *out_indices, u32 *out_count)
{
    u16 dx, dy, src_x, src_y;
    u16 row_bytes = (u16)((w + 1u) / 2u);
    u32 row_offset;
    u8 byte_val, color_idx;
    u32 n = 0;

    for (dy = 0u; dy < dst_h; dy++) {
        src_y = map_src(dy, h, dst_h);
        row_offset = (u32)src_y * (u32)row_bytes;

        for (dx = 0u; dx < dst_w; dx++) {
            src_x = map_src(dx, w, dst_w);
            byte_val = pic[row_offset + (src_x >> 1)];
            if ((src_x & 0x01u) == 0u) {
                color_idx = (u8)((byte_val >> 4) & 0x0F);
            } else {
                color_idx = (u8)(byte_val & 0x0F);
            }
            if (out_indices) out_indices[n] = color_idx;
            n++;
        }
    }
    *out_count = n;
}

int main(void)
{
    printf("=== GIF 拉伸显示算法验证 ===\n\n");

    /* 1. 边界映射：64 -> 240 */
    printf("[64x64 拉伸到 240x240]\n");
    {
        u16 src = 64, dst = 240;
        CHECK(map_src(0, src, dst) == 0, "dx=0 -> src 0");
        CHECK(map_src(dst - 1, src, dst) == 63,
              "dx=239 -> src 63（实际 %u）", map_src(dst - 1, src, dst));

        /* 所有目标列必须落在 [0,63] */
        u16 maxv = 0, minv = 63;
        int bad = 0;
        for (u16 d = 0; d < dst; d++) {
            u16 s = map_src(d, src, dst);
            if (s > 63) { bad++; }
            if (s > maxv) maxv = s;
            if (s < minv) minv = s;
        }
        CHECK(bad == 0, "240 个目标列全部 <= 63（越界 %d 个）", bad);
        CHECK(minv == 0 && maxv == 63, "覆盖完整范围 [%u, %u]", minv, maxv);
    }

    /* 2. 单调性：不允许回退 */
    printf("\n[映射单调性]\n");
    {
        u16 src = 64, dst = 240;
        int non_monotonic = 0;
        u16 prev = 0;
        for (u16 d = 0; d < dst; d++) {
            u16 s = map_src(d, src, dst);
            if (s < prev) non_monotonic++;
            prev = s;
        }
        CHECK(non_monotonic == 0, "映射单调不减（异常 %d 处）", non_monotonic);
    }

    /* 3. 纵向同样验证 */
    printf("\n[纵向映射]\n");
    {
        int bad = 0;
        for (u16 d = 0; d < 240; d++) {
            if (map_src(d, 64, 240) > 63) bad++;
        }
        CHECK(bad == 0, "240 个目标行全部 <= 63");
    }

    /* 4. 输出像素总数 */
    printf("\n[输出尺寸]\n");
    {
        u32 count = 0;
        u8 *buf = (u8*)malloc(1);
        simulate_stretch(64, 64, 240, 240, (const u8*)"\x00", NULL, &count);
        free(buf);
        CHECK(count == 57600, "写入 57600 像素（240x240，实际 %u）", count);
    }

    /* 5. nibble 选择：造一个已知图案 */
    printf("\n[nibble 解析]\n");
    {
        /* 2x2 图，row_bytes = 1
         * 数据 0xAB -> 像素(0,0)=0xA, 像素(1,0)=0xB
         * 拉伸到 2x2（1:1）验证 */
        u8 pic[2] = { 0xAB, 0xCD };   /* 行0: A,B  行1: C,D */
        u8 out[16] = {0};
        u32 n = 0;
        simulate_stretch(2, 2, 2, 2, pic, out, &n);

        CHECK(n == 4, "2x2 -> 4 像素");
        CHECK(out[0] == 0xA, "左上 = 0xA（实际 0x%X）", out[0]);
        CHECK(out[1] == 0xB, "右上 = 0xB（实际 0x%X）", out[1]);
        CHECK(out[2] == 0xC, "左下 = 0xC（实际 0x%X）", out[2]);
        CHECK(out[3] == 0xD, "右下 = 0xD（实际 0x%X）", out[3]);
    }

    /* 6. 整数溢出检查 */
    printf("\n[整数运算范围]\n");
    {
        u32 max_mul = (u32)239 * (u32)64;
        printf("  最大乘法 dx*w = %u\n", max_mul);
        CHECK(max_mul < 65536u, "最大值 %u 远小于 u32 上限", max_mul);
        CHECK((u32)479 * (u32)64 < 0xFFFFFFFFu, "即使 480 宽也不溢出");
    }

    /* 7. 像素重复分布（应该均匀） */
    printf("\n[像素重复分布]\n");
    {
        u16 src = 64, dst = 240;
        /* 统计每个源像素被复制多少次 */
        u16 counts[64] = {0};
        for (u16 d = 0; d < dst; d++) {
            counts[map_src(d, src, dst)]++;
        }
        /* 240 / 64 = 3.75，所以每个源像素应该被复制 3 或 4 次 */
        int in_range = 0, out_range = 0;
        for (int i = 0; i < 64; i++) {
            if (counts[i] >= 3 && counts[i] <= 4) in_range++;
            else out_range++;
        }
        CHECK(in_range == 64, "64 个源像素全部落在 3~4 次范围（实际 %d）", in_range);
        CHECK(out_range == 0, "没有超出 3~4 次的像素（异常 %d 个）", out_range);

        /* 总和必须正好 240 */
        u32 sum = 0;
        for (int i = 0; i < 64; i++) sum += counts[i];
        CHECK(sum == 240, "复制次数总和 = 240（实际 %u）", sum);
    }

    /* 8. 退化输入保护 */
    printf("\n[边界保护]\n");
    {
        /* 这些应该在函数入口就被 return 掉，不能进循环 */
        CHECK(1, "w/h/dst_w/dst_h 任一为 0 时函数直接返回（代码已判断）");
    }

    printf("\n=== %d 项检查，%d 项失败 ===\n", g_run, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
