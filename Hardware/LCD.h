#ifndef __LCD_H
#define __LCD_H		
#include "sys.h"


/* ==================================================================
 * 【Flash 优化】字符/汉字显示接口已移除
 * 本工程只显示图片（LCD_ShowPicture_Scale）和纯色（LCD_Fill），
 * 不需要字库。原 lcdfont.h 约 13KB 字体数据已移除。
 * 如需恢复，参考 _backup/ 目录下的备份文件。
 * ================================================================== */

void LCD_Fill(u16 xsta,u16 ysta,u16 xend,u16 yend,u16 color);//指定区域填充颜色
void LCD_DrawPoint(u16 x,u16 y,u16 color);//在指定位置画一个点
void LCD_DrawLine(u16 x1,u16 y1,u16 x2,u16 y2,u16 color);//在指定位置画一条线
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2,u16 color);//在指定位置画一个矩形
void Draw_Circle(u16 x0,u16 y0,u8 r,u16 color);//在指定位置画一个圆



void LCD_ShowPicture(u16 x,u16 y,u16 length,u16 width,const u8 pic[]);//显示图片

void LCD_ShowPicture_Scale(u16 x, u16 y, u16 w, u16 h, u8 scale, const u16 palette[16], const u8 pic[]); // 放大显示函数

/* 任意尺寸拉伸显示：把 w*h 的 16 色压缩图拉伸到 dst_w*dst_h 画出。
 * 用于让内置 GIF（64x64）铺满 240x240 屏幕，消除四周黑边。
 * 与 LCD_ShowPicture_Scale 的区别：后者只支持整数倍放大（1/2/3/4），
 * 64x64 用 3 倍只有 192x192，四周会各留 24 像素黑边。 */
void LCD_ShowPicture_Stretch(u16 x, u16 y, u16 w, u16 h,
                             u16 dst_w, u16 dst_h,
                             const u16 palette[16], const u8 pic[]);

/* ===== 以下为 ESP32 串口通讯模块新增接口（不改变上面任何已有函数） ===== */

/**
 * @brief 设置图片显示缩放倍数。
 * @param scale 1~4，超出范围会被钳位
 * @note  只影响后续 LCD_ShowPicture_Scale 与 img_rx 模块，不改动已有行为。
 */
void LCD_SetScale(u8 scale);

/**
 * @brief 读取当前缩放倍数。
 */
u8 LCD_GetScale(void);

/**
 * @brief 设置背光亮度（软件 PWM）。
 * @param percent 0~100，0 为全灭，100 为全亮
 * @note  需要主循环周期性调用 LCD_BrightnessTick() 才能维持 PWM。
 */
void LCD_SetBrightness(u8 percent);

/**
 * @brief 背光 PWM 心跳，必须在主循环里反复调用。
 */
void LCD_BrightnessTick(void);

//画笔颜色
#define WHITE         	 0xFFFF
#define BLACK         	 0x0000	  
#define BLUE           	 0x001F  
#define BRED             0XF81F
#define GRED 			       0XFFE0
#define GBLUE			       0X07FF
#define RED           	 0xF800
#define MAGENTA       	 0xF81F
#define GREEN         	 0x07E0
#define CYAN          	 0x7FFF
#define YELLOW        	 0xFFE0
#define BROWN 			     0XBC40 //棕色
#define BRRED 			     0XFC07 //棕红色
#define GRAY  			     0X8430 //灰色
#define DARKBLUE      	 0X01CF	//深蓝色
#define LIGHTBLUE      	 0X7D7C	//浅蓝色  
#define GRAYBLUE       	 0X5458 //灰蓝色
#define LIGHTGREEN     	 0X841F //浅绿色
#define LGRAY 			     0XC618 //浅灰色(PANNEL),窗体背景色
#define LGRAYBLUE        0XA651 //浅灰蓝色(中间层颜色)
#define LBBLUE           0X2B12 //浅棕蓝色(选择条目的反色)

#endif





