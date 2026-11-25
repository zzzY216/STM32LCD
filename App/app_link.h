/**
 * @file    app_link.h
 * @brief   ESP32 命令分发层（应用层）。
 * @details 把协议层解析出的帧映射成具体动作：
 *          图片流、播放控制、缩放、亮度。
 *
 *          与 proto.c 的分工：
 *            proto.c   —— 只管"字节流 -> 一帧数据"，不关心命令含义
 *            app_link.c —— 只管"这一帧要干什么"，不关心字节怎么收
 */
#ifndef APP_LINK_H
#define APP_LINK_H

#include "sys.h"
#include "proto.h"

/* ==================== 播放状态 ==================== */

typedef enum {
    PLAY_STOPPED = 0,   /**< 停止（停在当前画面） */
    PLAY_RUNNING,       /**< 正在播放 GIF */
    PLAY_PAUSED         /**< 暂停（停在当前帧） */
} play_state_t;

/* ==================== 接口 ==================== */

/**
 * @brief 初始化命令分发层。
 * @details 内部会把 proto 的收发函数接好。
 * @param baud 串口波特率（如 921600）
 */
void app_link_init(u32 baud);

/**
 * @brief 主循环处理函数，必须尽可能频繁地调用。
 * @details 内部完成：
 *            1. 从串口环形缓冲取出所有字节喂给协议解析器
 *            2. 检查协议超时
 *            3. 维持背光 PWM
 *            4. 按播放状态推进 GIF 帧（若在播放）
 */
void app_link_poll(void);

/** @brief 获取当前播放状态 */
play_state_t app_link_get_play_state(void);

/**
 * @brief 开始播放 GIF（等价于收到 PLAY 命令）。
 */
void app_link_play(void);

/**
 * @brief 暂停播放。
 */
void app_link_pause(void);

/**
 * @brief 停止播放并清屏。
 */
void app_link_stop(void);

#endif /* APP_LINK_H */
