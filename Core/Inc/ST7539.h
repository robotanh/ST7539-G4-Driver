/*
 * ST7539.h
 *
 *  Created on: Jan 16, 2026
 *      Author: anh.tqt
 *
 *  ---------------------------------------------------------------------------
 *  ST7539 LCD Driver Header (STM32F4 HAL + I2C)
 *  ---------------------------------------------------------------------------
 *  This header declares:
 *    - Low-level LCD control helpers (page/col, data write)
 *    - Basic init / clear
 *    - Text rendering APIs (5x7, 16x16 clock)
 *    - I2C address scan result struct
 *
 *  Notes:
 *    - ST7539 uses *two* I2C "control bytes" (addresses) in practice:
 *        CMD stream (A0=0) and DATA stream (A0=1)
 *    - COLUMN_OFFSET depends on your LCD glass mapping.
 */

#ifndef INC_ST7539_H_
#define INC_ST7539_H_

/* -------------------------------------------------------------------------- */
/*                                  INCLUDES                                  */
/* -------------------------------------------------------------------------- */
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include "ST7539_font.h"

/* -------------------------------------------------------------------------- */
/*                              PANEL CONFIGURATION                           */
/* -------------------------------------------------------------------------- */

/* Some LCD panels need an internal RAM offset to align visible column 0 */
#define COLUMN_OFFSET            (0x00u)   /* change to 0x40 if your panel needs it */

/* Display geometry (this project: 128x32 => 4 pages) */
#define LCD_COLS                 (128u)

/* -------------------------------------------------------------------------- */
/*                           CLOCK (16x16 FONT) LAYOUT                         */
/* -------------------------------------------------------------------------- */
#define W_DIGIT                  (16u)     /* width of one 16x16 digit glyph */
#define W_SPACE                  (4u)      /* width used for spaces in clock layout */
#define W_COLON                  (2u)      /* width of ':' glyph */

/* -------------------------------------------------------------------------- */
/*                           I2C ADDRESS / CONTROL BYTES                       */
/* -------------------------------------------------------------------------- */
/*
 * You currently use these as “prefixes” for command/data control bytes:
 *   ST7539_CMD_PREFIX_7  = 0x3E
 *   ST7539_DATA_PREFIX_7 = 0x3F
 *
 * In your .c file you do:
 *   CMD8W  = ST7539_CMD_PREFIX_7  << 1
 *   DATA8W = ST7539_DATA_PREFIX_7 << 1
 *
 * IMPORTANT:
 *   Make sure you are consistent with STM32 HAL addressing expectations:
 *   Many HAL APIs expect 7-bit address, and shift internally.
 */
#define ST7539_CMD_PREFIX_7       (0x3Eu)  /* command stream control (A0=0) */
#define ST7539_DATA_PREFIX_7      (0x3Fu)  /* data stream control (A0=1) */

/* Strap pin values (if you use SA1/SA0 in hardware). Not used in your current code. */
#define ST7539_SA1                (0u)
#define ST7539_SA0                (0u)

/*
 * Convert a 7-bit address to HAL "8-bit" format (left shifted, W=0)
 * (Use only if your HAL call expects that style.)
 */
#define ST7539_I2C_ADDR8W(addr7)  ((uint16_t)((uint16_t)(addr7) << 1))

/* -------------------------------------------------------------------------- */
/*                         LEGACY / DEBUG DEFINES (OPTIONAL)                   */
/* -------------------------------------------------------------------------- */
/* These look like old experiments. Keep only if you still use them elsewhere. */
#define CMD7                      (0x38u)
#define DATA7                     (0x39u)
#define CMD8                      (0x70u)
#define DATA8                     (0x72u)

/*
 * The following macros referenced ST7539_I2C_PREFIX_7 but it is not defined here.
 * They are not used in your posted .c, so they are removed from the main config.
 *
 * If you want to rebuild them properly, define ST7539_I2C_PREFIX_7 first.
 */
// #define ST7539_I2C_CMD7(SA1, SA0)   ...
// #define ST7539_I2C_DATA7(SA1, SA0)  ...

/* -------------------------------------------------------------------------- */
/*                                  TYPES                                     */
/* -------------------------------------------------------------------------- */

/*
 * Result structure for scanning ST7539 command/data address pair.
 * cmd8w / data8w should store the actual bus address/control bytes used for writes.
 */
typedef struct {
  uint8_t found;   /* 1 if a valid CMD/DATA pair was detected */
  uint8_t cmd8w;   /* I2C address/control byte for CMD write (A0=0) */
  uint8_t data8w;  /* I2C address/control byte for DATA write (A0=1) */
  uint8_t sa1;     /* decoded SA1 (if scan supports decoding) */
  uint8_t sa0;     /* decoded SA0 (if scan supports decoding) */
} ST7539ScanResult8;

/* -------------------------------------------------------------------------- */
/*                              CORE LOW-LEVEL API                             */
/* -------------------------------------------------------------------------- */

/* Hardware / strap helpers */
void ST7539_HWReset(void);
void ST7539_SetSA0_Pin(uint8_t level);

/* Low-level addressing helpers */
void LCD_SetCol(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t col);
void LCD_SetPage0Col0(I2C_HandleTypeDef *hi2c, uint16_t CMD8W);

/* Low-level data write (used by text drawing) */
HAL_StatusTypeDef LCD_Data(I2C_HandleTypeDef *hi2c,
                           uint8_t data8w,
                           const uint8_t *buf,
                           uint16_t len);

/* -------------------------------------------------------------------------- */
/*                              HIGH-LEVEL LCD API                             */
/* -------------------------------------------------------------------------- */
void ST7539_InitBasic(I2C_HandleTypeDef *hi2c);
void ST7539_Clear(I2C_HandleTypeDef *hi2c);

/* NOTE: In your .c, the function is named ST7539_DrawLine5x7(), not DrawText5x7().
 * If your project calls ST7539_DrawText5x7(), either rename in .c or change prototype.
 */
void ST7539_DrawLine5x7(I2C_HandleTypeDef *hi2c,
                        uint8_t page,
                        uint8_t col,
                        const char *s);

/* Scan address pair (CMD/DATA) on I2C bus */
ST7539ScanResult8 ST7539_FindAddressPair(I2C_HandleTypeDef *hi2c);

/* -------------------------------------------------------------------------- */
/*                     OPTIONAL: GENERIC "WRITE CMD/DATA" API                  */
/* -------------------------------------------------------------------------- */
/* You declared these, but in the .c you posted they are not implemented yet. */
HAL_StatusTypeDef ST7539_WriteCmd(I2C_HandleTypeDef *hi2c,
                                  uint8_t cmd8w,
                                  const uint8_t *buf,
                                  uint16_t len);

HAL_StatusTypeDef ST7539_WriteData(I2C_HandleTypeDef *hi2c,
                                   uint8_t data8w,
                                   const uint8_t *buf,
                                   uint16_t len);

/* -------------------------------------------------------------------------- */
/*                                 CLOCK API                                  */
/* -------------------------------------------------------------------------- */
/*
 * Clock format is drawn using 16x16 font across 2 pages:
 * Layout: "H H : M M : S S"
 */
void Clock_Draw(I2C_HandleTypeDef *hi2c,
                uint8_t page,
                uint8_t col,
                uint8_t hh,
                uint8_t mm,
                uint8_t ss);

/* Partial erase helpers (for updating only changed digits/colons) */
void ST7539_EraseClockDigit(I2C_HandleTypeDef *hi2c,
                            uint8_t page,
                            uint8_t base_col,
                            uint8_t digit_index);

void ST7539_EraseClockColon(I2C_HandleTypeDef *hi2c,
                            uint8_t page,
                            uint8_t base_col,
                            uint8_t colon_index);

#endif /* INC_ST7539_H_ */
