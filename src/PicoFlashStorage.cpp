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

namespace PicoFlashStorage
{

  uint8_t FlashStorage::LogLevel = 0;

// Constructor
  FlashStorage::FlashStorage(uint16_t baseSectorNumber, uint16_t sectorCount, const uint8_t identity[8])
    : pSectors(nullptr), baseSectorNumber(baseSectorNumber), sectorCount(sectorCount), maxEraseCount(0), signature(&identity[0])
  {
    // Wear-leveling needs at least one data sector + one reserved spare.
    if (this->sectorCount < 2)
    {
      PFS_LOG(1, "need at least 2 sectors for FlashStorage\r\n");
      this->sectorCount = 0;
      return;
    }

    pSectors = (SecureSector**)malloc(this->sectorCount * sizeof(SecureSector*));
    for (uint16_t i = 0; i < this->sectorCount; i++)
    {
      PFS_LOG(3, "creating sector %d\r\n", baseSectorNumber + i);
      pSectors[i] = new SecureSector(baseSectorNumber + i, signature);
      {
        PFS_LOG(5, "Header of sector %d is %s, eraseCount = %d, firstFreeBlock = %d\r\n", pSectors[i]->getSectorNumber(), pSectors[i]->isValid() ? "valid" : "invalid", pSectors[i]->getEraseCount(), pSectors[i]->getFirstFreeBlock());
        if (pSectors[i]->isValid())
        {
          maxEraseCount = std::max(pSectors[i]->getEraseCount(), maxEraseCount);
        }
      }
    }
    PFS_LOG(5, "Max erase count = %d\r\n", maxEraseCount);

    for (uint16_t i = 0; i < this->sectorCount; i++)
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
    if (!pSectors) return;
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      delete pSectors[i];
    }
    free(pSectors);
  }

  bool FlashStorage::write(FlashWriteBlock* block)
  {
    if (sectorCount < 2 || !pSectors)
    {
      PFS_LOG(1, "FlashStorage not usable (need >= 2 sectors)\r\n");
      return false;
    }

    block->setCRC();
    //dumpMemory(block->getBuffer(),8);
    FlashBlock fb;
    if (getBlock(fb, block->getType(), block->getSubtype()) && block->matches(&fb))
    {
      PFS_LOG(5, "data of block type %d/%d didn't change, don't save block\r\n", block->getType(), block->getSubtype());
      return true;
    }

    PFS_LOG(5, "data of block type %d/%d needs to be written\r\n", block->getType(), block->getSubtype());

    for (uint16_t i = 0; i < sectorCount - 1; i++)
    {
      if (pSectors[i]->hasFreeBlock() && pSectors[i]->write(block)) return true;
    }
    PFS_LOG(3, "no free block found to write type %d/%d\r\n", block->getType(), block->getSubtype());
    bool allBLocksFree = pSectors[sectorCount - 1]->getFirstFreeBlock() == 0;

    // Use BlockIndex to collect live blocks that must be preserved before reclaim
    BlockIndex index(this);
    std::vector<FlashWriteBlock*> blocksToPreserve;
    for (size_t i = 0; i < index.getCount(); ++i)
    {
      const auto& entry = *index.getEntry(i);

      // if we don't have the last sector free we need additionally to preserve any blocks from the last sector
      // sector(0) always needs to be preserved because it is the oldest block
      // sector(sectorCount-1) needs to be preserved if it is not empty, because it is the most recent block and we will erase it in the next step to get an additional free scetor
      if (entry.block.getSector() == 0 || (!allBLocksFree && entry.block.getSector() == sectorCount - 1))
      {
        blocksToPreserve.push_back(new FlashWriteBlock(entry.block));
        PFS_LOG(5, "will preserve block type %d/%d from sector %d block %d\r\n", entry.type, entry.subtype, entry.block.getSector(), entry.block.getBlock());
      }
    }
    if (!allBLocksFree)   
    {
      // this may happen in two cases: 
      // 1) the last preserve operation failed with power failure 
      // 2) the last sector is not reserved free because of update from previous implementation that didn't keep one free sector
      PFS_LOG(6, "Sorted sector list before freeing:\r\n");
      for (uint16_t i = 0; i < sectorCount; i++)
      {
        PFS_LOG(6, "pos %d sector %d eraseCount %d firstFreeBlock %d\r\n", i, pSectors[i]->getSectorNumber(), pSectors[i]->getEraseCount(), pSectors[i]->getFirstFreeBlock());
      }
      PFS_LOG(5, "freeing sector %d\r\n", sectorCount - 1);
      pSectors[sectorCount - 1]->format(SecureSector::nextEraseCount(pSectors[sectorCount - 1]->getEraseCount()));
    }

    bool result = true;
    for (const auto backup : blocksToPreserve)
    {
      PFS_LOG(5, "preserving block type %d/%d\r\n", backup->getType(), backup->getSubtype());
      result &= pSectors[sectorCount - 1]->write(backup);
      delete backup;
    }

    pSectors[0]->format(SecureSector::nextEraseCount(pSectors[sectorCount - 1]->getEraseCount()));
    sort();

    if (!result) return false;

    // Keep last sector reserved; use any writable sector with free space.
    for (uint16_t i = 0; i < sectorCount - 1; i++)
    {
      if (pSectors[i]->hasFreeBlock() && pSectors[i]->write(block)) return true;
    }
    return false;
  }

  int FlashStorage::getMaxEraseCount()
  {
    return maxEraseCount;
  }

  bool FlashStorage::isValid()
  {
    return sectorCount >= 2 && pSectors != nullptr;
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
    // Set all data bytes to 0xFF (deleted marker)
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
    PFS_LOG(6, "Sorted sector list:\r\n");
    for (uint16_t i = 0; i < sectorCount; i++)
    {
      PFS_LOG(6, "pos %d sector %d eraseCount %d firstFreeBlock %d\r\n", i, pSectors[i]->getSectorNumber(), pSectors[i]->getEraseCount(), pSectors[i]->getFirstFreeBlock());
    }
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
}
