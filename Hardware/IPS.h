#ifndef __IPS_H
#define __IPS_H

#include <stdint.h>

#define IPS_WIDTH 240
#define IPS_HEIGHT 240

/*引脚*/
#define LCD_RES_Port GPIOB
#define LCD_RES_Pin GPIO_PIN_0

#define LCD_DC_Port GPIOB
#define LCD_DC_Pin GPIO_PIN_1

#define LCD_BLK_Port GPIOB
#define LCD_BLK_Pin GPIO_PIN_10

/* 常用 RGB565 颜色 */
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F

void ST7789_Init(SPI_HandleTypeDef *hspi);
void ST7789_Backlight(uint8_t on);
void ST7789_FillScreen(uint16_t color);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

#endif