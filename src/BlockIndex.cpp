/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
*/

#include "BlockIndex.h"

namespace PicoFlashStorage
{

  BlockIndex::BlockIndex(FlashStorage* fs)  // maxEntries entfernt
    : fs(fs)
  {
    buildIndex();
  }

  BlockIndex::~BlockIndex()
  {
  }

  int BlockIndex::getCount() const { return static_cast<int>(entries.size()); }  // Verwendet size() statt count

  const BlockIndex::Entry* BlockIndex::getEntry(int idx) const
  {
    if (idx < 0 || idx >= static_cast<int>(entries.size())) return nullptr;
    return &entries[idx];
  }

  const BlockIndex::Entry* BlockIndex::find(uint8_t type, uint8_t subtype) const
  {
    for (size_t i = 0; i < entries.size(); ++i) {  // size_t für Index
      if (entries[i].type == type && entries[i].subtype == subtype)
        return &entries[i];
    }
    return nullptr;
  }

  /**
   * @brief Builds the index by scanning all sectors and blocks in the flash storage.
   * Only valid, not deleted blocks are indexed. Duplicate type/subtype combinations are ignored.
   */
  void BlockIndex::buildIndex()
  {
    entries.clear();  // Vector leeren statt count = 0
    if (!fs) return;
    int16_t sectorCount = fs->getSectorsCount();
    for (int16_t i = sectorCount - 1; i >= 0; i--) 
    {
      const SecureSector* sector = fs->getSector(i);
      if (!sector) continue;
      int16_t maxBlock = sector->getFirstFreeBlock();
      for (int16_t j = maxBlock - 1; j >= 0; j--) 
      {
        uint8_t* addr = sector->getBlockAddress(j);
        FlashBlock fb(addr);
        if (!fb.isActive()) continue;
        uint8_t type = fb.getType();
        uint8_t subtype = (type >= 0x80) ? fb.getSubtype() : 0;
        bool found = false;
        for (size_t k = 0; k < entries.size(); ++k) 
        {  
          if (entries[k].type == type && entries[k].subtype == subtype) 
          {
            found = true;
            break;
          }
        }
        if (!found) 
        {  
          entries.push_back({type, subtype, IndexedFlashBlock(addr, i, j)});
          PFS_LOG(5, "indexed block type %d/%d at sector %d block %d\r\n", type, subtype, i, j);
        }
      }
    }
  }
}
