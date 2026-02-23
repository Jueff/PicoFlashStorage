#include "SecureSector.h"
#include <cstdlib> // For malloc and free
#include <cstring> // For memcpy and memset
#include "Arduino.h" // For Serial
#include "crc16.h"
#include "PicoFlashStorage.h"
#include "FlashStorageConfig.h"

namespace PicoFlashStorage {

  SecureSector::SecureSector(uint16_t sectorNumber, const uint8_t identity[8])
  {
    buffer = (uint8_t*)malloc(FLASH_PAGE_SIZE);
    memcpy(this->identity, identity, 8);
    eraseCount = 1;
    this->sectorNumber = sectorNumber;
    checkFormat();
  }

  SecureSector::~SecureSector()
  {
    free(buffer);
  }

  int SecureSector::nextEraseCount(int eraseCount)
  {
    if (eraseCount >= MaxEraseCount) return 0;
    return ++eraseCount;
  }

  bool SecureSector::isHeaderValid() const
  {
    return headerValid;
  }

  int SecureSector::getEraseCount() const
  {
    return eraseCount;
  }

  uint16_t SecureSector::getSectorNumber() const
  {
    return sectorNumber;
  }

  bool SecureSector::checkFormat()
  {
    uint8_t* address = (uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE;
    memcpy(buffer, address, FLASH_PAGE_SIZE);
    memcpy(buffer, identity, sizeof(identity));
    setCRC(0, 14);

    headerValid = (memcmp(address, buffer, 16) == 0);
    if (!headerValid)
    {
#if FSC_LEVEL>= 1
      Serial.printf("Header of sector %d is invalid\r\n", sectorNumber);
#endif
    }
    eraseCount = (buffer[11] << 16) + (buffer[12] << 8) + (buffer[13]);
    return headerValid;
  }

  bool SecureSector::format(int eraseCount)
  {
    uint8_t* addr = (uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE;
    this->eraseCount = eraseCount;
    memset(buffer, 0xff, FLASH_PAGE_SIZE);
    memcpy(buffer, identity, sizeof(identity));
    buffer[11] = eraseCount >> 16;
    buffer[12] = eraseCount >> 8;
    buffer[13] = eraseCount;
    setCRC(0, 14);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sectorNumber * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    flash_range_program(sectorNumber * FLASH_SECTOR_SIZE, buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
#if FSC_LEVEL>= 3
    Serial.printf("formatting sector %d with eraseCount %d\r\n", sectorNumber, eraseCount);
#endif
    return checkFormat() && getFreeMemoryStartOffset() <= 16;
  }

  bool SecureSector::write(FlashWriteBlock* block)
  {
    auto blockIndex = getFirstFreeBlock();
    if (blockIndex >= getMaxBlockCount()) return false;
    const uint8_t* address = getBlockAddress(blockIndex);
    uint8_t* pageAddress = getPageAddress(blockIndex);
    uint32_t flashOffset = (uint32_t)(pageAddress - XIP_BASE);
    uint8_t bufferOffset = address - pageAddress;
    memcpy(buffer, pageAddress, FLASH_PAGE_SIZE);
    memcpy(buffer + bufferOffset, block->getBuffer(), 8);
#if FSC_LEVEL>= 3
    Serial.printf("writing block at flash address %X\r\n", address + bufferOffset);
#endif
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flashOffset, buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    return true;
  }

  bool SecureSector::hasFreeBlock() const
  {
    return getFirstFreeBlock() < getMaxBlockCount();
  }

  uint16_t SecureSector::getFirstFreeBlock() const
  {
    return ((getFreeMemoryStartOffset() + 7) / 8) - 2;
  }

  uint8_t* SecureSector::getBlockAddress(uint16_t blockIndex) const
  {
    return (uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE + 16 + blockIndex * 8;
  }

  uint8_t* SecureSector::getPageAddress(uint16_t blockIndex) const
  {
    uint16_t sectorOffset = 16 + blockIndex * 8;
    return (uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE + (sectorOffset / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
  }

  uint16_t SecureSector::getMaxBlockCount() const
  {
    return (FLASH_SECTOR_SIZE - 16) / 8;
  }

#if FSC_LEVEL>0
  void SecureSector::dump() const
  {
    FlashStorage::dumpMemory((uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
  }

  void SecureSector::dumpPage(uint8_t pageId) const
  {
    FlashStorage::dumpMemory((uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE + pageId * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE);
  }

  void SecureSector::dumpBuffer() const
  {
    FlashStorage::dumpMemory(buffer, FLASH_PAGE_SIZE);
  }
#endif

  uint16_t SecureSector::getFreeMemoryStartOffset() const
  {
    uint8_t* addr = (uint8_t*)XIP_BASE + sectorNumber * FLASH_SECTOR_SIZE;
    for (int index = FLASH_SECTOR_SIZE - 1; index >= 0; index--)
    {
      if (*(addr + index) != 0xff) return index + 1;
    }
    return 0;
  }

  bool SecureSector::isEmpty() const
  {
    if (getFreeMemoryStartOffset() == 0)
    {
#if FSC_LEVEL>= 4
      Serial.printf("Sector %d is empty\r\n", sectorNumber);
#endif
      return true;
    }
    return false;
  }

  void SecureSector::setCRC(uint16_t offset, uint16_t length)
  {
    uint16_t crc = CRC::crc16(buffer + offset, length);
    *(buffer + offset + length) = crc >> 8;
    *(buffer + offset + length + 1) = crc & 0xff;
#if FSC_LEVEL>= 5
    Serial.printf("set CRC sector %d at address %X with length %d: crc=%04X\r\n", sectorNumber, buffer + offset, length, crc);
#endif
  }
}