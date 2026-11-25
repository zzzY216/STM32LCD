/**
 * @file    img_rx.h
 * @brief   图像流式接收与直写屏幕。
 * @details 【核心约束】STM32F103C8 只有 20KB RAM，
 *          240x240 RGB565 一整帧需要 112.5KB，**放不下**。
 *
 *          所以采用"边收边写"：每收到一个像素立刻写进屏幕 GRAM，
 *          不做全帧缓冲。RAM 只占用两个行缓冲用于实现缩放。
 *
 *          代价：只能从上到下、从左到右顺序绘制，
 *          不能整屏重绘，也不支持任意旋转。对"显示手机发来的图片"够用。
 */
#ifndef IMG_RX_H
#define IMG_RX_H

#include "sys.h"

/* ==================== 参数 ==================== */

/** 屏幕尺寸，与 LCD_Init.h 的 LCD_W / LCD_H 保持一致 */
#define IMG_RX_SCREEN_W         240u
#define IMG_RX_SCREEN_H         240u

/** 支持的缩放倍数范围 */
#define IMG_RX_SCALE_MIN        1u
#define IMG_RX_SCALE_MAX        4u

/* ==================== 状态 ==================== */

typedef enum {
    IMG_RX_IDLE = 0,        /**< 空闲 */
    IMG_RX_ACTIVE,          /**< 正在接收图像数据 */
    IMG_RX_ERROR            /**< 出过错，需重新 BEGIN */
} img_rx_state_t;

/* ==================== 接口 ==================== */

/**
 * @brief 复位图像接收器。
 */
void img_rx_reset(void);

/**
 * @brief 设置缩放倍数。
 * @param scale 1 ~ 4；超出范围会被钳位
 */
void img_rx_set_scale(uint8_t scale);

/** @brief 读取当前缩放倍数 */
uint8_t img_rx_get_scale(void);

/**
 * @brief 设置显示位置（左上角）。
 * @note  仅在 img_rx_begin 时生效。
 */
void img_rx_set_pos(uint16_t x, uint16_t y);

/**
 * @brief 开始接收一帧图像。
 * @param w   图像宽（像素）
 * @param h   图像高（像素）
 * @param x   屏幕左上角 X
 * @param y   屏幕左上角 Y
 * @param fmt 像素格式，目前只支持 0 = RGB565
 * @retval PROTO_ERR_NONE 成功；其他为错误码
 * @details 内部会调用 LCD_Address_Set 设定写入窗口。
 */
uint8_t img_rx_begin(uint16_t w, uint16_t h, uint16_t x, uint16_t y, uint8_t fmt);

/**
 * @brief 写入一个像素（RGB565，高字节在前）。
 * @param hi 颜色高字节
 * @param lo 颜色低字节
 * @retval 0 = 成功；1 = 状态错误或像素超出声明尺寸
 * @note  调用方负责把图像数据流按像素拆成双字节喂进来。
 */
uint8_t img_rx_push_pixel(uint8_t hi, uint8_t lo);

/**
 * @brief 结束一帧。
 * @retval 0 = 成功（像素数正好）；1 = 像素数不符
 */
uint8_t img_rx_end(void);

/** @brief 查询当前状态 */
img_rx_state_t img_rx_get_state(void);

/** @brief 查询已收到的像素数（调试用） */
uint32_t img_rx_get_pixel_count(void);

#endif /* IMG_RX_H */
