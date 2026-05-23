/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP_DMA_LCM.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_DMA.h"
#include	"STC32G_NVIC.h"
#include	"STC32G_LCM.h"
#include	"STC32G_Delay.h"
#include	"font.h"

/*************	功能说明	**************

LCM接口+DMA驱动液晶屏程序

8bit I8080模式, P6口接数据口

sbit LCD_RS = P4^5;      //数据/命令切换
sbit LCD_WR = P4^2;      //写控制
sbit LCD_RD = P4^4;      //读控制
sbit LCD_CS = P3^4;      //片选
sbit LCD_RESET = P4^3;   //复位

LCM指令通过中断方式等待发送完成

DMA设置长度2048字节，通过中断方式判断传输完成

下载时, 选择时钟 24MHz (可以在配置文件"config.h"中修改).

******************************************/


//========================================================================
//                               本地常量声明	
//========================================================================

//支持横竖屏快速定义切换
#define USE_HORIZONTAL  	  0   //定义液晶屏顺时针旋转方向 	0-0度旋转，1-90度旋转，2-180度旋转，3-270度旋转

//画笔颜色
#define WHITE         	 0xFFFF
#define BLACK         	 0x0000	  
#define BLUE             0x001F  
#define BRED             0XF81F
#define GRED             0XFFE0
#define GBLUE            0X07FF
#define RED           	 0xF800
#define MAGENTA       	 0xF81F
#define GREEN         	 0x07E0
#define CYAN          	 0x7FFF
#define YELLOW        	 0xFFE0
#define BROWN            0XBC40 //棕色
#define BRRED            0XFC07 //棕红色
#define GRAY             0X8430 //灰色

#define DMA_AMT_LEN  2047  //n+1, 不要超过芯片 xdata 空间上限

//========================================================================
//                               本地变量声明
//========================================================================

//定义LCD的尺寸
#define LCD_W 240			//LCD 宽度
#define LCD_H 320			//LCD 高度

u16 POINT_COLOR=0x0000;	//画笔颜色

u16 xdata Buffer[8]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
u16 xdata Color[DMA_AMT_LEN+1];

typedef struct  
{										    
	u16 width;			//LCD 宽度
	u16 height;			//LCD 高度
	u16 id;				//LCD ID
	u8  dir;			//横屏还是竖屏控制：0，竖屏；1，横屏。	
	u8 wramcmd;		//开始写gram指令
	u8 rramcmd;   //开始读gram指令
	u8 setxcmd;		//设置x坐标指令
	u8 setycmd;		//设置y坐标指令	 
}_lcd_dev; 	

_lcd_dev lcddev;

//========================================================================
//                               本地函数声明
//========================================================================

void Test_Color(void);
void LCD_WR_DATA_16Bit(u16 Data);
void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd);
void Show_Str(u16 x, u16 y, u16 fc, u16 bc, u8 *str,u8 size,u8 mode);
void LCD_Init(void);

//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: DMA_LCM_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void DMA_LCM_init(void)
{
	LCM_InitTypeDef		LCM_InitStructure;		//结构定义
	DMA_LCM_InitTypeDef		DMA_LCM_InitStructure;		//结构定义

	//----------------------------------------------
	P6_MODE_OUT_PP(GPIO_Pin_All);		//P6 设置成推挽输出
	P3_MODE_OUT_PP(GPIO_Pin_4);			//P3.4口设置成推挽输出
	P4_MODE_OUT_PP(GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);	//P4.2~P4.5 设置成推挽输出
	
	//----------------------------------------------
	LCM_InitStructure.LCM_Enable = ENABLE;			//LCM接口使能  	ENABLE,DISABLE
	LCM_InitStructure.LCM_Mode = MODE_I8080;		//LCM接口模式  	MODE_I8080,MODE_M6800
	LCM_InitStructure.LCM_Bit_Wide = BIT_WIDE_8;	//LCM数据宽度  	BIT_WIDE_8,BIT_WIDE_16
	LCM_InitStructure.LCM_Setup_Time = 2;			//LCM通信数据建立时间  	0~7
	LCM_InitStructure.LCM_Hold_Time = 1;			//LCM通信数据保持时间  	0~3
	LCM_Inilize(&LCM_InitStructure);		//初始化
	NVIC_LCM_Init(ENABLE,Priority_0);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	//----------------------------------------------
	DMA_LCM_InitStructure.DMA_Enable = ENABLE;			//DMA使能  	ENABLE,DISABLE
	DMA_LCM_InitStructure.DMA_Length = DMA_AMT_LEN;	    //DMA传输总字节数  	(0~65535) + 1, 不要超过芯片 xdata 空间上限
	DMA_LCM_InitStructure.DMA_Tx_Buffer = (u16)Color;	//发送数据存储地址
	DMA_LCM_InitStructure.DMA_Rx_Buffer = (u16)Buffer;	//接收数据存储地址
	DMA_LCM_Inilize(&DMA_LCM_InitStructure);		//初始化
	NVIC_DMA_LCM_Init(ENABLE,Priority_0,Priority_0);	//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0~Priority_3; 总线优先级(低到高) Priority_0~Priority_3

	LCD_Init();
}

//========================================================================
// 函数: Sample_DMA_LCM
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-24
//========================================================================
void Sample_DMA_LCM(void)
{
	Test_Color();
}

void LCD_Fill(u16 sx,u16 sy,u16 ex,u16 ey,u16 color)
{  	
	u16 i,j;			
	u16 width=ex-sx+1; 		//得到填充的宽度
	u16 height=ey-sy+1;		//高度
	LCD_SetWindows(sx,sy,ex,ey);//设置显示窗口

	for(j=0,i=0;i<=DMA_AMT_LEN;i++)
	{
		Color[i] = color;
	}
	LCM_Cnt = 75;     //(320 * 240 * 2) / 2048 = 75
	LCD_CS=0;
	DMA_LCM_TRIG_WD();	//Write dat
	while(!LCD_CS);
}

void Test_Color(void)
{
	LCD_Fill(0,0,lcddev.width,lcddev.height,WHITE);
	Show_Str(20,30,BLUE,YELLOW,"LCM Test",16,1);delay_ms(800);
	LCD_Fill(0,0,lcddev.width,lcddev.height,RED);
	Show_Str(20,30,BLUE,YELLOW,"RED ",16,1);delay_ms(800);
	LCD_Fill(0,0,lcddev.width,lcddev.height,GREEN);
	Show_Str(20,30,BLUE,YELLOW,"GREEN ",16,1);delay_ms(800);
	LCD_Fill(0,0,lcddev.width,lcddev.height,BLUE);
	Show_Str(20,30,RED,YELLOW,"BLUE ",16,1);delay_ms(800);
}

/*****************************************************************************
 * @name       :void LCD_WR_REG(u8 Reg)	
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :data:Command value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_REG(u8 Reg)	 
{
	LCMIFDATL = Reg;
	LCD_CS=0;
	LCMIFCR = 0x84;		//Enable interface, write command out
	while(LcmFlag);
	LCD_CS = 1 ;
} 

/*****************************************************************************
 * @name       :void LCD_WR_DATA(u8 Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit data to the LCD screen
 * @parameters :data:data value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_DATA(u8 Data)
{
	LCMIFDATL = Data;
	LCD_CS=0;
	LCMIFCR = 0x85;		//Enable interface, write data out
	while(LcmFlag);
	LCD_CS = 1 ;
}

/*****************************************************************************
 * @name       :void LCD_WR_DATA_16Bit(u16 Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :Data:Data to be written
 * @retvalue   :None
******************************************************************************/	 
void LCD_WR_DATA_16Bit(u16 Data)
{
	LCD_WR_DATA((u8)(Data>>8));
	LCD_WR_DATA((u8)Data);
}

/*****************************************************************************
 * @name       :void LCD_WriteReg(u8 LCD_Reg, u8 LCD_RegValue)
 * @date       :2018-08-09 
 * @function   :Write data into registers
 * @parameters :LCD_Reg:Register address
                LCD_RegValue:Data to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WriteReg(u8 LCD_Reg, u8 LCD_RegValue)
{
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

/*****************************************************************************
 * @name       :void LCD_WriteRAM_Prepare(void)
 * @date       :2018-08-09 
 * @function   :Write GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_WriteRAM_Prepare(void)
{
 	LCD_WR_REG(lcddev.wramcmd);	  
}

/*****************************************************************************
 * @name       :void LCD_DrawPoint(u16 x,u16 y)
 * @date       :2018-08-09 
 * @function   :Write a pixel data at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/	
void LCD_DrawPoint(u16 x,u16 y)
{
	LCD_SetWindows(x,y,x,y);//设置光标位置 
	LCD_WR_DATA_16Bit(POINT_COLOR); 	    
} 	 

/*****************************************************************************
 * @name       :void LCDReset(void)
 * @date       :2018-08-09 
 * @function   :Reset LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCDReset(void)
{
	LCD_CS=1;
	delay_ms(50);	
	LCD_RESET=0;
	delay_ms(150);
	LCD_RESET=1;
	delay_ms(50);
}

/*****************************************************************************
 * @name       :void LCD_direction(u8 direction)
 * @date       :2018-08-09 
 * @function   :Setting the display direction of LCD screen
 * @parameters :direction:0-0 degree
                          1-90 degree
													2-180 degree
													3-270 degree
 * @retvalue   :None
******************************************************************************/ 
void LCD_direction(u8 direction)
{ 
    lcddev.setxcmd=0x2A;
    lcddev.setycmd=0x2B;
    lcddev.wramcmd=0x2C;
    lcddev.rramcmd=0x2E;
	switch(direction){
		case 0:
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;
			LCD_WriteReg(0x36,(1<<3));
		break;
		case 1:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<3)|(1<<7)|(1<<5)|(1<<4));
		break;
		case 2:
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;	
			LCD_WriteReg(0x36,(1<<3)|(1<<4)|(1<<6)|(1<<7));
		break;
		case 3:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<3)|(1<<5)|(1<<6));
		break;	
		default:break;
	}
}

/*****************************************************************************
 * @name       :void LCD_Init(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 	 
void LCD_Init(void)
{
	LCDReset(); //初始化之前复位
//	delay_ms(150);                     //根据不同晶振速度可以调整延时，保障稳定显示
//*************2.4inch ILI9341初始化**********//	
	LCD_WR_REG(0xCF);  
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0xD9); //0xC1 
	LCD_WR_DATA(0X30); 
	LCD_WR_REG(0xED);  
	LCD_WR_DATA(0x64); 
	LCD_WR_DATA(0x03); 
	LCD_WR_DATA(0X12); 
	LCD_WR_DATA(0X81); 
	LCD_WR_REG(0xE8);  
	LCD_WR_DATA(0x85); 
	LCD_WR_DATA(0x10); 
	LCD_WR_DATA(0x7A); 
	LCD_WR_REG(0xCB);  
	LCD_WR_DATA(0x39); 
	LCD_WR_DATA(0x2C); 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x34); 
	LCD_WR_DATA(0x02); 
	LCD_WR_REG(0xF7);  
	LCD_WR_DATA(0x20); 
	LCD_WR_REG(0xEA);  
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x00); 
	LCD_WR_REG(0xC0);    //Power control 
	LCD_WR_DATA(0x1B);   //VRH[5:0] 
	LCD_WR_REG(0xC1);    //Power control 
	LCD_WR_DATA(0x12);   //SAP[2:0];BT[3:0] 0x01
	LCD_WR_REG(0xC5);    //VCM control 
	LCD_WR_DATA(0x08); 	 //30
	LCD_WR_DATA(0x26); 	 //30
	LCD_WR_REG(0xC7);    //VCM control2 
	LCD_WR_DATA(0XB7); 
	LCD_WR_REG(0x36);    // Memory Access Control 
	LCD_WR_DATA(0x08);
	LCD_WR_REG(0x3A);   
	LCD_WR_DATA(0x55); 
	LCD_WR_REG(0xB1);   
	LCD_WR_DATA(0x00);   
	LCD_WR_DATA(0x1A); 
	LCD_WR_REG(0xB6);    // Display Function Control 
	LCD_WR_DATA(0x0A); 
	LCD_WR_DATA(0xA2); 
	LCD_WR_REG(0xF2);    // 3Gamma Function Disable 
	LCD_WR_DATA(0x00); 
	LCD_WR_REG(0x26);    //Gamma curve selected 
	LCD_WR_DATA(0x01); 
	LCD_WR_REG(0xE0);    //Set Gamma 
	LCD_WR_DATA(0x0F); 
	LCD_WR_DATA(0x1D); 
	LCD_WR_DATA(0x1A); 
	LCD_WR_DATA(0x0A); 
	LCD_WR_DATA(0x0D); 
	LCD_WR_DATA(0x07); 
	LCD_WR_DATA(0x49); 
	LCD_WR_DATA(0X66); 
	LCD_WR_DATA(0x3B); 
	LCD_WR_DATA(0x07); 
	LCD_WR_DATA(0x11); 
	LCD_WR_DATA(0x01); 
	LCD_WR_DATA(0x09); 
	LCD_WR_DATA(0x05); 
	LCD_WR_DATA(0x04); 		 
	LCD_WR_REG(0XE1);    //Set Gamma 
	LCD_WR_DATA(0x00); 
	LCD_WR_DATA(0x18); 
	LCD_WR_DATA(0x1D); 
	LCD_WR_DATA(0x02); 
	LCD_WR_DATA(0x0F); 
	LCD_WR_DATA(0x04); 
	LCD_WR_DATA(0x36); 
	LCD_WR_DATA(0x13); 
	LCD_WR_DATA(0x4C); 
	LCD_WR_DATA(0x07); 
	LCD_WR_DATA(0x13); 
	LCD_WR_DATA(0x0F); 
	LCD_WR_DATA(0x2E); 
	LCD_WR_DATA(0x2F); 
	LCD_WR_DATA(0x05); 
	LCD_WR_REG(0x2B); 
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x01);
	LCD_WR_DATA(0x3f);
	LCD_WR_REG(0x2A); 
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0xef);	 
	LCD_WR_REG(0x11); //Exit Sleep
	delay_ms(120);
	LCD_WR_REG(0x29); //display on	

	//设置LCD属性参数
	LCD_direction(USE_HORIZONTAL);//设置LCD显示方向 
}

void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
{	
	LCD_WR_REG(lcddev.setxcmd);	
	LCD_WR_DATA((u8)(xStar>>8));
	LCD_WR_DATA(0x00FF&xStar);		
	LCD_WR_DATA((u8)(xEnd>>8));
	LCD_WR_DATA(0x00FF&xEnd);

	LCD_WR_REG(lcddev.setycmd);	
	LCD_WR_DATA((u8)(yStar>>8));
	LCD_WR_DATA(0x00FF&yStar);		
	LCD_WR_DATA((u8)(yEnd>>8));
	LCD_WR_DATA(0x00FF&yEnd);	

	LCD_WriteRAM_Prepare();	//开始写入GRAM				
}

/*****************************************************************************
 * @name       :void LCD_ShowChar(u16 x,u16 y,u16 fc, u16 bc, u8 num,u8 size,u8 mode)
 * @date       :2018-08-09 
 * @function   :Display a single English character
 * @parameters :x:the beginning x coordinate of the Character display position
                y:the beginning y coordinate of the Character display position
								fc:the color value of display character
								bc:the background color of display character
								num:the ascii code of display character(0~94)
								size:the size of display character
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/ 
void LCD_ShowChar(u16 x,u16 y,u16 fc, u16 bc, u8 num,u8 size,u8 mode)
{
	u8 temp;
	u8 pos,t;
	u16 colortemp=POINT_COLOR;

	num=num-' ';//得到偏移后的值
	LCD_SetWindows(x,y,x+size/2-1,y+size-1);//设置单个文字显示窗口
	if(!mode) //非叠加方式
	{
		for(pos=0;pos<size;pos++)
		{
			if(size==12)temp=asc2_1206[num][pos];//调用1206字体
			else temp=asc2_1608[num][pos];		 //调用1608字体
			for(t=0;t<size/2;t++)
			{
				if(temp&0x01)LCD_WR_DATA_16Bit(fc); 
				else LCD_WR_DATA_16Bit(bc); 
				temp>>=1; 
			}
		}
	}
	else//叠加方式
	{
		for(pos=0;pos<size;pos++)
		{
			if(size==12)temp=asc2_1206[num][pos];//调用1206字体
			else temp=asc2_1608[num][pos];		 //调用1608字体
			for(t=0;t<size/2;t++)
			{
				POINT_COLOR=fc;
				if(temp&0x01)	LCD_DrawPoint(x+t,y+pos);//画一个点
				temp>>=1;
			}
		}
	}
	POINT_COLOR=colortemp;	
	LCD_SetWindows(0,0,LCD_W-1,LCD_H-1);//恢复窗口为全屏    	   	 	  
}

/*****************************************************************************
 * @name       :void Show_Str(u16 x, u16 y, u16 fc, u16 bc, u8 *str,u8 size,u8 mode)
 * @date       :2018-08-09 
 * @function   :Display Chinese and English strings
 * @parameters :x:the beginning x coordinate of the Chinese and English strings
                y:the beginning y coordinate of the Chinese and English strings
								fc:the color value of Chinese and English strings
								bc:the background color of Chinese and English strings
								str:the start address of the Chinese and English strings
								size:the size of Chinese and English strings
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/	   		   
void Show_Str(u16 x, u16 y, u16 fc, u16 bc, u8 *str,u8 size,u8 mode)
{					
	u16 x0=x;							  	  
	u8 bHz=0;     //字符或者中文 
	while(*str!=0)//数据未结束
	{ 
		if(!bHz)
		{
			if(x>(LCD_W-size/2)||y>(LCD_H-size)) 
			return;
			if(*str>0x80)	bHz=1;//中文 
			else              //字符
			{
				if(*str==0x0D)//换行符号
				{
					y+=size;
					x=x0;
					str++;
				}
				else
				{
					if(size>16)//字库中没有集成12X24 16X32的英文字体,用8X16代替
					{  
						LCD_ShowChar(x,y,fc,bc,*str,16,mode);
						x+=8; //字符,为全字的一半 
					}
					else
					{
						LCD_ShowChar(x,y,fc,bc,*str,size,mode);
						x+=size/2; //字符,为全字的一半 
					}
				}
				str++;
			}
		}
		else//中文
		{
//			if(x>(lcddev.width-size)||y>(lcddev.height-size))
//			return;
//			bHz=0;//有汉字库
//			if(size==32)
//			GUI_DrawFont32(x,y,fc,bc,str,mode);
//			else if(size==24)
//			GUI_DrawFont24(x,y,fc,bc,str,mode);
//			else
//			GUI_DrawFont16(x,y,fc,bc,str,mode);

			str+=2;
			x+=size;//下一个汉字偏移
		}
	}
}

