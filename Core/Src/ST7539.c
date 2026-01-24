/*
 * ST7539.c
 *
 *  Created on: Jan 16, 2026
 *      Author: anh.tqt
 */
#include "ST7539.h"
#include <string.h>
#include "main.h"


// ---------- Minimal 5x7 font for "HELLO " ----------
static const uint8_t FONT5x7_SPACE[5] = {0x00,0x00,0x00,0x00,0x00};
static const uint8_t FONT5x7_H[5]     = {0x7F,0x08,0x08,0x08,0x7F};
static const uint8_t FONT5x7_E[5]     = {0x7F,0x49,0x49,0x49,0x41};
static const uint8_t FONT5x7_L[5]     = {0x7F,0x40,0x40,0x40,0x40};
static const uint8_t FONT5x7_O[5]     = {0x3E,0x41,0x41,0x41,0x3E};

static const uint8_t* font5x7_get(char c) {
  switch (c) {
    case 'H': return FONT5x7_H;
    case 'E': return FONT5x7_E;
    case 'L': return FONT5x7_L;
    case 'O': return FONT5x7_O;
    case ' ': return FONT5x7_SPACE;
    default:  return FONT5x7_SPACE;
  }
}
// ST7539 I2C slave control byte on bus (8-bit):
// [7:0] = 0 1 1 1 SA1 SA0 A0 W/R

//A0 = 1 → payload bytes are display data → stored into display RAM, and internal pointer auto-increments.
//A0 = 0 → payload bytes are commands → decoded to change settings.
//W  = 0 -> Write
//SA0 = 0 SA1 = 1
// -------- Reset / strap pins (adjust if needed) --------
void ST7539_HWReset(void) {
  HAL_GPIO_WritePin(RST_GPIO_Port, RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(RST_GPIO_Port, RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);
}

// If CS0 is your SA0 strap pin in I2C mode, DO NOT toggle it during runtime.
// Strap it once (GPIO output fixed high/low) or via resistor/jumper.
void ST7539_SetSA0_Pin(uint8_t level) {
  HAL_GPIO_WritePin(CS0_GPIO_Port, CS0_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// -------- I2C probe using 8-bit address byte --------
static HAL_StatusTypeDef I2C_Ack8(I2C_HandleTypeDef *hi2c, uint8_t addr8_write) {
  // HAL expects addr8_write already (LSB=0)
  return HAL_I2C_IsDeviceReady(hi2c, addr8_write<<1, 2, 50);
//	return HAL_I2C_Master_Transmit(hi2c, addr8_write<<1,addr8_write<<1, 2, 50);
}
static HAL_StatusTypeDef LCD_Cmd(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t c) {
  return HAL_I2C_Master_Transmit(hi2c, cmd8w, &c, 1, 100);
}

static HAL_StatusTypeDef LCD_Cmd2(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t c1, uint8_t c2) {
  uint8_t b[2] = {c1, c2};
  return HAL_I2C_Master_Transmit(hi2c, cmd8w, b, 2, 100);
}

HAL_StatusTypeDef LCD_Data(I2C_HandleTypeDef *hi2c, uint8_t data8w, const uint8_t *buf, uint16_t len) {
  return HAL_I2C_Master_Transmit(hi2c, data8w, (uint8_t*)buf, len, 400);
}

void LCD_SetPage(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t page) {
  LCD_Cmd(hi2c, cmd8w, 0xB0 | (page & 0x0F));
}

void LCD_SetCol(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t col) {
  LCD_Cmd(hi2c, cmd8w, 0x10 | ((col >> 4) & 0x0F));
  LCD_Cmd(hi2c, cmd8w, 0x00 | (col & 0x0F));
}
void LCD_SetPage0Col0(I2C_HandleTypeDef *hi2c, uint16_t CMD8W) {
  LCD_Cmd(hi2c, CMD8W, 0xB0); // page 0
  LCD_Cmd(hi2c, CMD8W, 0x10); // col MSB = 0
  LCD_Cmd(hi2c, CMD8W, COLUMN_OFFSET); // col LSB = 0
}


// -------- Scan and decode SA0/SA1 using 8-bit bus addresses --------
ST7539ScanResult8 ST7539_FindAddressPair(I2C_HandleTypeDef *hi2c) {
	ST7539ScanResult8 r = {0};

  for (uint8_t cmd = 0x38; cmd <= 0x3E; cmd += 2) {
    uint8_t data = cmd + 1;
    HAL_StatusTypeDef cmd1 = I2C_Ack8(hi2c, cmd);
    HAL_StatusTypeDef data1 = I2C_Ack8(hi2c, data);
    if (cmd1 ==HAL_OK  && data1==HAL_OK){
      r.found = 1;
	  r.cmd8w = cmd1;
	  r.data8w = data1;

	  // cmdAddr = 0b0111 SA1 SA0 0
	  r.sa1 = (cmd1 >> 2) & 0x01;
	  r.sa0 = (cmd1 >> 1) & 0x01;
	  return r;
    }
    else{
    	printf("0x%02X=%d  0x%02X=%d  err=0x%08lX\r\n",
    			cmd, cmd1, cmd+1, data1, (unsigned long)HAL_I2C_GetError(hi2c));
//    	return r;
    }
//    if (I2C_Ack8(hi2c, cmd) ==HAL_OK  && I2C_Ack8(hi2c, data)==HAL_OK) {
//      r.found = 1;
//      r.cmd8w = cmd;
//      r.data8w = data;
//
//      // cmdAddr = 0b0111 SA1 SA0 0
//      r.sa1 = (cmd >> 2) & 0x01;
//      r.sa0 = (cmd >> 1) & 0x01;
//      return r;
//    }
  }

  return r;
}
//void I2C_Scan(I2C_HandleTypeDef *hi2c)
//{
//    printf("Scanning I2C bus...\r\n");
//
//    for (uint8_t addr7 = 1; addr7 < 128; addr7++)
//    {
//        HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(hi2c, addr7 << 1, 2, 40);
//
//        if (st == HAL_OK) {
//            printf("ACK at 0x%02X\r\n", addr7);
//        } else {
//            uint32_t err = HAL_I2C_GetError(hi2c);
//            if (err & HAL_I2C_ERROR_AF) {
//                // normal NACK, ignore
//            	 printf("normal NACK, ignore\r\n");
//            } else {
//                printf("0x%02X err=0x%08lX\r\n", addr7, (unsigned long)err);
//            }
//        }
//    }
//}

// ---------- Basic init (good enough for text) ----------
void ST7539_InitBasic(I2C_HandleTypeDef *hi2c) {
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7    << 1); // 0x7C
  uint16_t DATA8W  = (uint16_t)(ST7539_DATA_PREFIX_7    << 1); // 0x7D

  ST7539_HWReset();
  HAL_Delay(100);

  LCD_Cmd(hi2c, CMD8W, 0xE2);      // SW reset
  HAL_Delay(10);
  LCD_Cmd(hi2c, CMD8W, 0xAE);      // display off
  LCD_Cmd(hi2c, CMD8W, 0xA6);      // normal
  LCD_Cmd(hi2c, CMD8W, 0x40);      // start line = 0
  LCD_Cmd(hi2c, CMD8W, 0xA0);      // frame rate
  LCD_Cmd(hi2c, CMD8W, 0xA6);      // inverse display
  LCD_Cmd(hi2c, CMD8W, 0xEB);      // bias
  LCD_Cmd(hi2c, CMD8W, 0xC2);      // scan direction (try C8 if upside down)

//  LCD_Cmd2(hi2c, CMD8W, 0x81, 0x60); // contrast (tune 0x20..0xA0)
//  LCD_Cmd2(hi2c, CMD8W, 0x81, 0x90); // stronger
  LCD_Cmd(hi2c, CMD8W, 0x81);
  LCD_Cmd(hi2c, CMD8W, 0x99);


  // Set page/col = 0
  LCD_SetPage0Col0(hi2c, CMD8W);

  // ---- Write data into DDRAM (TEST: full white 128x32) ----
  uint8_t line1[128];
  uint8_t line2[128];
  memset(line1, 0xFF, sizeof(line1)); // all pixels ON in each page
  memset(line2, 0x00, sizeof(line2)); // all pixels Off in each page

  for (uint8_t page = 0; page < 4; page++) {
	if(page%2==0){
		LCD_SetPage(hi2c, CMD8W, page);
		LCD_SetCol(hi2c, CMD8W, COLUMN_OFFSET);
		LCD_Data(hi2c, DATA8W, line1, 128);
	}
	else{
		LCD_SetPage(hi2c, CMD8W, page);
		LCD_SetCol(hi2c, CMD8W, COLUMN_OFFSET);
		LCD_Data(hi2c, DATA8W, line2, 128);
	}

  }

  LCD_Cmd(hi2c, CMD8W, 0xAF);   // Display On
  HAL_Delay(100);
}
void ST7539_TestDisplay(I2C_HandleTypeDef *hi2c, uint8_t cmd7, uint8_t data7) {
  uint8_t CMD8W  = (uint8_t)(cmd7  << 1); // 0x70
  uint8_t DATA8W = (uint8_t)(data7 << 1); // 0x72

  // Reset + wait
    ST7539_HWReset();
    HAL_Delay(100);

    // ---- Recommended init sequence ----
    LCD_Cmd(hi2c, CMD8W, 0xE2);   // Software Reset
    LCD_Cmd(hi2c, CMD8W, 0xA0);   // Frame Rate 76 fps
    LCD_Cmd(hi2c, CMD8W, 0xEB);   // Bias 1/9
    LCD_Cmd(hi2c, CMD8W, 0xC0);   // MX=0, MY=0 (try 0xC8 if orientation wrong)

    LCD_Cmd(hi2c, CMD8W, 0x81);   // Set EV
    LCD_Cmd(hi2c, CMD8W, 0x99);   // EV = 153 (try 0x40..0xA0)

    // Set page/col = 0
    LCD_SetPage0Col0(hi2c, CMD8W);

    // ---- Write data into DDRAM (TEST: full white 128x32) ----
//    uint8_t line[128];
//    memset(line, 0xFF, sizeof(line)); // all pixels ON in each page
//
//    for (uint8_t page = 0; page < 4; page++) {
//      LCD_SetPage(hi2c, CMD8W, page);
//      LCD_SetCol(hi2c, CMD8W, 0);
//      LCD_Data(hi2c, DATA8W, line, 128);
//    }

    LCD_Cmd(hi2c, CMD8W, 0xAF);   // Display On
    HAL_Delay(100);
}
// ---------- Clear full 128x32 ----------
void ST7539_Clear(I2C_HandleTypeDef *hi2c) {
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7    << 1); // 0x7C
  uint16_t DATA8W  = (uint16_t)(ST7539_DATA_PREFIX_7    << 1); // 0x7D

  uint8_t zero[128];
  memset(zero, 0x00, sizeof(zero));

  for (uint8_t page = 0; page < 4; page++) {
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol(hi2c, CMD8W, 0);
    LCD_Data(hi2c, DATA8W, zero, sizeof(zero));
  }
}

// Draw text on one page (8px tall). 5x7 fits into a page.
void ST7539_DrawText5x7(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, const char *s) {
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7    << 1); // 0x7C
  uint16_t DATA8W  = (uint16_t)(ST7539_DATA_PREFIX_7    << 1); // 0x7D

  LCD_SetPage(hi2c, CMD8W, page);
  LCD_SetCol(hi2c, CMD8W, COLUMN_OFFSET);
//  LCD_SetPage0Col0(hi2c, CMD8W);

  while (*s) {
    const uint8_t *g = font5x7_get(*s++);
    uint8_t out[6] = { g[0], g[1], g[2], g[3], g[4], 0x00 };
    LCD_Data(hi2c, DATA8W, out, sizeof(out));
  }
}



static const Glyph16W* font16_getw(char c) {
  for (unsigned i=0; i<sizeof(font16)/sizeof(font16[0]); i++)
    if (font16[i].c == c) return &font16[i];
  return &font16[sizeof(font16)/sizeof(font16[0]) - 1]; // space as default
}

//USE TO DRAW CLOCK
void ST7539_DrawText16x16(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, const char *s)
{
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7  << 1);
  uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7 << 1);

  while (*s) {
    const Glyph16W *gw = font16_getw(*s++);
    uint8_t w = gw->width;

    // top half
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, (uint8_t*)&gw->b[0], w);

    // bottom half
    LCD_SetPage(hi2c, CMD8W, page + 1);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, (uint8_t*)&gw->b[16], w);

    col += w;
  }
}
// If you have a clear function, use it. Otherwise this clears by writing zeros.
static void ST7539_Clear32(I2C_HandleTypeDef *hi2c)
{
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7  << 1);
  uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7 << 1);

  uint8_t line[128];
  memset(line, 0x00, sizeof(line));

  for (uint8_t page = 0; page < 4; page++) {
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol (hi2c, CMD8W, 0);
    LCD_Data   (hi2c, DATA8W, line, 128);
  }
}


void Clock_Draw(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col,
                           uint8_t hh, uint8_t mm, uint8_t ss)
{
  char buf[16];
  // format: "HH:MM:SS"
  snprintf(buf, sizeof(buf),
           "%u %u : %u %u : %u %u",
           hh/10, hh%10, mm/10, mm%10, ss/10, ss%10);

  // Clear only the 2-page band you draw into (optional).
  // If you don't have region-clear, just clear full screen each second (safe but slower).
  ST7539_Clear32(hi2c);

  ST7539_DrawText16x16(hi2c, page, col, buf);
}


// Compute X position of each digit in "H H : M M : S S"
static uint8_t clock_digit_col(uint8_t base_col, uint8_t digit_index)
{
    // positions in tokens: D sp D sp : sp D sp D sp : sp D sp D
    // token widths:        16  3 16  3 2  3 16 3 16 3 2 3 16 3 16

    uint8_t col = base_col;

    // Helper macro to advance
    #define ADV(w) do { col += (w); } while(0)

    switch (digit_index) {
      case 0: return col;                        // H tens
      case 1: ADV(W_DIGIT); ADV(W_SPACE); return col;  // H ones

      case 2:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        return col;                              // M tens

      case 3:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        return col;                              // M ones

      case 4:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        return col;                              // S tens

      case 5:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        return col;                              // S ones

      default:
        return base_col;
    }

    #undef ADV
}
void ST7539_EraseGlyph16x16(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, uint8_t width)
{
    uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7  << 1);
    uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7 << 1);

    if (width == 0) return;

    uint8_t zeros[16];                 // max we erase per call here
    if (width > sizeof(zeros)) width = sizeof(zeros);
    memset(zeros, 0x00, width);

    // top half (page)
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, zeros, width);

    // bottom half (page+1)
    LCD_SetPage(hi2c, CMD8W, page + 1);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, zeros, width);
}

void ST7539_EraseClockDigit(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t base_col, uint8_t digit_index)
{
    uint8_t col = clock_digit_col(base_col, digit_index);
    ST7539_EraseGlyph16x16(hi2c, page, col, W_DIGIT);
}


