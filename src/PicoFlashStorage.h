#pragma once

/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * FlashStorage Library for RP2040
 * 
 * A wear-leveling flash storage library for persistent data storage on RP2040 microcontrollers.
 * Manages multiple flash sectors with automatic wear leveling, CRC data integrity checking,
 * and block-based data organization for reliable long-term storage of configuration data,
 * counters, and other persistent information.
 * 
*/



extern "C" {
#include <hardware/sync.h>
#include <hardware/flash.h>
};
#include "SecureSector.h"
#include "Arduino.h"


namespace PicoFlashStorage {

  #define PFS_LOG(level,...) if (FlashStorage::LogLevel>=level) Serial.printf(__VA_ARGS__);

  class FlashStorage
  {
  private:
    SecureSector** pSectors;
    uint16_t baseSectorNumber;
    uint16_t sectorCount;
    int maxEraseCount;
    int currentUpdateCounter;

    const uint8_t* signature;
    uint8_t buf[FLASH_PAGE_SIZE];

    void dumpBuffer(uint8_t* address) const;
    void sort();

    unsigned long set_crc();
    bool check_crc(uint8_t* address);
    int16_t findCurrentPage();
    bool saveToPage(uint16_t page);
    bool isEmpty(uint16_t pageId);
    bool initBuffers();
    void checkFormat();

  public:
    /**
     * @brief Constructor - Initializes the flash storage system with multiple sectors
     * @param baseSectorNumber Starting sector number for flash storage
     * @param sectorCount Number of sectors to manage
     * @param identity 8-byte unique identifier for this storage instance
     */
    FlashStorage(uint16_t baseSectorNumber, uint16_t sectorCount, const uint8_t identity[8]);

    /**
     * @brief Destructor - Cleans up allocated memory for sectors
     */
    ~FlashStorage();

    /**
     * @brief Writes a data block to flash with wear leveling and deduplication
     * @param block Pointer to FlashWriteBlock containing data to write
     * @return true if write successful, false if failed or no space available
     */
    bool write(FlashWriteBlock* block);

    /**
     * @brief Gets the maximum erase count across all sectors (wear level indicator)
     * @return Maximum number of erase cycles performed on any sector
     */
    int getMaxEraseCount();

    /**
     * @brief Checks if the flash storage system is valid and ready to use
     * @return true if storage is initialized and has valid sectors
     */
    bool isValid();

    /**
     * @brief Gets the total number of managed sectors
     * @return Number of sectors in this storage instance
     */
    uint8_t getSectorsCount();

    /**
     * @brief Gets a pointer to a specific sector for inspection
     * @param index Sector index (0 to getSectorsCount()-1)
     * @return Pointer to SecureSector object, or nullptr if invalid index
     */
    const SecureSector* getSector(uint8_t index);

    /**
     * @brief Retrieves the most recent block of specified type from flash
     * @param block Reference to FlashBlock that will be populated with found data
     * @param type Block type identifier (0-255)
     * @param subType Block subtype identifier (used when type >= 0x80)
     * @return true if block found and loaded, false if no matching block exists
     */
    bool getBlock(FlashBlock& block, uint8_t type, uint8_t subType = 0);

    /**
     * deletes a block of given type.
     */
    bool deleteBlock(uint8_t type);

    /**
     * deletes a block of given type and subtype.
     */
    bool deleteBlock(uint8_t type, uint8_t subtype);

    /**
     * @brief Dumps the contents of a specific sector to serial output for debugging
     * @param sectorId Sector number to dump
     */
    void dumpSector(uint16_t sectorId);

    /**
     * @brief Dumps a memory region in hex format to serial output for debugging
     * @param address Starting address of memory to dump
     * @param size Number of bytes to dump
     */
    static void dumpMemory(const uint8_t* address, uint16_t size);

    /**
     * @brief Sets the log level for debug output (0 = none, higher values increase verbosity)
     * @param level Log level to set
     */
    static constexpr int ReservedPages = 32;

    /**
     * @brief Sets the log level for debug output (0 = none, higher values increase verbosity)
     * @param level Log level to set
     */
    static int flashTargetOffset;

    /**
     * @brief Sets the log level for debug output (0 = none, higher values increase verbosity)
     * @param level Log level to set
     */
    static uint8_t LogLevel;
  };
}