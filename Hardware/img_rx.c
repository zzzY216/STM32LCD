/**
 * @file    img_rx.c
 * @brief   图像流式直写实现。
 *
 * 【内存策略】
 *   不做全帧缓冲（240x240x2 = 112.5KB，远超 20KB RAM）。
 *   只用一个"行缓冲"保存当前行，用于纵向重复写入实现缩放：
 *
 *     scale = 1：收到一像素就写一次
 *     scale = N：收到一像素横向写 N 次（立即）；
 *                收满一整行后，再把该行重复写 (N-1) 次
 *
 *   行缓冲大小 = 240 像素 * 2 字节 = 480 字节。
 */
#include "img_rx.h"
#include "lcd.h"
#include "lcd_init.h"
#include "proto.h"

/* ==================== 内部状态 ==================== */

/** 行缓冲：保存当前行已写入的 RGB565 颜色，用于纵向重复 */
static u16 s_line_buf[IMG_RX_SCREEN_W];

static struct {
    img_rx_state_t state;
    u16  img_w;         /* 图像原始宽 */
    u16  img_h;         /* 图像原始高 */
    u16  pos_x;         /* 屏幕左上角 */
    u16  pos_y;
    u16  col;           /* 当前行内列号 0..img_w-1 */
    u16  row;           /* 当前行号 0..img_h-1 */
    u32  pixels;        /* 已收像素总数 */
    u8   scale;         /* 缩放倍数 */
} s_img;

/* 待设置的显示位置（BEGIN 时生效） */
static u16 s_pending_x = 0u;
static u16 s_pending_y = 0u;

/* ==================== 内部函数 ==================== */

/**
 * @brief 把当前行重复写入 (scale-1) 次，实现纵向放大。
 * @note  横向放大已在 push_pixel 里完成。
 */
/**
 * @brief 把当前行重复写入 (scale-1) 次，实现纵向放大。
 *
 * 【关键】纵向重复的每一行，必须是"已经横向放大过"的整行，
 *        所以每个源像素要写 scale 次。
 *        早期版本这里只写了 img_w 次，导致 scale>1 时
 *        放大后的行宽不足，画面会出问题。
 */
static void flush_extra_rows(void)
{
    u8  rep;
    u16 i;
    u8  sx;

    if (s_img.scale <= 1u) {
        return;
    }

    for (rep = 1u; rep < s_img.scale; rep++) {
        for (i = 0u; i < s_img.img_w; i++) {
            for (sx = 0u; sx < s_img.scale; sx++) {
                LCD_WR_DATA(s_line_buf[i]);
            }
        }
    }
}

/* ==================== 对外接口 ==================== */

void img_rx_reset(void)
{
    s_img.state  = IMG_RX_IDLE;
    s_img.img_w  = 0u;
    s_img.img_h  = 0u;
    s_img.pos_x  = 0u;
    s_img.pos_y  = 0u;
    s_img.col    = 0u;
    s_img.row    = 0u;
    s_img.pixels = 0u;
    /* scale 保留用户设定，不因复位丢失 */
}

void img_rx_set_scale(uint8_t scale)
{
    if (scale < IMG_RX_SCALE_MIN) {
        scale = IMG_RX_SCALE_MIN;
    }
    if (scale > IMG_RX_SCALE_MAX) {
        scale = IMG_RX_SCALE_MAX;
    }
    s_img.scale = scale;
}

uint8_t img_rx_get_scale(void)
{
    return (s_img.scale == 0u) ? 1u : s_img.scale;
}

void img_rx_set_pos(uint16_t x, uint16_t y)
{
    s_pending_x = x;
    s_pending_y = y;
}

uint8_t img_rx_begin(u16 w, u16 h, u16 x, u16 y, u8 fmt)
{
    u32 out_w;
    u32 out_h;

    /* 只支持 RGB565 */
    if (fmt != 0u) {
        return (uint8_t)PROTO_ERR_PARAM;
    }
    if (w == 0u || h == 0u) {
        return (uint8_t)PROTO_ERR_PARAM;
    }
    if (w > IMG_RX_SCREEN_W || h > IMG_RX_SCREEN_H) {
        return (uint8_t)PROTO_ERR_PARAM;
    }

    /* 计算放大后的实际尺寸，必须仍在屏幕内 */
    out_w = (u32)w * (u32)img_rx_get_scale();
    out_h = (u32)h * (u32)img_rx_get_scale();
    if ((x + out_w) > IMG_RX_SCREEN_W || (y + out_h) > IMG_RX_SCREEN_H) {
        return (uint8_t)PROTO_ERR_PARAM;
    }

    s_img.state  = IMG_RX_ACTIVE;
    s_img.img_w  = w;
    s_img.img_h  = h;
    s_img.pos_x  = x;
    s_img.pos_y  = y;
    s_img.col    = 0u;
    s_img.row    = 0u;
    s_img.pixels = 0u;

    /* 设定写入窗口（放大后的实际显示区域） */
    LCD_Address_Set(x, y,
                    (u16)(x + out_w - 1u),
                    (u16)(y + out_h - 1u));

    return (uint8_t)PROTO_ERR_NONE;
}

uint8_t img_rx_push_pixel(uint8_t hi, uint8_t lo)
{
    u16 color;
    u8  rep;

    if (s_img.state != IMG_RX_ACTIVE) {
        return 1u;
    }

    /* 像素数超限：防止上层发的数据比声明的多 */
    if (s_img.pixels >= ((u32)s_img.img_w * s_img.img_h)) {
        s_img.state = IMG_RX_ERROR;
        return 1u;
    }

    color = (u16)(((u16)hi << 8) | (u16)lo);

    /* 记录到行缓冲（供纵向重复用） */
    s_line_buf[s_img.col] = color;

    /* 横向重复 scale 次 */
    for (rep = 0u; rep < img_rx_get_scale(); rep++) {
        LCD_WR_DATA(color);
    }

    s_img.pixels++;
    s_img.col++;

    /* 一行收完 */
    if (s_img.col >= s_img.img_w) {
        s_img.col = 0u;
        flush_extra_rows();         /* 纵向重复 */
        s_img.row++;
    }

    return 0u;
}

uint8_t img_rx_end(void)
{
    u32 expect;
    u32 got;

    if (s_img.state != IMG_RX_ACTIVE) {
        img_rx_reset();
        return 1u;
    }

    expect = (u32)s_img.img_w * s_img.img_h;
    got    = s_img.pixels;

    /* 必须先保存计数再复位，否则比较永远失败 */
    img_rx_reset();

    return (got == expect) ? 0u : 1u;
}

img_rx_state_t img_rx_get_state(void)
{
    return s_img.state;
}

u32 img_rx_get_pixel_count(void)
{
    return s_img.pixels;
}
