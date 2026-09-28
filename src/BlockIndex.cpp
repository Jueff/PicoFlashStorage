/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
*/

#include "BlockIndex.h"

namespace PicoFlashStorage
{

  BlockIndex::BlockIndex(FlashStorage* fs)
    : fs(fs)
  {
    buildIndex();
  }

  BlockIndex::~BlockIndex()
  {
  }

  int BlockIndex::getCount() const
  {
    return static_cast<int>(entries.size());
  }

  const BlockIndex::Entry* BlockIndex::getEntry(int idx) const
  {
    if (idx < 0 || idx >= static_cast<int>(entries.size())) return nullptr;
    return &entries[idx];
  }

  const BlockIndex::Entry* BlockIndex::find(uint8_t type, uint8_t subtype) const
  {
    for (size_t i = 0; i < entries.size(); ++i)
    {
      if (entries[i].type == type && entries[i].subtype == subtype)
        return &entries[i];
    }
    return nullptr;
  }

  /**
   * @brief Builds the index by scanning all sectors and blocks (newest first).
   * For each type/subtype, the newest valid block decides:
   * - not deleted -> included in the index
   * - deleted     -> omitted (older live copies of the same key are ignored)
   * Deleted blocks are never delivered by the index.
   */
  void BlockIndex::buildIndex()
  {
    entries.clear();
    if (!fs) return;

    // Keys already decided from a newer valid block (live or deleted).
    std::vector<std::pair<uint8_t, uint8_t>> decided;

    auto alreadyDecided = [&](uint8_t type, uint8_t subtype) -> bool
    {
      for (const auto& key : decided)
      {
        if (key.first == type && key.second == subtype) return true;
      }
      return false;
    };

    const int16_t sectorCount = fs->getSectorsCount();
    for (int16_t i = sectorCount - 1; i >= 0; --i)
    {
      const SecureSector* sector = fs->getSector(i);
      if (!sector) continue;

      for (int16_t j = sector->getFirstFreeBlock() - 1; j >= 0; --j)
      {
        uint8_t* addr = sector->getBlockAddress(j);
        FlashBlock fb(addr);

        // isValid includes live blocks and deleted tombstones.
        if (!fb.isValid()) continue;

        const uint8_t type = fb.getType();
        const uint8_t subtype = (type >= 0x80) ? fb.getSubtype() : 0;
        if (alreadyDecided(type, subtype)) continue;

        decided.push_back({type, subtype});

        if (fb.isDeleted())
        {
          PFS_LOG(5, "skip deleted block type %d/%d at sector %d block %d\r\n",
                  type, subtype, i, j);
          continue;
        }

        entries.push_back({type, subtype, IndexedFlashBlock(addr, i, j)});
        PFS_LOG(5, "indexed block type %d/%d at sector %d block %d\r\n", type, subtype, i, j);
      }
    }
  }
}
