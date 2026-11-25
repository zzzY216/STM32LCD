/**
 * @file    app_link.c
 * @brief   命令分发实现。
 */
#include "app_link.h"
#include "proto.h"
#include "uart_link.h"
#include "img_rx.h"
#include "lcd.h"
#include "lcd_init.h"
#include "delay.h"
#include "tick.h"
#include "gif_data.h"

/* ==================== 内部状态 ==================== */

static struct {
    play_state_t play;          /* 播放状态 */
    u8   frame_index;           /* 当前 GIF 帧号 */
    u32  frame_last_ms;         /* 上一帧切换时间 */
    u16  img_x;                 /* 图片显示位置 */
    u16  img_y;
    u8   img_pending_lo;        /* 图像数据里"半个像素"暂存 */
    u8   img_half;              /* 1 = 收到了高字节，等待低字节 */
    u32  img_total_bytes;       /* 本次图像已收字节数（校验用） */
    u32  img_expect_bytes;      /* 期望字节数 */
} s_app;

/** GIF 播放帧间隔（毫秒） */
#define APP_GIF_FRAME_MS        50u

/** GIF 显示放大倍数（与 main.c 原值一致） */
#define APP_GIF_SCALE           3u

/** GIF 显示位置 */
#define APP_GIF_X               24u
#define APP_GIF_Y               24u

/* ==================== 前向声明 ==================== */

static proto_err_t app_handle_frame(const proto_frame_t *f);
static void app_play_gif_step(u32 now_ms);

/* ==================== 初始化 ==================== */

void app_link_init(u32 baud)
{
    s_app.play            = PLAY_STOPPED;
    s_app.frame_index     = 0u;
    s_app.frame_last_ms   = 0u;
    s_app.img_x           = 0u;
    s_app.img_y           = 0u;
    s_app.img_pending_lo  = 0u;
    s_app.img_half        = 0u;
    s_app.img_total_bytes = 0u;
    s_app.img_expect_bytes= 0u;

    /* 串口初始化 */
    uart_link_init(baud);

    /* 协议层接线：收到的帧交给 app_handle_frame，应答通过串口发出 */
    proto_reset();
    proto_set_callback(app_handle_frame);
    proto_set_tx(uart_link_send);

    /* 图像接收器复位 */
    img_rx_reset();

    /* 默认缩放 1 倍、背光全亮 */
    img_rx_set_scale(1u);
    LCD_SetBrightness(100u);
}

/* ==================== 命令处理 ==================== */

/**
 * @brief 处理一帧已通过 CRC 校验的指令。
 * @retval 错误码，会被填进 ACK/NAK
 */
static proto_err_t app_handle_frame(const proto_frame_t *f)
{
    const u8 *p = f->payload;

    switch (f->cmd) {

    /* ---------- 心跳 ---------- */
    case PROTO_CMD_PING:
        /* 直接回 ACK 即可，无需额外动作 */
        return PROTO_ERR_NONE;

    /* ---------- 图像传输 ---------- */
    case PROTO_CMD_IMAGE_BEGIN:
    {
        u16 w, h, x, y;
        u8  fmt;
        u8  err;

        if (f->len < 9u) {
            return PROTO_ERR_PARAM;
        }

        /* 小端解析：w, h, x, y 各 2 字节，fmt 1 字节 */
        w = (u16)((u16)p[0] | ((u16)p[1] << 8));
        h = (u16)((u16)p[2] | ((u16)p[3] << 8));
        x = (u16)((u16)p[4] | ((u16)p[5] << 8));
        y = (u16)((u16)p[6] | ((u16)p[7] << 8));
        fmt = p[8];

        /* 收到新图：先停掉 GIF，避免画面撕裂 */
        s_app.play = PLAY_STOPPED;

        s_app.img_x = x;
        s_app.img_y = y;

        err = img_rx_begin(w, h, x, y, fmt);
        if (err != (u8)PROTO_ERR_NONE) {
            return (proto_err_t)err;
        }

        s_app.img_total_bytes  = 0u;
        s_app.img_expect_bytes = (u32)w * (u32)h * 2u;
        s_app.img_half         = 0u;

        return PROTO_ERR_NONE;
    }

    case PROTO_CMD_IMAGE_DATA:
    {
        u16 i;
        u8  rc;

        if (img_rx_get_state() != IMG_RX_ACTIVE) {
            return PROTO_ERR_STATE;
        }

        /* 逐字节拼成像素写入 */
        for (i = 0u; i < f->len; i++) {
            if (s_app.img_half == 0u) {
                s_app.img_pending_lo = p[i];    /* 暂存高字节 */
                s_app.img_half = 1u;
            } else {
                rc = img_rx_push_pixel(s_app.img_pending_lo, p[i]);
                s_app.img_half = 0u;
                if (rc != 0u) {
                    return PROTO_ERR_OVERFLOW;
                }
            }
        }

        s_app.img_total_bytes += f->len;
        return PROTO_ERR_NONE;
    }

    case PROTO_CMD_IMAGE_END:
    {
        u8 rc;

        if (img_rx_get_state() != IMG_RX_ACTIVE) {
            return PROTO_ERR_STATE;
        }

        /* 检查是否收了半截像素 */
        if (s_app.img_half != 0u) {
            img_rx_reset();
            return PROTO_ERR_PARAM;
        }

        /* 字节数必须与声明一致 */
        if (s_app.img_total_bytes != s_app.img_expect_bytes) {
            img_rx_reset();
            return PROTO_ERR_LEN;
        }

        rc = img_rx_end();
        return (rc == 0u) ? PROTO_ERR_NONE : PROTO_ERR_LEN;
    }

    /* ---------- 播放控制 ---------- */
    case PROTO_CMD_PLAY:
        s_app.play = PLAY_RUNNING;
        s_app.frame_last_ms = 0u;   /* 立刻切下一帧 */
        return PROTO_ERR_NONE;

    case PROTO_CMD_PAUSE:
        s_app.play = PLAY_PAUSED;
        return PROTO_ERR_NONE;

    case PROTO_CMD_STOP:
        s_app.play = PLAY_STOPPED;
        img_rx_reset();
        LCD_Fill(0, 0, LCD_W, LCD_H, BLACK);
        return PROTO_ERR_NONE;

    /* ---------- 显示参数 ---------- */
    case PROTO_CMD_SET_SCALE:
        if (f->len < 1u) {
            return PROTO_ERR_PARAM;
        }
        if (p[0] < IMG_RX_SCALE_MIN || p[0] > IMG_RX_SCALE_MAX) {
            return PROTO_ERR_PARAM;
        }
        img_rx_set_scale(p[0]);
        LCD_SetScale(p[0]);
        return PROTO_ERR_NONE;

    case PROTO_CMD_SET_POS:
        if (f->len < 4u) {
            return PROTO_ERR_PARAM;
        }
        s_app.img_x = (u16)((u16)p[0] | ((u16)p[1] << 8));
        s_app.img_y = (u16)((u16)p[2] | ((u16)p[3] << 8));
        img_rx_set_pos(s_app.img_x, s_app.img_y);
        return PROTO_ERR_NONE;

    case PROTO_CMD_SET_BRIGHT:
        if (f->len < 1u) {
            return PROTO_ERR_PARAM;
        }
        if (p[0] > 100u) {
            return PROTO_ERR_PARAM;
        }
        LCD_SetBrightness(p[0]);
        return PROTO_ERR_NONE;

    case PROTO_CMD_CLEAR:
    {
        u16 color;
        if (f->len < 2u) {
            /* 无参数时默认黑色 */
            color = BLACK;
        } else {
            color = (u16)((u16)p[0] | ((u16)p[1] << 8));
        }
        s_app.play = PLAY_STOPPED;
        LCD_Fill(0, 0, LCD_W, LCD_H, color);
        return PROTO_ERR_NONE;
    }

    /* ---------- 应答类命令不该由我们接收 ---------- */
    case PROTO_CMD_ACK:
    case PROTO_CMD_NAK:
    case PROTO_CMD_PONG:
        return PROTO_ERR_NONE;

    default:
        return PROTO_ERR_CMD;
    }
}

/* ==================== 播放推进 ==================== */

/**
 * @brief 推进一帧 GIF。
 * @note  沿用原 main.c 的显示方式：LCD_ShowPicture_Scale。
 *        当前 GIF 是 64x64、16 色调色板，放大 3 倍显示为 192x192。
 */
static void app_play_gif_step(u32 now_ms)
{
    /* 未到换帧时间就什么都不做，避免全速刷屏 */
    if (s_app.frame_last_ms != 0u) {
        if ((u32)(now_ms - s_app.frame_last_ms) < APP_GIF_FRAME_MS) {
            return;
        }
    }
    s_app.frame_last_ms = now_ms;

    LCD_ShowPicture_Scale((u16)APP_GIF_X, (u16)APP_GIF_Y,
                          (u16)GIF_WIDTH, (u16)GIF_HEIGHT,
                          (u8)APP_GIF_SCALE,
                          gif_palette,
                          gif_frames[s_app.frame_index]);

    s_app.frame_index++;
    if (s_app.frame_index >= GIF_FRAMES) {
        s_app.frame_index = 0u;
    }
}

/* ==================== 主循环 ==================== */

void app_link_poll(void)
{
    u8  b;
    u32 now;
    u8  overflow;

    /* 0. 推进毫秒时基（必须在最前面，保证时间准确） */
    tick_poll();

    /* 1. 把串口缓冲里的字节全部喂给协议解析器。
     *    这里一次性取空，保证高波特率下不丢数据。 */
    while (uart_link_read(&b)) {
        proto_feed_byte(b);
    }

    /* 2. 处理接收超时（半帧卡住的情况） */
    now = tick_get_ms();
    proto_poll_timeout(now);

    /* 3. 维持背光 PWM */
    LCD_BrightnessTick();

    /* 4. 缓冲溢出保护：说明主循环太慢，清空重来 */
    overflow = uart_link_take_overflow_flag();
    if (overflow) {
        uart_link_flush();
        proto_reset();
    }

    /* 5. 播放推进 */
    if (s_app.play == PLAY_RUNNING) {
        app_play_gif_step(now);
    }
}

play_state_t app_link_get_play_state(void)
{
    return s_app.play;
}

void app_link_play(void)
{
    s_app.play = PLAY_RUNNING;
    s_app.frame_last_ms = 0u;   /* 立刻切下一帧 */
}

void app_link_pause(void)
{
    s_app.play = PLAY_PAUSED;
}

void app_link_stop(void)
{
    s_app.play = PLAY_STOPPED;
    img_rx_reset();
    LCD_Fill(0, 0, LCD_W, LCD_H, BLACK);
}
