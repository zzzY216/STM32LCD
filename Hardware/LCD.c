#include "lcd.h"
#include "lcd_init.h"
#include "delay.h"


/******************************************************************************
      函数说明：在指定区域填充颜色
      入口数据：xsta,ysta   起始坐标
                xend,yend   终止坐标
								color       要填充的颜色
      返回值：  无
******************************************************************************/
void LCD_Fill(u16 xsta,u16 ysta,u16 xend,u16 yend,u16 color)
{          
	u16 i,j; 
	LCD_Address_Set(xsta,ysta,xend-1,yend-1);//设置显示范围
	for(i=ysta;i<yend;i++)
	{													   	 	
		for(j=xsta;j<xend;j++)
		{
			LCD_WR_DATA(color);
		}
	} 					  	    
}

/******************************************************************************
      函数说明：在指定位置画点
      入口数据：x,y 画点坐标
                color 点的颜色
      返回值：  无
******************************************************************************/
void LCD_DrawPoint(u16 x,u16 y,u16 color)
{
	LCD_Address_Set(x,y,x,y);//设置光标位置 
	LCD_WR_DATA(color);
} 


/******************************************************************************
      函数说明：画线
      入口数据：x1,y1   起始坐标
                x2,y2   终止坐标
                color   线的颜色
      返回值：  无
******************************************************************************/
void LCD_DrawLine(u16 x1,u16 y1,u16 x2,u16 y2,u16 color)
{
	u16 t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance;
	int incx,incy,uRow,uCol;
	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1;
	uRow=x1;//画线起点坐标
	uCol=y1;
	if(delta_x>0)incx=1; //设置单步方向 
	else if (delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;}
	if(delta_y>0)incy=1;
	else if (delta_y==0)incy=0;//水平线 
	else {incy=-1;delta_y=-delta_y;}
	if(delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y;
	for(t=0;t<distance+1;t++)
	{
		LCD_DrawPoint(uRow,uCol,color);//画点
		xerr+=delta_x;
		yerr+=delta_y;
		if(xerr>distance)
		{
			xerr-=distance;
			uRow+=incx;
		}
		if(yerr>distance)
		{
			yerr-=distance;
			uCol+=incy;
		}
	}
}


/******************************************************************************
      函数说明：画矩形
      入口数据：x1,y1   起始坐标
                x2,y2   终止坐标
                color   矩形的颜色
      返回值：  无
******************************************************************************/
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2,u16 color)
{
	LCD_DrawLine(x1,y1,x2,y1,color);
	LCD_DrawLine(x1,y1,x1,y2,color);
	LCD_DrawLine(x1,y2,x2,y2,color);
	LCD_DrawLine(x2,y1,x2,y2,color);
}


/******************************************************************************
      函数说明：画圆
      入口数据：x0,y0   圆心坐标
                r       半径
                color   圆的颜色
      返回值：  无
******************************************************************************/
void Draw_Circle(u16 x0,u16 y0,u8 r,u16 color)
{
	int a,b;
	a=0;b=r;	  
	while(a<=b)
	{
		LCD_DrawPoint(x0-b,y0-a,color);             //3           
		LCD_DrawPoint(x0+b,y0-a,color);             //0           
		LCD_DrawPoint(x0-a,y0+b,color);             //1                
		LCD_DrawPoint(x0-a,y0-b,color);             //2             
		LCD_DrawPoint(x0+b,y0+a,color);             //4               
		LCD_DrawPoint(x0+a,y0-b,color);             //5
		LCD_DrawPoint(x0+a,y0+b,color);             //6 
		LCD_DrawPoint(x0-b,y0+a,color);             //7
		a++;
		if((a*a+b*b)>(r*r))//判断要画的点是否过远
		{
			b--;
		}
	}
}

/* ==================================================================
 * 【Flash 优化】字符显示功能已移除
 *
 * 原工程包含 LCD_ShowChar / LCD_ShowString / LCD_ShowChinese* /
 * LCD_ShowIntNum / LCD_ShowFloatNum1 等函数，配套 lcdfont.h 里
 * 约 13KB 的字体表（4 套 ASCII + 多套汉字点阵）。
 *
 * 本项目只显示手机下发的图片（LCD_ShowPicture_Scale）和纯色填充
 * （LCD_Fill），**完全没有用到任何字符/汉字显示**。
 * 因此这些函数与整套字体数据已全部移除，节省约 77KB 源文件、
 * 约 13KB Flash，为 Keil 免费版 32KB 限制腾出空间。
 *
 * 如果以后需要显示文字（比如在图片上叠加状态），有两条路：
 *   1. 从 _backup/lcdfont.h.bak 恢复字体表，并把 LCD.c.bak 里
 *      对应的函数块贴回来；
 *   2. 更省空间的做法：让手机端把文字渲染成图片一起下发，
 *      STM32 只管显示像素，不需要字库。
 *
 * 注意：LCD_ShowPicture / LCD_ShowPicture_Scale 不依赖字体，
 *       仍然保留可用。
 * ================================================================== */

/******************************************************************************
      函数说明：显示图片
      入口数据：x,y起点坐标
                length 图片长度
                width  图片宽度
                pic[]  图片数组    
      返回值：  无
******************************************************************************/
void LCD_ShowPicture(u16 x,u16 y,u16 length,u16 width,const u8 pic[])
{
	u16 i,j;
	u32 k=0;
	LCD_Address_Set(x,y,x+length-1,y+width-1);
	for(i=0;i<length;i++)
	{
		for(j=0;j<width;j++)
		{
			LCD_WR_DATA8(pic[k*2]);
			LCD_WR_DATA8(pic[k*2+1]);
			k++;
		}
	}			
}

// 16色压缩图片放大显示函数
// x,y: 起点坐标; w,h: 图片原始宽高; scale: 放大倍数(2=2倍, 3=3倍)
void LCD_ShowPicture_Scale(u16 x, u16 y, u16 w, u16 h, u8 scale, const u16 palette[16], const u8 pic[])
{
	if (scale == 0 || w == 0 || h == 0) return;
    u16 row, col;
    u8 sy, sx;
    u16 row_bytes = (w + 1) / 2;
    u32 row_offset;
    u8 byte_val, color_idx;
    u16 color;

    LCD_Address_Set(x, y, x + w * scale - 1, y + h * scale - 1);

    for (row = 0; row < h; row++)
    {
        row_offset = (u32)row * row_bytes;
        for (sy = 0; sy < scale; sy++)
        {
            for (col = 0; col < w; col++)
            {
                byte_val = pic[row_offset + (col >> 1)];
                if ((col & 0x01) == 0)
                    color_idx = (byte_val >> 4) & 0x0F;
                else
                    color_idx = byte_val & 0x0F;

                color = palette[color_idx];
                for (sx = 0; sx < scale; sx++)
                {
                    LCD_WR_DATA(color);
                }
            }
        }
    }
}

/* ==================================================================
 * 以下为 ESP32 串口通讯模块新增功能
 * 说明：全部是"新增"，不修改上面任何已有函数的行为，
 *       原有 GIF 播放功能不受影响。
 * ================================================================== */

/** 当前缩放倍数（预留给上层查询） */
static u8 s_lcd_scale = 1;

/** 背光 PWM 状态 */
static u8 s_blk_duty    = 100;   /* 0~100 */
static u8 s_blk_counter = 0;     /* 0~99 */

void LCD_SetScale(u8 scale)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    s_lcd_scale = scale;
}

u8 LCD_GetScale(void)
{
    return s_lcd_scale;
}

/**
 * @brief 设置背光亮度。
 * @param percent 0~100
 * @note  只是记录目标值，实际 PWM 由 LCD_BrightnessTick 输出。
 *        之所以做软件 PWM：原工程 BLK 接在 PB9，是普通 GPIO，
 *        没有接定时器通道，改动硬件代价大。
 */
void LCD_SetBrightness(u8 percent)
{
    if (percent > 100) percent = 100;
    s_blk_duty = percent;

    /* 0% 或 100% 时直接给定电平，避免 PWM 抖动 */
    if (s_blk_duty == 0) {
        LCD_BLK_Clr();
    } else if (s_blk_duty >= 100) {
        LCD_BLK_Set();
    }
}

/**
 * @brief 背光 PWM 心跳。
 * @details 每调用 100 次构成一个 PWM 周期。
 *          必须在主循环里尽可能频繁地调用，否则会看到闪烁。
 *          调得越频繁，PWM 频率越高，闪烁越少。
 */
void LCD_BrightnessTick(void)
{
    if (s_blk_duty == 0) {
        LCD_BLK_Clr();
        return;
    }
    if (s_blk_duty >= 100) {
        LCD_BLK_Set();
        return;
    }

    s_blk_counter++;
    if (s_blk_counter >= 100) {
        s_blk_counter = 0;
    }

    if (s_blk_counter < s_blk_duty) {
        LCD_BLK_Set();
    } else {
        LCD_BLK_Clr();
    }
}
