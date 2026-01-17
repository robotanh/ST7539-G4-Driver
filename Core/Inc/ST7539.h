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

typedef struct {
  uint8_t found;
  uint8_t cmd8w;   // 8-bit bus address byte for CMD WRITE (LSB=0)
  uint8_t data8w;  // 8-bit bus address byte for DATA WRITE (LSB=0)
  uint8_t sa1;
  uint8_t sa0;
} ST7539ScanResult8;

void ST7539_HWReset(void);
void ST7539_SetSA0_Pin(uint8_t level);
ST7539ScanResult8 ST7539_FindAddressPair(I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef ST7539_WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd8w, const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef ST7539_WriteData(I2C_HandleTypeDef *hi2c, uint8_t data8w, const uint8_t *buf, uint16_t len);

#endif /* INC_ST7539_H_ */
