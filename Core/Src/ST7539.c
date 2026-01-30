/*
 * ST7539.c
 *
 *  Created on: Jan 16, 2026
 *      Author: anh.tqt
 *
 *  ---------------------------------------------------------------------------
 *  ST7539 LCD Driver (I2C mode) - Detailed Commentary
 *  ---------------------------------------------------------------------------
 *
 *  Overview
 *  --------
 *  This file provides a small driver layer for an ST7539-based monochrome LCD
 *  connected via I2C. It supports:
 *    - Hardware reset
 *    - Low-level command/data writes over I2C
 *    - Address scanning (trying command/data control bytes)
 *    - Basic initialization sequence
 *    - Clear full screen (128x32 => 4 pages x 128 columns)
 *    - Text rendering with 5x7 font (bit-reversed to match LCD bit order)
 *    - Bigger font rendering (16x16 glyphs split across 2 LCD pages)
 *    - A "clock" helper using 16x16 digits with partial erase functions
 *
 *  Notes about ST7539 over I2C
 *  ---------------------------
 *  ST7539 uses a *control byte* concept:
 *     A0 = 0 => following bytes are COMMANDS
 *     A0 = 1 => following bytes are DISPLAY DATA
 *
 *  On the I2C bus, you typically use two 8-bit "slave control bytes":
 *     - One with A0=0 for commands (CMD8W)
 *     - One with A0=1 for data     (DATA8W)
 *
 *  IMPORTANT:
 *    - The HAL library expects a 7-bit slave address in many functions,
 *      then shifts left internally. If you pass an already-shifted
 *      "8-bit" address, you can easily end up shifting twice.
 *    - In this code, CMD8W/DATA8W appear to be already shifted (<<1),
 *      and are passed directly to HAL_I2C_Master_Transmit().
 *      This works only if those values match what HAL expects (device
 *      address in upper 7 bits, LSB ignored). Keep consistent!
 *
 *  Display memory model (128x32)
 *  -----------------------------
 *  The LCD is treated like a "paged" display:
 *     - Each "page" is 8 pixels tall.
 *     - 32 pixels high => 4 pages (0..3)
 *     - Each page has 128 columns.
 *  Writing data bytes fills 8 vertical pixels at the current column,
 *  then column auto-increments.
 */

#include "ST7539.h"
#include <string.h>
#include "main.h"

/*
 * ST7539 I2C slave control byte on bus (8-bit):
 *   [7:0] = 0 1 1 1 SA1 SA0 A0 W/R
 *
 * Where:
 *   - SA1/SA0 are strap pins (hardware address select)
 *   - A0 selects command/data stream
 *   - W/R is read/write bit (we only use write)
 *
 * A0 = 1  -> payload bytes are display data -> stored into display RAM
 * A0 = 0  -> payload bytes are commands -> modify controller settings
 * W  = 0  -> write
 */

/* -------------------------------------------------------------------------- */
/*                              RESET / STRAP PINS                             */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_HWReset()
 * ----------------
 * Toggles the LCD reset pin (RST) to force the ST7539 into a known state.
 * Typical reset pulse is a few milliseconds; delays provide enough setup time.
 *
 * Requirements:
 *   - RST_GPIO_Port/RST_Pin must be configured as GPIO output.
 */
void ST7539_HWReset(void) {
  HAL_GPIO_WritePin(RST_GPIO_Port, RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(RST_GPIO_Port, RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);
}

/*
 * ST7539_SetSA0_Pin(level)
 * ------------------------
 * If the ST7539 is configured to use a strap pin for SA0 (I2C address selection),
 * it must be held at a fixed level *at startup*. DO NOT toggle it dynamically
 * during normal operation or your bus address will change.
 *
 * This helper simply sets the CS0 pin (used as SA0 strap in some HW designs).
 */
void ST7539_SetSA0_Pin(uint8_t level) {
  HAL_GPIO_WritePin(CS0_GPIO_Port, CS0_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* -------------------------------------------------------------------------- */
/*                              LOW-LEVEL I2C I/O                              */
/* -------------------------------------------------------------------------- */

/*
 * I2C_Ack8()
 * ---------
 * Attempts to detect if a device ACKs at a given "address".
 *
 * WARNING / Bug note:
 *   HAL_I2C_IsDeviceReady() expects a 7-bit address (not including R/W bit)
 *   placed in bits [7:1] then shifted by HAL. In most STM32 HAL versions,
 *   the API expects the address already in 7-bit form, and the function
 *   internally shifts left by 1.
 *
 * Here, the function signature says "addr8_write", but the code shifts again:
 *     HAL_I2C_IsDeviceReady(hi2c, addr8_write<<1, ...)
 * If addr8_write is already an 8-bit control byte (like 0x7C), shifting again
 * will break addressing. If addr8_write is a 7-bit base, shifting is OK.
 *
 * So: be consistent about what you pass in.
 */
static HAL_StatusTypeDef I2C_Ack8(I2C_HandleTypeDef *hi2c, uint8_t addr8_write) {
  // HAL expects a 7-bit address in most cases; code shifts left by 1 here.
  return HAL_I2C_IsDeviceReady(hi2c, addr8_write<<1, 2, 50);

  // (Old attempt commented out - wrong prototype usage)
  // return HAL_I2C_Master_Transmit(hi2c, addr8_write<<1, addr8_write<<1, 2, 50);
}

/*
 * LCD_Cmd()
 * ---------
 * Sends a single command byte 'c' to the LCD using the CMD control address.
 *
 * Parameters:
 *   - cmd8w: I2C address/control-byte for "command write" (A0=0, W=0)
 */
static HAL_StatusTypeDef LCD_Cmd(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t c) {
  return HAL_I2C_Master_Transmit(hi2c, cmd8w, &c, 1, 100);
}

/*
 * LCD_Cmd2()
 * ----------
 * Sends two command bytes in one I2C transaction (common for "command + value").
 */
static HAL_StatusTypeDef LCD_Cmd2(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t c1, uint8_t c2) {
  uint8_t b[2] = {c1, c2};
  return HAL_I2C_Master_Transmit(hi2c, cmd8w, b, 2, 100);
}

/*
 * bitrev8()
 * ---------
 * Reverses bit order of one byte: b7..b0 -> b0..b7.
 *
 * Some LCD memory layouts require vertical bit order opposite to the font
 * representation. Reversing allows using a standard font table but matching
 * the LCD's expected bit direction.
 */
static inline uint8_t bitrev8(uint8_t x)
{
  x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
  x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
  x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
  return x;
}

/*
 * LCD_Data()
 * ----------
 * Sends display data bytes (A0=1 stream) to the LCD.
 *
 * The ST7539 will write these bytes into display RAM at the current
 * page+column location, and auto-increment column pointer.
 */
HAL_StatusTypeDef LCD_Data(I2C_HandleTypeDef *hi2c, uint8_t data8w, const uint8_t *buf, uint16_t len) {
  return HAL_I2C_Master_Transmit(hi2c, data8w, (uint8_t*)buf, len, 400);
}

/* -------------------------------------------------------------------------- */
/*                           ADDRESSING: PAGE + COLUMN                         */
/* -------------------------------------------------------------------------- */

/*
 * LCD_SetPage()
 * -------------
 * Sets the display RAM page address (0..15 typically, but 0..3 used for 32px).
 * Command format: 0xB0 | page
 */
void LCD_SetPage(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t page) {
  LCD_Cmd(hi2c, cmd8w, 0xB0 | (page & 0x0F));
}

/*
 * LCD_SetCol()
 * ------------
 * Sets the display RAM column address using two commands:
 *   - 0x10 | high nibble
 *   - 0x00 | low nibble
 */
void LCD_SetCol(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, uint8_t col) {
  LCD_Cmd(hi2c, cmd8w, 0x10 | ((col >> 4) & 0x0F));
  LCD_Cmd(hi2c, cmd8w, 0x00 | (col & 0x0F));
}

/*
 * LCD_SetPage0Col0()
 * ------------------
 * Convenience: sets page=0 and column=0+offset.
 *
 * COLUMN_OFFSET is used when the panel/driver maps visible column 0 to
 * an internal RAM offset (common with some LCD glass wiring).
 */
void LCD_SetPage0Col0(I2C_HandleTypeDef *hi2c, uint16_t CMD8W) {
  LCD_Cmd(hi2c, CMD8W, 0xB0); // page 0
  LCD_Cmd(hi2c, CMD8W, 0x10); // col MSB = 0
  LCD_Cmd(hi2c, CMD8W, COLUMN_OFFSET); // col LSB = COLUMN_OFFSET
}

/* -------------------------------------------------------------------------- */
/*                          ADDRESS SCAN (SA0/SA1)                             */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_FindAddressPair()
 * ------------------------
 * Tries to scan possible I2C control-byte pairs for ST7539.
 *
 * It loops command control bytes:
 *   0x38, 0x3A, 0x3C, 0x3E
 * and assumes data control byte is cmd+1:
 *   data = cmd + 1
 *
 * If both ACK, it considers the device found.
 *
 * WARNING / Bug note:
 *   In the current code, when found, it sets:
 *       r.cmd8w  = cmd1;
 *       r.data8w = data1;
 *   But cmd1/data1 are HAL_StatusTypeDef (HAL_OK/HAL_ERROR),
 *   not the address bytes. So this stores wrong values.
 *
 * It also tries to decode SA1/SA0 from cmd1 (status), which is incorrect.
 * The decode should be from 'cmd' itself.
 *
 * So this scan function prints useful debug, but the return struct fields
 * are currently incorrect unless you fix the assignments.
 */
ST7539ScanResult8 ST7539_FindAddressPair(I2C_HandleTypeDef *hi2c) {
  ST7539ScanResult8 r = {0};

  for (uint8_t cmd = 0x38; cmd <= 0x3E; cmd += 2) {
    uint8_t data = cmd + 1;

    HAL_StatusTypeDef cmd1  = I2C_Ack8(hi2c, cmd);
    HAL_StatusTypeDef data1 = I2C_Ack8(hi2c, data);

    if (cmd1 == HAL_OK && data1 == HAL_OK) {
      r.found = 1;

      // BUG: should be r.cmd8w = cmd; r.data8w = data;
      r.cmd8w  = cmd1;
      r.data8w = data1;

      // BUG: should decode from 'cmd', not from cmd1 status
      // cmdAddr = 0b0111 SA1 SA0 0
      r.sa1 = (cmd1 >> 2) & 0x01;
      r.sa0 = (cmd1 >> 1) & 0x01;

      return r;
    } else {
      // Prints probe result + I2C error flags for debugging
      printf("0x%02X=%d  0x%02X=%d  err=0x%08lX\r\n",
             cmd, cmd1, cmd+1, data1, (unsigned long)HAL_I2C_GetError(hi2c));
    }
  }

  return r;
}

/* -------------------------------------------------------------------------- */
/*                              BASIC INITIALIZATION                           */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_InitBasic()
 * ------------------
 * Sends a minimal init sequence that configures the LCD and fills test pattern.
 *
 * CMD8W and DATA8W are derived from ST7539_CMD_PREFIX_7 / ST7539_DATA_PREFIX_7:
 *   - These are expected to represent the device's base control bytes / address.
 *
 * Sequence highlights (depends on ST7539 datasheet):
 *   - SW reset (0xE2)
 *   - Display OFF (0xAE)
 *   - Normal display (0xA6)
 *   - Start line (0x40)
 *   - Frame rate / some setting (0xA0)
 *   - Inverse display?? (0xA6 used again; comment says inverse but 0xA7 is
 *     often "inverse" in other controllers; verify with ST7539 datasheet)
 *   - Bias (0xEB)
 *   - Scan direction (0xC2, sometimes 0xC8 flips)
 *   - Contrast (0x81 then value 0x99)
 *
 * Then it writes a test pattern:
 *   - pages 0..3
 *   - even pages filled with 0xFF (all pixels ON)
 *   - odd pages filled with 0x00 (all pixels OFF)
 *
 * Finally Display ON (0xAF).
 */
void ST7539_InitBasic(I2C_HandleTypeDef *hi2c) {
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7    << 1); // example: 0x7C
  uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7    << 1); // example: 0x7D

  // Reset LCD hardware
  ST7539_HWReset();
  HAL_Delay(100);

  // Basic init commands
  LCD_Cmd(hi2c, CMD8W, 0xE2);      // SW reset
  HAL_Delay(10);
  LCD_Cmd(hi2c, CMD8W, 0xAE);      // Display OFF
  LCD_Cmd(hi2c, CMD8W, 0xA6);      // Normal display (verify: inverse is often 0xA7)
  LCD_Cmd(hi2c, CMD8W, 0x40);      // Start line = 0
  LCD_Cmd(hi2c, CMD8W, 0xA0);      // Frame rate / panel setting (verify)
  LCD_Cmd(hi2c, CMD8W, 0xA6);      // Comment says inverse, but value is A6 again
  LCD_Cmd(hi2c, CMD8W, 0xEB);      // Bias setting
  LCD_Cmd(hi2c, CMD8W, 0xC2);      // Scan direction (try C8 if upside down)

  // Contrast: command 0x81 then contrast value
  LCD_Cmd(hi2c, CMD8W, 0x81);
  LCD_Cmd(hi2c, CMD8W, 0x99);

  // Set initial address
  LCD_SetPage0Col0(hi2c, CMD8W);

  // Test pattern buffers
  uint8_t line1[128];
  uint8_t line2[128];
  memset(line1, 0xFF, sizeof(line1)); // all pixels ON in each byte
  memset(line2, 0x00, sizeof(line2)); // all pixels OFF

  // Write alternating stripes by page
  for (uint8_t page = 0; page < 4; page++) {
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol(hi2c, CMD8W, COLUMN_OFFSET);

    if (page % 2 == 0) {
      LCD_Data(hi2c, DATA8W, line1, 128);
    } else {
      LCD_Data(hi2c, DATA8W, line2, 128);
    }
  }

  // Turn display ON
  LCD_Cmd(hi2c, CMD8W, 0xAF);
  HAL_Delay(100);
}

/* -------------------------------------------------------------------------- */
/*                                CLEAR DISPLAY                                */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_Clear()
 * --------------
 * Clears the full 128x32 screen by writing 0x00 to all pages.
 *
 * NOTE:
 *   Here LCD_SetCol is called with col=0 (no COLUMN_OFFSET).
 *   If your panel requires COLUMN_OFFSET to align visible left edge, you
 *   probably want to use COLUMN_OFFSET here as well.
 */
void ST7539_Clear(I2C_HandleTypeDef *hi2c) {
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7    << 1); // e.g. 0x7C
  uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7    << 1); // e.g. 0x7D

  uint8_t zero[128];
  memset(zero, 0x00, sizeof(zero));

  for (uint8_t page = 0; page < 4; page++) {
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol(hi2c, CMD8W, 0);
    LCD_Data(hi2c, DATA8W, zero, sizeof(zero));
  }
}

/* -------------------------------------------------------------------------- */
/*                           5x7 TEXT RENDERING (1 PAGE)                       */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_DrawLine5x7()
 * --------------------
 * Draws a single line of ASCII text using a 5x7 font on one LCD page.
 *
 * Inputs:
 *   - page: which LCD page to draw on (0..3 for 32px panel)
 *   - col : starting column (0..127). If your panel needs COLUMN_OFFSET,
 *           caller can add it or change implementation.
 *   - s   : string to render; stops on '\r' or '\n'
 *
 * Rendering:
 *   - Each character uses 5 columns of glyph + 1 column spacing = 6 columns.
 *   - A space ' ' is implemented as 2 blank columns (narrower than 6).
 *   - Each glyph byte is bit-reversed before sending to match LCD bit order.
 */
void ST7539_DrawLine5x7(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, const char *s)
{
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7  << 1);
  uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7 << 1);

  // Set target page and start column
  LCD_SetPage(hi2c, CMD8W, page);
  LCD_SetCol (hi2c, CMD8W, col);

  while (*s) {
    char c = *s++;
    if (c == '\r' || c == '\n') break;

    if (c == ' ') {
      // Render a narrow space (2 blank columns)
      uint8_t sp[2] = {0x00, 0x00};
      LCD_Data(hi2c, DATA8W, sp, 2);
      col += 2;
      if (col >= 128) break;
    } else {
      // Lookup 5x7 glyph data for character
      const uint8_t *g = font5x7_get(c);

      // Prepare output: 5 columns + 1 column spacing
      uint8_t out[6] = {
        bitrev8(g[0]),
        bitrev8(g[1]),
        bitrev8(g[2]),
        bitrev8(g[3]),
        bitrev8(g[4]),
        0x00 // spacing column
      };

      // Clip if no room for full character
      if (col + 6 > 128) break;

      // Transmit to LCD; LCD auto-increments column
      LCD_Data(hi2c, DATA8W, out, 6);
      col += 6;
    }
  }
}

/* -------------------------------------------------------------------------- */
/*                           16x16 FONT LOOKUP + DRAW                          */
/* -------------------------------------------------------------------------- */

/*
 * font16_getw()
 * -------------
 * Finds a Glyph16W entry for the given character.
 *
 * font16[] is expected to be an array of Glyph16W with:
 *   - .c : character code
 *   - .width : glyph width in columns (often 16 for digits, 2 for ':' ...)
 *   - .b[] : bitmap bytes. Here it is used as:
 *       - first 16 bytes = top 8 rows (page)
 *       - next 16 bytes  = bottom 8 rows (page+1)
 *
 * If not found, returns the last font entry as a default (space).
 */
static const Glyph16W* font16_getw(char c) {
  for (unsigned i=0; i<sizeof(font16)/sizeof(font16[0]); i++)
    if (font16[i].c == c) return &font16[i];
  return &font16[sizeof(font16)/sizeof(font16[0]) - 1]; // space as default
}

/*
 * ST7539_DrawText16x16()
 * ----------------------
 * Draws text using 16x16 glyphs. Each glyph is rendered across two pages:
 *   - page     : top half (rows 0..7 of the glyph)
 *   - page + 1 : bottom half (rows 8..15)
 *
 * For each character:
 *   - get glyph with font16_getw()
 *   - write top bytes to 'page'
 *   - write bottom bytes to 'page+1'
 *   - advance column by glyph width
 *
 * Used for clock digits in this project.
 */
void ST7539_DrawText16x16(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, const char *s)
{
  uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7  << 1);
  uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7 << 1);

  while (*s) {
    const Glyph16W *gw = font16_getw(*s++);
    uint8_t w = gw->width;

    // Draw top 8 rows of glyph into 'page'
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, (uint8_t*)&gw->b[0], w);

    // Draw bottom 8 rows of glyph into 'page+1'
    LCD_SetPage(hi2c, CMD8W, page + 1);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, (uint8_t*)&gw->b[16], w);

    col += w;
  }
}

/* -------------------------------------------------------------------------- */
/*                           CLEAR HELPER (32px high)                          */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_Clear32()
 * ----------------
 * Clears all 4 pages by writing 128 zeros to each page.
 * This is similar to ST7539_Clear(), but used by Clock_Draw() before drawing.
 *
 * NOTE:
 *   Uses LCD_SetCol(..., 0). If you need COLUMN_OFFSET alignment, apply it.
 */
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

/* -------------------------------------------------------------------------- */
/*                                CLOCK DRAWING                                */
/* -------------------------------------------------------------------------- */

/*
 * Clock_Draw()
 * ------------
 * Draws a digital clock (HH:MM:SS) using the 16x16 font.
 *
 * Format used:
 *   "%u %u : %u %u : %u %u"
 * This produces a string like:
 *   "1 2 : 3 4 : 5 6"
 *
 * This layout assumes the font includes:
 *   - digits '0'..'9' with width W_DIGIT (usually 16)
 *   - ':' with width W_COLON (usually 2)
 *   - ' ' (space) with some width defined in font16 table (often 4)
 *
 * Steps:
 *   - clamp hh/mm/ss to valid ranges
 *   - build formatted string with spaces between digits
 *   - clear entire display
 *   - draw the 16x16 string starting at (page,col)
 */
void Clock_Draw(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col,
                uint8_t hh, uint8_t mm, uint8_t ss)
{
  // sanitize inputs
  if (hh > 23) hh = 0;
  if (mm > 59) mm = 0;
  if (ss > 59) ss = 0;

  char buf[24];

  // "H H : M M : S S"
  snprintf(buf, sizeof(buf),
           "%u %u : %u %u : %u %u",
           (unsigned)(hh/10), (unsigned)(hh%10),
           (unsigned)(mm/10), (unsigned)(mm%10),
           (unsigned)(ss/10), (unsigned)(ss%10));

  // clear then draw
  ST7539_Clear32(hi2c);
  ST7539_DrawText16x16(hi2c, page, col, buf);
}

/* -------------------------------------------------------------------------- */
/*                   CLOCK LAYOUT: DIGIT/COLON COLUMN CALCULATION              */
/* -------------------------------------------------------------------------- */

/*
 * clock_digit_col()
 * -----------------
 * Computes the X column position for each digit within the clock layout.
 *
 * The intended token pattern is:
 *   D sp D sp : sp D sp D sp : sp D sp D
 *
 * Where widths are constants:
 *   W_DIGIT = width of a 16x16 digit glyph (often 16)
 *   W_SPACE = width of a space in the layout (often 4)
 *   W_COLON = width of ':' glyph (often 2)
 *
 * digit_index mapping:
 *   0: HH tens
 *   1: HH ones
 *   2: MM tens
 *   3: MM ones
 *   4: SS tens
 *   5: SS ones
 *
 * This allows partial updates (erase/re-draw only the changed digit).
 */
static uint8_t clock_digit_col(uint8_t base_col, uint8_t digit_index)
{
    uint8_t col = base_col;

    // Helper macro: advance current column by a width
    #define ADV(w) do { col += (w); } while(0)

    switch (digit_index) {
      case 0: return col;                                  // H tens

      case 1: ADV(W_DIGIT); ADV(W_SPACE); return col;      // H ones

      case 2:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        return col;                                        // M tens

      case 3:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        return col;                                        // M ones

      case 4:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        return col;                                        // S tens

      case 5:
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        ADV(W_COLON); ADV(W_SPACE);
        ADV(W_DIGIT); ADV(W_SPACE);
        return col;                                        // S ones

      default:
        return base_col;
    }

    #undef ADV
}

/* -------------------------------------------------------------------------- */
/*                         PARTIAL ERASE (16x16 GLYPHS)                         */
/* -------------------------------------------------------------------------- */

/*
 * ST7539_EraseGlyph16x16()
 * ------------------------
 * Clears (erases) a rectangular glyph area of size:
 *   width columns x 16 rows
 *
 * Because 16 rows span two pages:
 *   - top 8 rows on 'page'
 *   - bottom 8 rows on 'page+1'
 *
 * Implementation:
 *   - builds a small zero buffer (up to 16 bytes here)
 *   - writes it to page and page+1 at the same column
 *
 * Limitation:
 *   - zeros[] buffer is 16 bytes; if width > 16 it's clipped.
 *     For digits, width is typically 16 so it's perfect.
 */
void ST7539_EraseGlyph16x16(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t col, uint8_t width)
{
    uint16_t CMD8W  = (uint16_t)(ST7539_CMD_PREFIX_7  << 1);
    uint16_t DATA8W = (uint16_t)(ST7539_DATA_PREFIX_7 << 1);

    if (width == 0) return;

    uint8_t zeros[16];
    if (width > sizeof(zeros)) width = sizeof(zeros);
    memset(zeros, 0x00, width);

    // Erase top half
    LCD_SetPage(hi2c, CMD8W, page);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, zeros, width);

    // Erase bottom half
    LCD_SetPage(hi2c, CMD8W, page + 1);
    LCD_SetCol (hi2c, CMD8W, col);
    LCD_Data   (hi2c, DATA8W, zeros, width);
}

/*
 * ST7539_EraseClockDigit()
 * ------------------------
 * Erases one of the clock digits (index 0..5) at the correct X position.
 * Uses clock_digit_col() to compute the column.
 */
void ST7539_EraseClockDigit(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t base_col, uint8_t digit_index)
{
    uint8_t col = clock_digit_col(base_col, digit_index);
    ST7539_EraseGlyph16x16(hi2c, page, col, W_DIGIT);
}

/*
 * clock_colon_col()
 * -----------------
 * Returns the column position of the ':' separators.
 *
 * colon_index:
 *   0 => first colon between HH and MM
 *   1 => second colon between MM and SS
 *
 * It computes position by summing widths of preceding tokens.
 */
static uint8_t clock_colon_col(uint8_t base_col, uint8_t colon_index)
{
    uint8_t col = base_col;

    // Move past "H tens + space + H ones + space"
    col += W_DIGIT;  col += W_SPACE;
    col += W_DIGIT;  col += W_SPACE;

    if (colon_index == 0) return col;  // first ':'

    // Move past first colon + space + "M tens + space + M ones + space"
    col += W_COLON;  col += W_SPACE;
    col += W_DIGIT;  col += W_SPACE;
    col += W_DIGIT;  col += W_SPACE;

    return col; // second ':'
}

/*
 * ST7539_EraseClockColon()
 * ------------------------
 * Erases one of the ':' separators.
 * Uses ST7539_EraseGlyph16x16() with width W_COLON (typically 2).
 */
void ST7539_EraseClockColon(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t base_col, uint8_t colon_index)
{
    uint8_t col = clock_colon_col(base_col, colon_index);
    ST7539_EraseGlyph16x16(hi2c, page, col, W_COLON); // W_COLON = 2
}

