/*
 * ST7539.c
 *
 *  Created on: Jan 16, 2026
 *      Author: anh.tqt
 */
#include "ST7539.h"
#include "main.h"

// ST7539 I2C slave control byte on bus (8-bit):
// [7:0] = 0 1 1 1 SA1 SA0 A0 W/R

//A0 = 1 → payload bytes are display data → stored into display RAM, and internal pointer auto-increments.
//A0 = 0 → payload bytes are commands → decoded to change settings.
//W  = 0 -> Write
//
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
  return HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr8_write<<1), 2, 50);
}

// -------- Scan and decode SA0/SA1 using 8-bit bus addresses --------
ST7539ScanResult8 ST7539_FindAddressPair(I2C_HandleTypeDef *hi2c) {
	ST7539ScanResult8 r = {0};

  for (uint8_t cmd = 0x38; cmd <= 0x3E; cmd += 2) {
    uint8_t data = cmd + 1;
    if (I2C_Ack8(hi2c, cmd) && I2C_Ack8(hi2c, data)) {
      r.found = 1;
      r.cmd8w = cmd;
      r.data8w = data;

      // cmdAddr = 0b0111 SA1 SA0 0
      r.sa1 = (cmd >> 2) & 0x01;
      r.sa0 = (cmd >> 1) & 0x01;
      return r;
    }
  }

  return r;
}

// -------- Low-level write helpers (8-bit addressing) --------
HAL_StatusTypeDef ST7539_WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd8, const uint8_t *buf, uint16_t len) {
  // cmd8 is the 8-bit bus address byte with W=0
  return HAL_I2C_Master_Transmit(hi2c, (uint16_t)cmd8, (uint8_t*)buf, len, 100);
}

HAL_StatusTypeDef ST7539_WriteData(I2C_HandleTypeDef *hi2c, uint8_t data8, const uint8_t *buf, uint16_t len) {
  return HAL_I2C_Master_Transmit(hi2c, (uint16_t)data8, (uint8_t*)buf, len, 100);
}
