/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * 
*/

#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include "PicoFlashStorage.h"
#include "BlockIndex.h"
#include "crc16.h"

namespace PicoFlashStorage {

  // Initialize static member
  int     FlashStorage::flashTargetOffset = PICO_FLASH_SIZE_BYTES - FLASH_PAGE_SIZE * FlashStorage::ReservedPages;
  uint8_t FlashStorage::LogLevel = 0;

// Konstruktor
  FlashStorage::FlashStorage(uint16_t baseSectorNumber, uint16_t sectorCount, const uint8_t identity[8])
    : baseSectorNumber(baseSectorNumber), sectorCount(sectorCount), maxEraseCount(0), currentUpdateCounter(-1), signature(&identity[0])
  {
    pSectors = (SecureSector**)malloc(sectorCount * sizeof(SecureSector));
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      PFS_LOG(3, "creating sector %d\r\n", baseSectorNumber + i);
      pSectors[i] = new SecureSector(baseSectorNumber + i, signature);
      {
        PFS_LOG(5, "Header of sector %d is valid, eraseCount = %d, firstFreeBlock = %d\r\n", pSectors[i]->getSectorNumber(), pSectors[i]->getEraseCount(), pSectors[i]->getFirstFreeBlock());
        maxEraseCount = std::max(pSectors[i]->getEraseCount(), maxEraseCount);
      }
    }
    PFS_LOG(5, "Max erase count = %d\r\n", maxEraseCount);

    for (uint16_t i = 0; i < sectorCount; i++)
    {
      if (!pSectors[i]->isHeaderValid())
      {
        maxEraseCount = SecureSector::nextEraseCount(maxEraseCount);
        if (pSectors[i]->format(maxEraseCount))
        {
          PFS_LOG(4, "format of sector %d with eraseCount %d is ok\r\n", i, maxEraseCount);
        }
        else
        {
          PFS_LOG(1, "format of sector % d with eraseCount % d failed\r\n", i, maxEraseCount);
          pSectors[i] = NULL;
        }
      }
    }
    sort();
  }

  FlashStorage::~FlashStorage()
  {
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      delete pSectors[i];
    }
    free(pSectors);
  }

  bool FlashStorage::write(FlashWriteBlock* block)
  {
    block->setCRC();
    //dumpMemory(block->getBuffer(),8);
    FlashBlock fb;
    if (getBlock(fb, block->getType(), block->getSubtype()) && block->matches(&fb))
    {
      PFS_LOG(5, "data of block type %d/%d didn't change, don't save block\r\n", block->getType(), block->getSubtype());
      return true;
    }

    PFS_LOG(5, "data of block type %d/%d needs to be written\r\n", block->getType(), block->getSubtype());

    for (uint16_t i = 0; i < sectorCount; i++)
    {
      if (pSectors[i]->hasFreeBlock() && pSectors[i]->write(block)) return true;
    }
    PFS_LOG(3, "no free block found to write type %d/%d\r\n", block->getType(), block->getSubtype());

    // BlockIndex verwenden, um zu sichern
    BlockIndex index(sectorCount, this);
    std::vector<FlashWriteBlock*> blocksToPreserve;
    for (int i = 0; i < index.getCount(); ++i) {
      const auto& entry = *index.getEntry(i);
      if (entry.block.getSector() == 0) {
        blocksToPreserve.push_back(new FlashWriteBlock(entry.block));
        PFS_LOG(5, "will preserve block type %d/%d from sector %d block %d\r\n", entry.type, entry.subtype, entry.block.getSector(), entry.block.getBlock());
      }
    }

    pSectors[0]->format(SecureSector::nextEraseCount(pSectors[sectorCount - 1]->getEraseCount()));
    sort();

    SecureSector* newSector = pSectors[sectorCount - 1];
    bool result = true;
    for (const auto backup : blocksToPreserve) {
      PFS_LOG(5, "preserving block type %d/%d\r\n", backup->getType(), backup->getSubtype());
      result &= write(backup);
      delete backup;
    }
    if (result) if (newSector->hasFreeBlock() && newSector->write(block)) return true;
    return false;
  }

  int FlashStorage::getMaxEraseCount()
  {
    return maxEraseCount;
  }

  bool FlashStorage::isValid()
  {
    return sectorCount > 0;
  }

  uint8_t FlashStorage::getSectorsCount()
  {
    return sectorCount;
  }

  const SecureSector* FlashStorage::getSector(uint8_t index)
  {
    return pSectors[index];
  }

  bool FlashStorage::getBlock(FlashBlock& block, uint8_t type, uint8_t subType)
  {
    bool hasSubtype = type >= 0x80;
    for (int16_t i = sectorCount - 1; i >= 0; i--)
    {
      for (int16_t j = pSectors[i]->getFirstFreeBlock() - 1; j >= 0; j--)
      {
        const byte* address = pSectors[i]->getBlockAddress(j);
        FlashBlock fb(address);

        if (fb.matchesType(type, subType))
        {
          if (fb.isDeleted())
          {
            PFS_LOG(2, "found deleted block of type %d/%d at sector %d block %d\r\n", type, subType, i, j);
            return false;
          }

          // found a corrupted block, don't continue lookup because we must inform user about corrupted data
          if (!fb.isValid())
          {
            PFS_LOG(2, "found corrupted block of type %d/%d at sector %d block %d\r\n", type, subType, i, j);
            return false;
          }

          PFS_LOG(5, "found block of type %d/%d at sector %d block %d, ffb = %d\r\n", type, subType, i, j, pSectors[i]->getFirstFreeBlock());
          block.setAddress(address);
          return true;
        }
      }
    }
    return false;
  }

  bool FlashStorage::deleteBlock(uint8_t type)
  {
    return deleteBlock(type, type >= 0x80 ? 00 : 0xff);
  }

  bool FlashStorage::deleteBlock(uint8_t type, uint8_t subtype)
  {
    FlashWriteBlock delBlock(type, subtype);
    delBlock.setIsDeleted(true);
    // Setze alle Datenbytes auf 0xFF (gelöscht)
    memset(delBlock.getBuffer() + 2, 0xFF, 6);
    return write(&delBlock);
  }

  void FlashStorage::dumpSector(uint16_t sectorId)
  {
#if FSC_LEVEL>0
    if (sectorId < sectorCount && pSectors[sectorId] != NULL) pSectors[sectorId]->dump();
#endif
  }

  /**
   * Sorts the sector list by erase count, handling 24-bit overflow.
   * If an erase count overflow occurs (e.g. erase counts wrap from max to 0),
   * the youngest sector (with erase count 0) will be placed first in the list,
   * followed by older sectors. This ensures correct wear-leveling and block allocation order.
   * Sectors with erase counts near SecureSector::MaxEraseCount are considered oldest, and those near 0 are youngest.
   */
  void FlashStorage::sort()
  {
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      PFS_LOG(6, "before: pos %d sector %d eraseCount %d firstFreeBlock %d\r\n", i, pSectors[i]->getSectorNumber(), pSectors[i]->getEraseCount(), pSectors[i]->getFirstFreeBlock());
    }

    SecureSector* sortedSectors[sectorCount];
    memset(&sortedSectors[0], 0, sizeof(sortedSectors));
    uint8_t badSectors = 0;

    bool isHighBlock = false;
    bool isLowBlock = false;

    // Detect overflow: if there are both very high and very low erase counts
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      // Sector near end of life cycle (oldest)
      if (pSectors[i] != NULL && pSectors[i]->getEraseCount() == SecureSector::MaxEraseCount) isHighBlock = true;
      // Sector just erased (youngest)
      if (pSectors[i] != NULL && pSectors[i]->getEraseCount() == 0) isLowBlock = true;
    }
    bool isOverflow = isHighBlock && isLowBlock;

    if (isOverflow)
    {
      PFS_LOG(4, "Erase count overflow detected, sorting sectors with overflow handling");
    }

    // Sort sectors: youngest first, oldest last (handles overflow)
    for (uint16_t j = 0; j < sectorCount; j++)
    {
      int minErasecount = 0x0fffffff;
      int8_t index = -1;
      for (uint16_t i = 0; i < sectorCount; i++)
      {
        if (pSectors[i] != NULL)
        {
          int eraseCount = pSectors[i]->getEraseCount();
          // If overflow detected, treat low erase counts as highest
          if (isOverflow && eraseCount < (SecureSector::MaxEraseCount - sectorCount)) eraseCount += (SecureSector::MaxEraseCount + 1);
          if (eraseCount < minErasecount)
          {
            minErasecount = eraseCount;
            index = i;
          }
        }
      }
      if (index != -1)
      {
        sortedSectors[j] = pSectors[index];
        pSectors[index] = NULL;
      }
      else
      {
        badSectors++;
      }
    }
    memcpy(&pSectors[0], &sortedSectors[0], sizeof(sortedSectors));
    sectorCount -= badSectors;
    // dump sector list
    PFS_LOG(6, "Sorted sector list:");
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      PFS_LOG(6, "pos %d sector %d eraseCount %d firstFreeBlock %d\r\n", i, pSectors[i]->getSectorNumber(), pSectors[i]->getEraseCount(), pSectors[i]->getFirstFreeBlock());
    }
  }

  void FlashStorage::dumpBuffer(uint8_t* address) const
  {
    dumpMemory(address, FLASH_PAGE_SIZE);
  }

  void FlashStorage::dumpMemory(const uint8_t* address, uint16_t length)
  {
    for (int i = 0; i < length; i++)
    {
      uint8_t ch = *(address + i);
      Serial.printf("%02X ", ch);
      if (i % 32 == 31) Serial.println();
      else if (i % 8 == 7) Serial.print("- ");
    }
  }

  unsigned long FlashStorage::set_crc()
  {
    unsigned long crc = CRC::crc16(&buf[0], FLASH_PAGE_SIZE - 4);
    *(int*)(&buf[FLASH_PAGE_SIZE - 4]) = crc;
    return crc;
  }

  bool FlashStorage::check_crc(uint8_t* address)
  {
    unsigned long crc = CRC::crc16(address, FLASH_PAGE_SIZE - 4);
    unsigned long expected = *(unsigned long*)(address + FLASH_PAGE_SIZE - 4);
    return crc == expected;
  }

  int16_t FlashStorage::findCurrentPage()
  {
    for (int i = FlashStorage::ReservedPages - 1; i > 0; i--)
    {
      byte* addr = (byte*)XIP_BASE + FlashStorage::flashTargetOffset + i * FLASH_PAGE_SIZE;
      if (check_crc(addr))
      {
        return i;
      }
    }
    return 0;
  }

  bool FlashStorage::saveToPage(uint16_t page)
  {
    *(int*)(&buf) = currentUpdateCounter;
    unsigned long crc = set_crc();

    byte* addr = (byte*)XIP_BASE + FlashStorage::flashTargetOffset + page * FLASH_PAGE_SIZE;
    PFS_LOG(3, "writing data at %08X with updateCounter=%d to page %d with checksum %08X\r\n", addr, currentUpdateCounter, page, crc);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FlashStorage::flashTargetOffset + page * FLASH_PAGE_SIZE, &buf[0], FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    if (memcmp(addr, &buf[0], FLASH_PAGE_SIZE) != 0)
    {
      PFS_LOG(1, "flash page %d write failed\r\n", page);
      return false;
    }
    else
    {
      PFS_LOG(3, "flash page %d write ok\r\n", page);
      return true;
    }
  }

  bool FlashStorage::isEmpty(uint16_t pageId)
  {
    byte* addr = (byte*)XIP_BASE + FlashStorage::flashTargetOffset + pageId * FLASH_PAGE_SIZE;
    for (int index = 0; index < FLASH_PAGE_SIZE; index++)
    {
      if (*(addr + index) != 0xff)
      {
        return false;
      }
    }
    return true;
  }

  bool FlashStorage::initBuffers()
  {
    memset(&buf, 0, FLASH_PAGE_SIZE);
    memcpy(&buf, (const void*)signature, 8);
    set_crc();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FlashStorage::flashTargetOffset, FLASH_PAGE_SIZE * FlashStorage::ReservedPages);
    flash_range_program(FlashStorage::flashTargetOffset, &buf[0], FlashStorage::ReservedPages);
    restore_interrupts(ints);
    return true;
  }

  void FlashStorage::checkFormat()
  {
    byte* addr = (byte*)XIP_BASE + FlashStorage::flashTargetOffset;
    bool good = true;
    for (int i = 1; good && i < FlashStorage::ReservedPages; i++)
    {
      addr = (byte*)XIP_BASE + FlashStorage::flashTargetOffset + i * FLASH_PAGE_SIZE;
      if (!isEmpty(i) && !check_crc(addr))
      {
        PFS_LOG(3, "page %d not empty or CRC wrong\r\n", i);
        good = false;
      }
    }
    addr = (byte*)XIP_BASE + FlashStorage::flashTargetOffset;
    memset(&buf, 0, FLASH_PAGE_SIZE);
    memcpy(&buf, signature, 8);
    set_crc();

    if (!good || memcmp(addr, &buf[0], FLASH_PAGE_SIZE) != 0)
    {
      PFS_LOG(1, "flash isn't correctly initialized\r\n");
      initBuffers();
    }
    else
    {
      PFS_LOG(3, "flash correctly initialized\r\n");
    }
  }
}