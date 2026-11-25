#ifndef __LCD_H
#define __LCD_H
#include "sys.h"
void LCD_WR_DATA(u16 color);
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2);
#define LCD_W  240
#define LCD_H  240
#define BLACK  0x0000
#endif
