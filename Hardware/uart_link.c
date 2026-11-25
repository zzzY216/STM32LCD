/**
 * @file    uart_link.c
 * @brief   USART1 中断接收 + 环形缓冲实现。
 */
#include "uart_link.h"

/* ==================== 接收环形缓冲 ==================== */

static volatile uint8_t  s_rx_buf[UART_LINK_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0u;    /* 中断写入位置 */
static volatile uint16_t s_rx_tail = 0u;    /* 主循环读取位置 */
static volatile uint8_t  s_rx_overflow = 0u;

/* ==================== 初始化 ==================== */

void uart_link_init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    /* 1. 时钟：GPIOA + USART1（都在 APB2） */
    RCC_APB2PeriphClockCmd(UART_LINK_GPIO_CLK | UART_LINK_USART_CLK, ENABLE);

    /* 2. PA9 = TX，复用推挽输出 */
    gpio.GPIO_Pin   = UART_LINK_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(UART_LINK_GPIO_PORT, &gpio);

    /* 3. PA10 = RX，上拉输入（浮空时容易收到噪声，上拉更稳） */
    gpio.GPIO_Pin  = UART_LINK_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(UART_LINK_GPIO_PORT, &gpio);

    /* 4. USART 参数：8 数据位、无校验、1 停止位、无流控 */
    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(UART_LINK_USART, &usart);

    /* 5. 只开接收中断（RXNE），发送用轮询 */
    USART_ITConfig(UART_LINK_USART, USART_IT_RXNE, ENABLE);

    /* 6. 中断优先级：比 SysTick 低一档，避免影响延时精度 */
    nvic.NVIC_IRQChannel                   = UART_LINK_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority        = 0u;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    /* 7. 使能串口 */
    USART_Cmd(UART_LINK_USART, ENABLE);

    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_rx_overflow = 0u;
}

/* ==================== 中断服务 ==================== */

/**
 * @brief 接收中断：只做"取字节 -> 存缓冲"，立刻返回。
 * @note  绝不在这里做解析或写屏，否则会严重拖慢主循环。
 */
void UART_LINK_IRQHandler(void)
{
    if (USART_GetITStatus(UART_LINK_USART, USART_IT_RXNE) != RESET) {
        uint8_t data = (uint8_t)(USART_ReceiveData(UART_LINK_USART) & 0xFFu);
        uint16_t next = (uint16_t)((s_rx_head + 1u) & (UART_LINK_RX_BUF_SIZE - 1u));

        if (next == s_rx_tail) {
            /* 缓冲满：丢弃这个字节并置标志。
             * 不覆盖旧数据，保证已收到的帧不被破坏。 */
            s_rx_overflow = 1u;
        } else {
            s_rx_buf[s_rx_head] = data;
            s_rx_head = next;
        }

        /* 清中断标志（读 SR 再读 DR 已清除 RXNE，这里显式再清一次更保险） */
        USART_ClearITPendingBit(UART_LINK_USART, USART_IT_RXNE);
    }
}

/* ==================== 读取 ==================== */

uint8_t uart_link_read(uint8_t *b)
{
    if (b == 0) {
        return 0u;
    }
    if (s_rx_tail == s_rx_head) {
        return 0u;      /* 空 */
    }

    *b = s_rx_buf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (UART_LINK_RX_BUF_SIZE - 1u));
    return 1u;
}

void uart_link_flush(void)
{
    /* 只动 tail，避免与中断里的 head 竞争 */
    s_rx_tail = s_rx_head;
}

uint8_t uart_link_take_overflow_flag(void)
{
    uint8_t f = s_rx_overflow;
    s_rx_overflow = 0u;
    return f;
}

/* ==================== 发送 ==================== */

void uart_link_send(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (data == 0) {
        return;
    }

    for (i = 0u; i < len; i++) {
        /* 等发送寄存器空 */
        while (USART_GetFlagStatus(UART_LINK_USART, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(UART_LINK_USART, data[i]);
    }

    /* 等最后一字节真正发完，避免调用方立刻改缓冲导致数据错 */
    while (USART_GetFlagStatus(UART_LINK_USART, USART_FLAG_TC) == RESET) {
    }
}
