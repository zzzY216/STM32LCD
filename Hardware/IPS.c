#include "IPS.h"

static SPI_HandleTypeDef *lcd_spi;
/* 单行发送缓存，加快刷屏速度（240像素 * 2字节 = 480字节） */
static uint8_t line_buf[IPS_WIDTH * 2];

#define LCD_RES_LOW()   HAL_GPIO_WritePin(LCD_RES_Port, LCD_RES_Pin, GPIO_PIN_RESET)
#define LCD_RES_HIGH()  HAL_GPIO_WritePin(LCD_RES_Port, LCD_RES_Pin, GPIO_PIN_SET)
#define LCD_DC_CMD()    HAL_GPIO_WritePin(LCD_DC_Port,  LCD_DC_Pin,  GPIO_PIN_RESET)
#define LCD_DC_DATA()   HAL_GPIO_WritePin(LCD_DC_Port,  LCD_DC_Pin,  GPIO_PIN_SET)

static void ST7789_WriteCmd(uint8_t cmd)
{
    LCD_DC_CMD();
    HAL_SPI_Transmit(lcd_spi, &cmd, 1, HAL_MAX_DELAY);
}

static void ST7789_WriteData(uint8_t *data, uint16_t len)
{
    LCD_DC_DATA();
    HAL_SPI_Transmit(lcd_spi, data, len, HAL_MAX_DELAY);
}

static void ST7789_WriteByte(uint8_t data)
{
    ST7789_WriteData(&data, 1);
}

static void ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    x0 += ST7789_X_OFFSET;
    x1 += ST7789_X_OFFSET;
    y0 += ST7789_Y_OFFSET;
    y1 += ST7789_Y_OFFSET;

    /* 设置列地址 */
    ST7789_WriteCmd(0x2A);
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    ST7789_WriteData(data, 4);

    /* 设置行地址 */
    ST7789_WriteCmd(0x2B);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    ST7789_WriteData(data, 4);

    /* 开始写入 GRAM */
    ST7789_WriteCmd(0x2C);
}

void ST7789_Backlight(uint8_t on)
{
    HAL_GPIO_WritePin(LCD_BLK_Port, LCD_BLK_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ST7789_Init(SPI_HandleTypeDef *hspi)
{
    lcd_spi = hspi;

    /* 1. 硬件复位 */
    LCD_RES_HIGH();
    HAL_Delay(50);
    LCD_RES_LOW();
    HAL_Delay(50);
    LCD_RES_HIGH();
    HAL_Delay(120);

    /* 2. 退出睡眠模式 */
    ST7789_WriteCmd(0x11);
    HAL_Delay(120);

    /* 3. 显示方向设置 (0x00正常, 0xC0旋转180度, 0x60/0xA0横屏) */
    ST7789_WriteCmd(0x36);
    ST7789_WriteByte(0x00);

    /* 4. 像素格式: 16bit RGB565 */
    ST7789_WriteCmd(0x3A);
    ST7789_WriteByte(0x55);

    /* 5. 常规电源与伽马初始化 */
    ST7789_WriteCmd(0xB2);
    uint8_t porctrl[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    ST7789_WriteData(porctrl, 5);

    ST7789_WriteCmd(0xB7);
    ST7789_WriteByte(0x35);

    ST7789_WriteCmd(0xBB);
    ST7789_WriteByte(0x19);

    ST7789_WriteCmd(0xC0);
    ST7789_WriteByte(0x2C);

    ST7789_WriteCmd(0xC2);
    ST7789_WriteByte(0x01);

    ST7789_WriteCmd(0xC3);
    ST7789_WriteByte(0x12);

    ST7789_WriteCmd(0xC4);
    ST7789_WriteByte(0x20);

    ST7789_WriteCmd(0xC6);
    ST7789_WriteByte(0x0F);

    ST7789_WriteCmd(0xD0);
    uint8_t pwctrl[] = {0xA4, 0xA1};
    ST7789_WriteData(pwctrl, 2);

    /* 6. 开启反显（IPS 屏通常必须开启 0x21 才能保证颜色正常） */
    ST7789_WriteCmd(0x21);

    /* 7. 正常显示并打开屏幕 */
    ST7789_WriteCmd(0x13);
    ST7789_WriteCmd(0x29);

    ST7789_Backlight(1);
    ST7789_FillScreen(COLOR_BLACK);
}

void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    uint8_t data[2] = { (color >> 8) & 0xFF, color & 0xFF };
    ST7789_SetWindow(x, y, x, y);
    ST7789_WriteData(data, 2);
}

void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (x + w > ST7789_WIDTH)  w = ST7789_WIDTH - x;
    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;

    uint8_t hi = (color >> 8) & 0xFF;
    uint8_t lo = color & 0xFF;

    for (uint16_t i = 0; i < w; i++) {
        line_buf[2 * i]     = hi;
        line_buf[2 * i + 1] = lo;
    }

    ST7789_SetWindow(x, y, x + w - 1, y + h - 1);
    LCD_DC_DATA();
    for (uint16_t row = 0; row < h; row++) {
        HAL_SPI_Transmit(lcd_spi, line_buf, w * 2, HAL_MAX_DELAY);
    }
}

void ST7789_FillScreen(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}