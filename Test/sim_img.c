/**
 * @file    sim_img.c
 * @brief   图像接收模块的 PC 端验证。
 * @details 用桩函数替换 LCD_WR_DATA / LCD_Address_Set，
 *          把写入的颜色记录下来，验证：
 *            1. 像素能按行列正确推进
 *            2. 缩放（scale=1/2/3）产生正确的写入次数
 *            3. 像素数超限能拒绝
 *            4. END 时数量校验
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "img_rx.h"

/* ==================== 测试框架 ==================== */

static int g_run = 0;
static int g_fail = 0;

#define CHECK(cond, msg, ...)                                      \
    do {                                                           \
        g_run++;                                                   \
        if (!(cond)) {                                             \
            g_fail++;                                              \
            printf("  [FAIL] " msg "\n", ##__VA_ARGS__);           \
        } else {                                                   \
            printf("  [ OK ] " msg "\n", ##__VA_ARGS__);           \
        }                                                          \
    } while (0)

/* ==================== LCD 桩 ==================== */

#define MAX_CAPTURE 4096

static uint16_t g_colors[MAX_CAPTURE];
static int      g_color_count = 0;
static uint16_t g_win[4];       /* x1, y1, x2, y2 */
static int      g_win_set = 0;

void LCD_WR_DATA(uint16_t color)
{
    if (g_color_count < MAX_CAPTURE) {
        g_colors[g_color_count] = color;
    }
    g_color_count++;
}

void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    g_win[0] = x1; g_win[1] = y1; g_win[2] = x2; g_win[3] = y2;
    g_win_set = 1;
}

/** 复位采集状态 */
static void reset_capture(void)
{
    g_color_count = 0;
    g_win_set = 0;
    memset(g_colors, 0, sizeof(g_colors));
}

/* ==================== 测试用例 ==================== */

static void test_scale1_basic(void)
{
    uint16_t w = 3, h = 2;      /* 3x2 = 6 像素 */
    uint16_t x, y;
    uint8_t  rc;

    printf("\n[scale=1 基本写入]\n");

    reset_capture();
    img_rx_reset();
    img_rx_set_scale(1);

    rc = img_rx_begin(w, h, 10, 20, 0);
    CHECK(rc == 0, "begin 成功 (rc=%u)", rc);
    CHECK(g_win_set == 1, "设置了写入窗口");
    CHECK(g_win[0] == 10 && g_win[1] == 20, "窗口起点 (10,20)");
    CHECK(g_win[2] == 12 && g_win[3] == 21, "窗口终点 (12,21)");

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint16_t c = (uint16_t)(0x1000 + y * 10 + x);
            rc = img_rx_push_pixel((uint8_t)(c >> 8), (uint8_t)(c & 0xFF));
            CHECK(rc == 0, "像素(%u,%u) ok", x, y);
        }
    }

    CHECK(g_color_count == 6, "写入 6 次颜色（实际 %d）", g_color_count);
    CHECK(img_rx_get_pixel_count() == 6, "计数 6（实际 %u）",
          (unsigned)img_rx_get_pixel_count());

    rc = img_rx_end();
    CHECK(rc == 0, "end 成功（像素数匹配）");
}

static void test_scale2_writes(void)
{
    uint16_t w = 2, h = 2;      /* 2x2 = 4 像素，放大 2 倍 */
    uint16_t i;
    uint8_t  rc;

    printf("\n[scale=2 横向+纵向重复]\n");

    reset_capture();
    img_rx_reset();
    img_rx_set_scale(2);

    rc = img_rx_begin(w, h, 0, 0, 0);
    CHECK(rc == 0, "begin 成功");
    /* 放大后窗口应为 4x4 */
    CHECK(g_win[2] == 3 && g_win[3] == 3, "窗口 4x4 (右上角 3,3)");

    /* 4 个像素，每个颜色不同，便于识别 */
    {
        uint16_t colors[4] = {0xA001, 0xA002, 0xA003, 0xA004};
        for (i = 0; i < 4; i++) {
            rc = img_rx_push_pixel((uint8_t)(colors[i] >> 8),
                                   (uint8_t)(colors[i] & 0xFF));
            CHECK(rc == 0, "像素 %u ok", (unsigned)i);
        }
    }

    /* 每个像素横向写 2 次 => 8 次；每行重复 1 次 => 再加 8 次；共 16 次 */
    CHECK(g_color_count == 16, "写入 16 次颜色（实际 %d）", g_color_count);
    CHECK(img_rx_end() == 0, "end 成功");
}

static void test_scale3_writes(void)
{
    uint16_t w = 1, h = 1;      /* 1x1 像素，放大 3 倍 */
    uint8_t  rc;

    printf("\n[scale=3 单像素放大]\n");

    reset_capture();
    img_rx_reset();
    img_rx_set_scale(3);

    rc = img_rx_begin(w, h, 0, 0, 0);
    CHECK(rc == 0, "begin 成功");
    CHECK(g_win[2] == 2 && g_win[3] == 2, "窗口 3x3 (右上角 2,2)");

    rc = img_rx_push_pixel(0xAB, 0xCD);
    CHECK(rc == 0, "像素 ok");

    /* 横向 3 次 + 纵向再 2 次 = 9 次 */
    CHECK(g_color_count == 9, "写入 9 次颜色（实际 %d）", g_color_count);
    CHECK(img_rx_end() == 0, "end 成功");
}

static void test_pixel_overflow_reject(void)
{
    uint16_t w = 1, h = 1;      /* 只允许 1 个像素 */
    uint8_t  rc;

    printf("\n[像素数超限]\n");

    reset_capture();
    img_rx_reset();
    img_rx_set_scale(1);

    (void)img_rx_begin(w, h, 0, 0, 0);

    rc = img_rx_push_pixel(0x11, 0x22);
    CHECK(rc == 0, "第 1 个像素 ok");

    rc = img_rx_push_pixel(0x33, 0x44);
    CHECK(rc == 1, "第 2 个像素被拒绝 (rc=%u)", rc);

    CHECK(img_rx_get_state() == IMG_RX_ERROR, "状态变为 ERROR");
}

static void test_bad_params(void)
{
    printf("\n[非法参数]\n");

    img_rx_reset();
    img_rx_set_scale(1);

    CHECK(img_rx_begin(0, 10, 0, 0, 0) != 0, "宽=0 被拒绝");
    CHECK(img_rx_begin(10, 0, 0, 0, 0) != 0, "高=0 被拒绝");
    CHECK(img_rx_begin(300, 10, 0, 0, 0) != 0, "宽超屏被拒绝");
    CHECK(img_rx_begin(10, 300, 0, 0, 0) != 0, "高超屏被拒绝");
    CHECK(img_rx_begin(200, 200, 100, 100, 0) != 0, "位置越界被拒绝");
    CHECK(img_rx_begin(10, 10, 0, 0, 1) != 0, "非 RGB565 格式被拒绝");
}

static void test_end_count_mismatch(void)
{
    printf("\n[END 时像素数不符]\n");

    reset_capture();
    img_rx_reset();
    img_rx_set_scale(1);

    (void)img_rx_begin(2, 2, 0, 0, 0);   /* 需要 4 个像素 */
    (void)img_rx_push_pixel(0x00, 0x01);
    (void)img_rx_push_pixel(0x00, 0x02);
    /* 只喂了 2 个就结束 */

    CHECK(img_rx_end() != 0, "像素数不足时 end 返回错误");
}

static void test_scale_clamp(void)
{
    printf("\n[缩放倍数钳位]\n");

    img_rx_reset();

    img_rx_set_scale(0);
    CHECK(img_rx_get_scale() == 1, "scale=0 钳位到 1（实际 %u）",
          img_rx_get_scale());

    img_rx_set_scale(9);
    CHECK(img_rx_get_scale() == 4, "scale=9 钳位到 4（实际 %u）",
          img_rx_get_scale());

    img_rx_set_scale(3);
    CHECK(img_rx_get_scale() == 3, "scale=3 保持（实际 %u）",
          img_rx_get_scale());
}

static void test_push_without_begin(void)
{
    printf("\n[未 begin 就 push]\n");

    img_rx_reset();
    CHECK(img_rx_push_pixel(0x11, 0x22) != 0, "未 begin 时 push 被拒绝");
}

/* ==================== 主函数 ==================== */

int main(void)
{
    printf("=== img_rx.c 图像接收验证 ===\n");

    test_scale1_basic();
    test_scale2_writes();
    test_scale3_writes();
    test_pixel_overflow_reject();
    test_bad_params();
    test_end_count_mismatch();
    test_scale_clamp();
    test_push_without_begin();

    printf("\n=== %d 项检查，%d 项失败 ===\n", g_run, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
