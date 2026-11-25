/**
 * @file    uart_link.h
 * @brief   ESP32 通讯串口（USART1）驱动。
 * @details 中断只负责把字节塞进环形缓冲，解析在主循环完成。
 *          这样中断停留时间极短，不会影响屏幕刷新。
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include "sys.h"

/* ==================== 引脚与参数 ==================== */

/** USART1：PA9 = TX，PA10 = RX */
#define UART_LINK_TX_PIN        GPIO_Pin_9
#define UART_LINK_RX_PIN        GPIO_Pin_10
#define UART_LINK_GPIO_PORT     GPIOA
#define UART_LINK_GPIO_CLK      RCC_APB2Periph_GPIOA
#define UART_LINK_USART_CLK     RCC_APB2Periph_USART1
#define UART_LINK_USART         USART1
#define UART_LINK_IRQn          USART1_IRQn
#define UART_LINK_IRQHandler    USART1_IRQHandler

/**
 * @brief 默认波特率。
 * @note  115200 传 115KB 一帧约需 10 秒，太慢；
 *        921600 约需 1.3 秒。若接线质量撑不住可降到 460800。
 */
#define UART_LINK_DEFAULT_BAUD  921600u

/** 接收环形缓冲大小。必须是 2 的幂，便于用掩码取模。 */
#define UART_LINK_RX_BUF_SIZE   512u

/* ==================== 接口 ==================== */

/**
 * @brief 初始化 USART1 与接收中断。
 * @param baud 波特率，如 921600
 */
void uart_link_init(uint32_t baud);

/**
 * @brief 从接收缓冲取出一个字节。
 * @param b 输出字节
 * @retval 1 = 取到，0 = 缓冲为空
 */
uint8_t uart_link_read(uint8_t *b);

/**
 * @brief 发送数据（轮询方式，阻塞到发完）。
 * @param data 数据
 * @param len  长度
 * @note  只在回应答时使用，数据量很小（几十字节）。
 */
void uart_link_send(const uint8_t *data, uint16_t len);

/**
 * @brief 查询接收缓冲是否曾经溢出过。
 * @retval 1 = 溢出过（说明主循环处理太慢或波特率过高）
 * @note  读取后自动清零。
 */
uint8_t uart_link_take_overflow_flag(void);

/**
 * @brief 清空接收缓冲（重新同步时用）。
 */
void uart_link_flush(void);

#endif /* UART_LINK_H */
