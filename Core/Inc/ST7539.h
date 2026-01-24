/*
 * ST7539.h
 *
 *  Created on: Jan 16, 2026
 *      Author: anh.tqt
 */

#ifndef INC_ST7539_H_
#define INC_ST7539_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include "ST7539_font.h"

//typedef struct {
//  char c;
//  uint8_t width;   // in columns
//  uint8_t b[32];   // 16+16 columns data (we can still store 16 wide max)
//} Glyph16W;


#define ST7539_SA1              (0u)
#define ST7539_SA0              (0u)

//#define COLUMN_OFFSET 			0X40
#define COLUMN_OFFSET 			0X00
/* ---------- 7-bit base prefix ---------- */
#define ST7539_DATA_PREFIX_7     (0x3Fu)   /* 0b0111111 _WR */
#define ST7539_CMD_PREFIX_7     (0x3Eu)   /* 0b0111110 _WR */

/* ---------- build 7-bit addresses from SA1/SA0 ---------- */
#define ST7539_I2C_CMD7(ST7539_SA1, ST7539_SA0)   ((uint8_t)(ST7539_I2C_PREFIX_7 | (0 << 2) | (0 << 1) | 0u))
#define ST7539_I2C_DATA7(ST7539_SA1, ST7539_SA0)   ((uint8_t)(ST7539_I2C_PREFIX_7 | (0 << 2) | (0 << 1) | 1u))
#define CMD7 0x38
#define DATA7 0x39
#define CMD8 0x70
#define DATA8 0x72


#define W_DIGIT  16
#define W_SPACE   4   // set to your actual space width
#define W_COLON   2
/* ---------- convert 7-bit to STM32 HAL 8-bit write address ---------- */
#define ST7539_I2C_ADDR8W(addr7)    ((uint16_t)((uint16_t)(addr7) << 1))   /* W=0 */


typedef struct {
  uint8_t found;
  uint8_t cmd8w;   // 8-bit bus address byte for CMD WRITE (LSB=0)
  uint8_t data8w;  // 8-bit bus address byte for DATA WRITE (LSB=0)
  uint8_t sa1;
  uint8_t sa0;
} ST7539ScanResult8;

void ST7539_HWReset(void);
void ST7539_SetSA0_Pin(uint8_t level);
void ST7539_TestDisplay(I2C_HandleTypeDef *hi2c, uint8_t cmd7, uint8_t data7);
void ST7539_InitBasic(I2C_HandleTypeDef *hi2c);
void ST7539_Clear(I2C_HandleTypeDef *hi2c);
void ST7539_DrawText5x7(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, const char *s);
void LCD_SetPage0Col0(I2C_HandleTypeDef *hi2c, uint16_t CMD8W);
void LCD_SetCol(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t col);
void LCD_SetPage0Col0(I2C_HandleTypeDef *hi2c, uint16_t CMD8W);
HAL_StatusTypeDef LCD_Data(I2C_HandleTypeDef *hi2c, uint8_t data8w, const uint8_t *buf, uint16_t len);
//void I2C_Scan(I2C_HandleTypeDef *hi2c);
ST7539ScanResult8 ST7539_FindAddressPair(I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef ST7539_WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef ST7539_WriteData(I2C_HandleTypeDef *hi2c, uint8_t data8w, const uint8_t *buf, uint16_t len);


/*2 Page font clock. Input follow this "h h : m m : s s*/
void Clock_Draw(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col,
                           uint8_t hh, uint8_t mm, uint8_t ss);
void ST7539_EraseClockDigit(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t base_col, uint8_t digit_index);



#endif /* INC_ST7539_H_ */
