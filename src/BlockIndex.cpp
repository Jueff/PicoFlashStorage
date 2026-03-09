/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: MIT
 *
*/

#include "BlockIndex.h"

namespace PicoFlashStorage {

  BlockIndex::BlockIndex(int maxEntries, FlashStorage* fs)
    : maxEntries(maxEntries), fs(fs), count(0)
  {
    entries = new Entry[maxEntries];
    buildIndex();
  }

  BlockIndex::~BlockIndex() {
    delete[] entries;
  }

  int BlockIndex::getCount() const { return count; }

  const BlockIndex::Entry* BlockIndex::getEntry(int idx) const {
    if (idx < 0 || idx >= count) return nullptr;
    return &entries[idx];
  }

  const BlockIndex::Entry* BlockIndex::find(uint8_t type, uint8_t subtype) const {
    for (int i = 0; i < count; ++i) {
      if (entries[i].type == type && entries[i].subtype == subtype)
        return &entries[i];
    }
    return nullptr;
  }

  /**
   * @brief Builds the index by scanning all sectors and blocks in the flash storage.
   * Only valid blocks are indexed. Duplicate type/subtype combinations are ignored.
   */
  void BlockIndex::buildIndex() {
    count = 0;
    if (!fs) return;
    int16_t sectorCount = fs->getSectorsCount();
    for (int16_t i = sectorCount - 1; i >= 0; i--) {
      const SecureSector* sector = fs->getSector(i);
      if (!sector) continue;
      int16_t maxBlock = sector->getFirstFreeBlock();
      for (int16_t j = maxBlock - 1; j >= 0; j--) {
        uint8_t* addr = sector->getBlockAddress(j);
        FlashBlock fb(addr);
        if (!fb.isValid()) continue;
        uint8_t type = fb.getType();
        uint8_t subtype = (type >= 0x80) ? fb.getSubtype() : 0;
        bool found = false;
        for (int k = 0; k < count; ++k) {
          if (entries[k].type == type && entries[k].subtype == subtype) {
            found = true;
            break;
          }
        }
        if (!found && count < maxEntries) {
          entries[count].type = type;
          entries[count].subtype = subtype;
          entries[count].block = IndexedFlashBlock(addr, i, j);
          ++count;
        }
      }
    }
  }
}
