#pragma once

#include <cstdint>
#include <cstring>
extern "C" {
#include <hardware/sync.h>
#include <hardware/flash.h>
};

#include "FlashBlock.h"
#include "FlashStorageConfig.h"

namespace PicoFlashStorage {

  class SecureSector
  {
  private:
    uint8_t   identity[8];
    int       eraseCount;
    uint16_t  sectorNumber;
    bool      headerValid = false;
    uint8_t*  buffer;

  public:
    ~SecureSector();
    SecureSector(uint16_t sectorNumber, const uint8_t identity[8]);
    static constexpr int MaxEraseCount = 0x00FFFFFF;
    static int nextEraseCount(int eraseCount);

    bool isHeaderValid() const;
    int getEraseCount() const;
    uint16_t getSectorNumber() const;
    bool checkFormat();
    bool format(int eraseCount);
    bool write(FlashWriteBlock* block);
    bool hasFreeBlock() const;
    uint16_t getFirstFreeBlock() const;
    uint8_t* getBlockAddress(uint16_t blockIndex) const;
    uint8_t* getPageAddress(uint16_t blockIndex) const;
    uint16_t getMaxBlockCount() const;
    uint16_t getFreeMemoryStartOffset() const;
    bool isEmpty() const;
    void setCRC(uint16_t offset, uint16_t length);

#if FSC_LEVEL>0
    void dump() const;
    void dumpPage(uint8_t pageId) const;
    void dumpBuffer() const;
#endif
  };
}