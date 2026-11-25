/**
 * @file    tick.h
 * @brief   毫秒时基（基于 TIM3 独立定时器，不占用 SysTick）。
 *
 * @details 原工程 Delay.c 用 SysTick 做阻塞延时，但没有提供
 *          "系统已运行多少毫秒"的接口。播放 GIF 需要按时间间隔换帧，
 *          串口协议也需要超时判断，因此补一个毫秒时基。
 *
 *          【关键设计】用 TIM3，不用 SysTick。
 *          Delay.c 每次延时结束都会关闭 SysTick 计数器，
 *          如果时基也挂在 SysTick 上，两者会互相破坏。
 *          TIM3 是独立外设，与 Delay.c 完全解耦。
 *
 *          详见 tick.c 文件头部的完整说明。
 */
#ifndef TICK_H
#define TICK_H

#include "sys.h"

/**
 * @brief 初始化毫秒时基（配置并使能 TIM3，1ms 中断）。
 * @note  必须在主循环开始前调用一次。
 *        重复调用会重新从 0 开始计时。
 */
void tick_init(void);

/**
 * @brief 主循环里周期性调用。
 * @details 当前实现中时间是靠 TIM3 中断累加的，本函数只做
 *          "未初始化则补初始化"的保护，几乎无开销。
 *
 *          保留它是为了接口稳定，也便于将来更换时基方案。
 */
void tick_poll(void);

/**
 * @brief 获取自初始化以来的毫秒数。
 * @retval 毫秒计数（32 位，约 49.7 天回绕一次）
 */
u32 tick_get_ms(void);

#endif /* TICK_H */
