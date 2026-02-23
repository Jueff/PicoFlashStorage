/*
   crc16.h

   Created: 16-01-2018 23:07:05
    Author: JMR_2
*/

#pragma once
#include <stdint.h>

namespace PicoFlashStorage {

  class CRC {
  public:
    static uint16_t next(uint8_t newchar, uint16_t previous = 0xFFFF);		// 'previous' defaults to CRC seed value, 0xFFFF
    static uint16_t crc16(const uint8_t* buffer, uint16_t length);
  };
}
