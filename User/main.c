/**
 * @file    main.c
 * @brief   主程序：GIF 播放 + ESP32 串口指令接收。
 * @details 相比原版改动很小：
 *            1. 增加 tick_init() 与 app_link_init()
 *            2. 主循环改为调用 app_link_poll()，不再用 delay_ms 卡住主循环
 *
 *          为什么去掉 delay_ms(50)：
 *            原版靠阻塞延时控制帧率，主循环会被卡住 50ms，
 *            期间收到的串口数据会堆在缓冲里来不及处理。
 *            现在改为"非阻塞轮询 + 时间戳判断"，串口响应及时得多。
 */
#include "delay.h"
#include "sys.h"
#include "lcd_init.h"
#include "lcd.h"
#include "tick.h"
#include "app_link.h"

int main(void)
{
    /* 1. 基础外设初始化 */
    delay_init();               /* 沿用原工程的延时函数 */
    LCD_Init();                 /* ST7789 初始化 */
    LCD_Fill(0, 0, LCD_W, LCD_H, BLACK);

    /* 2. 时基（GIF 帧率与非阻塞轮询都依赖它） */
    tick_init();

    /* 3. 串口通讯与命令分发
     *    波特率 921600：传一帧 240x240 图约需 1.3 秒。
     *    若接线质量不佳导致丢包，可降到 460800 或 115200。 */
    app_link_init(921600);

    /* 4. 上电默认播放 GIF，行为与原来一致 */
    app_link_play();

    /* 5. 主循环：不做任何阻塞操作 */
    while (1)
    {
        app_link_poll();
    }
}
