/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
*/

#include "FlashBlock.h"
#include <cstdlib>
#include <cstring>
#include "Arduino.h"
#include "crc16.h"
#include "PicoFlashStorage.h"

namespace PicoFlashStorage {

  FlashBlock::FlashBlock(const uint8_t* address)
  {
    this->address = address;
  }

  void FlashBlock::setAddress(const uint8_t* address)
  {
    this->address = address;
  }

  bool FlashBlock::isEmpty() const
  {
    if (address == nullptr) return false;
    for (uint8_t index = 0; index < 8; index++)
    {
      if (*(address + index) != 0xff) return false;
    }
    return true;
  }

  /*
  * A block is valid if the CRC of the first 6 bytes matches the last 2 bytes or the inverted CRC matches and the block is empty.
  * This means that the block
    - has been written at least once
    - is not corrupted
  */

  bool FlashBlock::isValid() const
  {
    if (address == nullptr) return false;
    uint16_t crc = CRC::crc16(address, 6);
    uint16_t crcExpected = *(address + 6) * 256 + *(address + 7);

    PFS_LOG(5, "checking block at %X: calculated CRC=%04X, expected CRC=%04X or %04X\r\n", address, crc, crcExpected, (uint16_t)~crcExpected);

    if (crc == crcExpected) return true;
    if (crc == (uint16_t)~crcExpected) return isDeleted();
    return false;
  }

  bool FlashBlock::isDeleted() const
  {
    if (address == nullptr) return false;
    bool hasSubtype = getType() >= 0x80;
    uint8_t offset = hasSubtype ? 2 : 1;
    for (uint8_t i = offset; i < 6; ++i)
      if (*(address + i) != 0xFF)
      {
        PFS_LOG(5, "block at %X is not deleted because byte %d is not 0xFF\r\n", address, i);
        return false;
      }
    uint16_t crc = CRC::crc16(address, 6);
    crc = ~crc;
    bool result = (*(address + 6) == (crc >> 8)) && (*(address + 7) == (crc & 0xFF));

    if (FlashStorage::LogLevel >= 5)
    {
      if (!result)
      {
        PFS_LOG(5, "block at %X is not a deleted block because CRC does not match: expected %04X but found %04X\r\n", address, crc, *(address + 6) * 256 + *(address + 7));
      }
      else
      {
        PFS_LOG(5, "block at %X is a deleted block\r\n", address);
      }
    }
    return result;
  }

  uint8_t FlashBlock::getType() const
  {
    if (address == nullptr) return 0;
    return *address;
  }

  uint8_t FlashBlock::getSubtype() const
  {
    if (address == nullptr) return 0;
    return (*address >= 0x80) ? *(address + 1) : 0;
  }

  const uint8_t* FlashBlock::getAddress() const
  {
    return address;
  }
  
  bool FlashBlock::getData(uint8_t* dest, uint8_t srcOffset, uint8_t length) const
  {
    bool hasSubtype = getType() >= 0x80;
    if ((srcOffset + length) > (hasSubtype ? 4 : 5))
      return false;
    srcOffset += hasSubtype ? 2 : 1;
    memcpy(dest, getAddress()+srcOffset, length);
    return true;
  }

  uint8_t FlashBlock::getData(uint8_t offset) const
  {
    uint8_t value;
    getData(&value, offset, 1);
    return value;
  }  

  uint16_t FlashBlock::getWord(uint8_t offset) const
  {
    uint16_t value;
    getData((uint8_t*)&value, offset, 2);
    return value;
  }  
  
  uint32_t FlashBlock::getLong(uint8_t offset) const
  {
    uint32_t value;
    getData((uint8_t*)&value, offset, 4);
    return value;
  }  
  
  
  bool FlashBlock::matchesType(uint8_t type, uint8_t subType) const
  {
    return getType() == type && (type < 0x80 || getSubtype() == subType) && isValid();
  }

  // FlashWriteBlock

  FlashWriteBlock::FlashWriteBlock(int8_t blockType)
  {
    newData = (uint8_t*)malloc(8);
    memset(newData, 0xff, 8);
    *newData = blockType;
  }

  FlashWriteBlock::FlashWriteBlock(int8_t blockType, uint8_t subType)
  {
    newData = (uint8_t*)malloc(8);
    *newData = blockType;
    *(newData + 1) = subType;
  }

  FlashWriteBlock::FlashWriteBlock(const FlashBlock& fb)
  {
    newData = (uint8_t*)malloc(8);
    memcpy(newData, fb.getAddress(), 8);
  }

  uint8_t FlashWriteBlock::getType() const
  {
    return *newData;
  }

  uint8_t FlashWriteBlock::getSubtype() const
  {
    return (*newData >= 0x80) ? *(newData + 1) : 0;
  }

  bool FlashWriteBlock::setData(const uint8_t* source, uint8_t offset, uint8_t length)
  {
    bool hasSubtype = (*newData) >= 0x80;
    if ((offset + length) > (hasSubtype ? 4 : 5))
      return false;
    offset += hasSubtype ? 2 : 1;
    memcpy((uint8_t*)(newData + offset), source, length);
    return true;
  }

  bool FlashWriteBlock::setData(uint8_t value, uint8_t offset)
  {
    return setData(&value, offset, 1);
  }

  bool FlashWriteBlock::setWord(uint16_t value, uint8_t offset)
  {
    return setData((uint8_t*)&value, offset, 2);
  }

    bool FlashWriteBlock::setLong(uint32_t value, uint8_t offset)
  {
    return setData((uint8_t*)&value, offset, 4);
  }

  uint8_t FlashWriteBlock::setBlockSubtype(uint8_t value)
  {
    if (newData == nullptr) newData = (uint8_t*)malloc(8);
    return newData[1] = value;
  }

  uint8_t* FlashWriteBlock::getBuffer() const
  {
    return newData;
  }

  bool FlashWriteBlock::isDeleted() const
  {
    return isDeletedFlag;
  }

  void FlashWriteBlock::setIsDeleted(bool value)
  {
    isDeletedFlag = value;
  }

  void FlashWriteBlock::setCRC()
  {
    auto crc = CRC::crc16(newData, 6);
    if (isDeletedFlag) crc = ~crc;
    newData[6] = crc >> 8;
    newData[7] = crc & 0xff;
    PFS_LOG(5, "set CRC for block type %d/%d: crc=%04X, isDeleted = %d\r\n", getType(), getSubtype(), crc, isDeletedFlag);
  }

  bool FlashWriteBlock::matches(const FlashBlock* flashBlock) const
  {
    return memcmp(newData, flashBlock->getAddress(), 8) == 0;
  }

  FlashWriteBlock::~FlashWriteBlock()
  {
    free(newData);
  }

  // IndexedFlashBlock

  IndexedFlashBlock::IndexedFlashBlock(uint8_t* address, int16_t sector, int16_t block)
    : FlashBlock(address), sector(sector), block(block)
  {
  }

  int16_t IndexedFlashBlock::getSector() const { return sector; }
  void IndexedFlashBlock::setSector(int16_t value) { sector = value; }
  int16_t IndexedFlashBlock::getBlock() const { return block; }
  void IndexedFlashBlock::setBlock(int16_t value) { block = value; }
}