#ifndef __LCD_INIT_H
#define __LCD_INIT_H

#include "sys.h"

#define USE_HORIZONTAL 0 // 锟斤拷锟矫猴拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷示 0锟斤拷1为锟斤拷锟斤拷 2锟斤拷3为锟斤拷锟斤拷

#define LCD_W 240
#define LCD_H 240

//-----------------LCD锟剿口讹拷锟斤拷----------------

#define LCD_SCLK_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_5) // SCL=SCLK
#define LCD_SCLK_Set() GPIO_SetBits(GPIOB, GPIO_Pin_5)

#define LCD_MOSI_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_6) // SDA=MOSI
#define LCD_MOSI_Set() GPIO_SetBits(GPIOB, GPIO_Pin_6)

#define LCD_RES_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_7) // RES
#define LCD_RES_Set() GPIO_SetBits(GPIOB, GPIO_Pin_7)

#define LCD_DC_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_8) // DC
#define LCD_DC_Set() GPIO_SetBits(GPIOB, GPIO_Pin_8)

#define LCD_BLK_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_9) // BLK
#define LCD_BLK_Set() GPIO_SetBits(GPIOB, GPIO_Pin_9)

void LCD_GPIO_Init(void);                             // 锟斤拷始锟斤拷GPIO
void LCD_Writ_Bus(u8 dat);                            // 模锟斤拷SPI时锟斤拷
void LCD_WR_DATA8(u8 dat);                            // 写锟斤拷一锟斤拷锟街斤拷
void LCD_WR_DATA(u16 dat);                            // 写锟斤拷锟斤拷锟斤拷锟街斤拷
void LCD_WR_REG(u8 dat);                              // 写锟斤拷一锟斤拷指锟斤拷
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2); // 锟斤拷锟斤拷锟斤拷锟疥函锟斤拷
void LCD_Init(void);                                  // LCD锟斤拷始锟斤拷
#endif
