/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
*/

#pragma once

#include "FlashBlock.h"
#include "PicoFlashStorage.h"

namespace PicoFlashStorage {

  class BlockIndex {
  public:
    struct Entry {
      uint8_t type;
      uint8_t subtype;
      IndexedFlashBlock block;
    };

    /**
     * @brief BlockIndex manages an index of flash blocks for fast lookup by type and subtype.
     * It builds an index from the flash storage and provides access to indexed entries.
     */
    BlockIndex(int maxEntries, FlashStorage* fs);

    /**
     * @brief Destructor. Releases memory allocated for the index entries.
     */
    ~BlockIndex();

    /**
     * @brief Returns the number of indexed entries.
     * @return Number of valid entries in the index.
     */
    int getCount() const;

    /**
     * @brief Returns a pointer to the entry at the given index.
     * @param idx Index of the entry (0 <= idx < getCount())
     * @return Pointer to Entry or nullptr if index is out of bounds.
     */
    const Entry* getEntry(int idx) const;

    /**
     * @brief Finds an entry by block type and subtype.
     * @param type Block type identifier.
     * @param subtype Block subtype identifier.
     * @return Pointer to Entry or nullptr if not found.
     */
    const Entry* find(uint8_t type, uint8_t subtype = 0) const;

  private:
    int maxEntries;
    int count;
    Entry* entries;
    FlashStorage* fs;

    void buildIndex(); // Nur Deklaration
  };
}