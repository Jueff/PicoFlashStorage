/*
 * Copyright 2026 Juergen Winkler
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
 * SOFTWARE.* Testing/examples for reading/writing flash on the RP2040.
*/


extern "C" {
#include <hardware/sync.h>
#include <hardware/flash.h>
#include "crc16.h"
};

#include "FlashStorage.h"
#include "FlashBlock.h"
#include "BlockIndex.h"

using namespace PicoFlashStorage;

int* p;
unsigned int page; // prevent comparison of unsigned and signed int

BlockIndex* pBlockIndex = nullptr;

/*

Sector Layout

Sector has 4096 bytes
Header Offset 0, len = 16 byte
  - 8 byte Sector Identity - signature of data, e.g. MLLSERVO
  - 3 byte Erase count - incremented on every page erase - hint for number of previous erases
  - 3 bytes reserved (0)
  - 2 byte CRC

Block 0-509 Offset 16+Block*8
  - 1 byte block type: <0xf0
  - 5 byte block data
  - 2 byte block CRC
  or
  - 1 byte block type >=0xf0
  - 1 byte block subtype
  - 4 byte block data
  - 2 byte block CRC

Block types:
  Servo position block
  - 1 byte: type 0xf0
  - 1 byte: subtype 0x01
  - 1 byte: Pos#0
  - 1 byte: Pos#1
  - 1 byte: Pos#2
  - 1 byte: reserved (0)
  - 2 byte: CRC

  Servo settings block
  - 1 byte: type 0xA0
  - 1 byte: Server#
  - 1 byte: MinPos
  - 1 byte: MaxPos
  - 1 byte: Speed
  - 1 byte: options
  - 2 byte: CRC

*/



#define SECTORS_TO_USE 4

FlashStorage* pFS;

void setup() {

  Serial.begin(115200);
  // only enable for debugging purpose to see trace output of boot code
  while (!Serial) {}
  Serial.println("FLASH_PAGE_SIZE = " + String(FLASH_PAGE_SIZE, DEC));
  Serial.println("FLASH_SECTOR_SIZE = " + String(FLASH_SECTOR_SIZE, DEC));
  Serial.println("FLASH_BLOCK_SIZE = " + String(FLASH_BLOCK_SIZE, DEC));
  Serial.println("PICO_FLASH_SIZE_BYTES = " + String(PICO_FLASH_SIZE_BYTES, DEC));
  Serial.println("XIP_BASE = 0x" + String(XIP_BASE, HEX));

  uint16_t baseSectorNumber = (PICO_FLASH_SIZE_BYTES / FLASH_SECTOR_SIZE) - SECTORS_TO_USE;
  pFS = new FlashStorage(baseSectorNumber, SECTORS_TO_USE, (uint8_t*)"MLLSRV01");
  if (!pFS->isValid())
  {
    Serial.println("can't use flash storage");
    pFS = NULL;
  }

  pBlockIndex = new BlockIndex(30, pFS);
  Serial.printf("BlockIndexCount: %d\n", pBlockIndex->getCount());
  for (int i = 0; i < pBlockIndex->getCount(); i++) {
    const BlockIndex::Entry* e = pBlockIndex->getEntry(i);
    Serial.printf("found block %d/%d at sector %d block %d\n", e->type, e->subtype, e->block.getSector(), e->block.getBlock());
  }
  Serial.printf("End of BlockIndex\n");

  if (1)
  {
    FlashWriteBlock fb(0x40);
    fb.setData(0x22, 0);
    fb.setData(0x33, 1);
    fb.setData(0x44, 2);
    fb.setData(0x55, 3);
    for (int i = 0; i < 1; i++)
    {
      fb.setData(0x00, 4);
      pFS->write(&fb);
      fb.setData(0x66, 4);
      pFS->write(&fb);
    }
  }

  FlashBlock fbs;
  if (pFS->getBlock(fbs, 0x40))
  {
    Serial.printf("found block at %X\r\n", fbs.getAddress());
  }
  else
  {
    Serial.printf("didn't find block for type %d\r\n", 0x40);
  }
  Serial.println("setup done.");

  ListBlocks();
  TestAll();
}

unsigned long lastMillis = 0;

void ListBlocks()
{
  // List crrently stored blocks
  Serial.println("Listing current blocks...");
  BlockIndex index(1000, pFS);
  for (int i = 0; i < index.getCount(); i++)
  {
    const BlockIndex::Entry* entry = index.getEntry(i);
    Serial.printf("Block Type: %d, Subtype: %d, Sector: %d, Block: %d\n",
      entry->type, entry->subtype, entry->block.getSector(), entry->block.getBlock());
  }
  Serial.println("End of block listing.");

}

bool TestWriteDelete()
{
  // Test: Write, delete and verify a block is not found anymore
  Serial.println("Test: Write, delete and verify block...");
  FlashWriteBlock testBlock(0x77);
  testBlock.setData(0x12, 0);
  testBlock.setData(0x34, 1);
  testBlock.setData(0x56, 2);
  testBlock.setData(0x78, 3);
  testBlock.setData(0x9A, 4);

  if (pFS->write(&testBlock)) {
    Serial.println("Block 0x77 written successfully.");
  }
  else {
    Serial.println("Block 0x77 could not be written.");
    return false;
  }

  // Check if the block can be found
  FlashBlock foundBlock;
  bool found = pFS->getBlock(foundBlock, 0x77);
  if (found) {
    Serial.println("Block 0x77 found (before delete).");
  }
  else {
    Serial.println("Block 0x77 not found (before delete)!");
    return false;
  }

  // Delete the block
  Serial.println("deleting Block 0x77");

  if (pFS->deleteBlock(0x77)) {
    Serial.println("Block 0x77 deleted successfully.");
  }
  else {
    Serial.println("Block 0x77 could not be deleted.");
    return false;
  }

  // Check if the block is still found after deletion
  found = pFS->getBlock(foundBlock, 0x77);
  if (!found) {
    Serial.println("Block 0x77 correctly not found after deletion.");
  }
  else {
    Serial.println("Error: Block 0x77 still found after deletion!");
    return false;
  }
  return true;
}

bool TestBlockRealloction()
{
  bool error = false;

  // Expected data for comparison blocks
  uint8_t expected40[6] = { 0x40, 0x22, 0x33, 0x44, 0x55, 0x66 };
  uint8_t expectedF3[6] = { 0xf3, 0xaa, 0x11, 0x22, 0x33, 0x44 };
  uint8_t expected50[6] = { 0x50, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };

  // Write block 0xf3 with subtype 0xaa
  FlashWriteBlock blockF3(0xf3, 0xaa);
  blockF3.setData(0x11, 0);
  blockF3.setData(0x22, 1);
  blockF3.setData(0x33, 2);
  blockF3.setData(0x44, 3);
  pFS->write(&blockF3);

  // Write block 0x40
  FlashWriteBlock writeBlock(0x40);
  writeBlock.setData(0x22, 0);
  writeBlock.setData(0x33, 1);
  writeBlock.setData(0x44, 2);
  writeBlock.setData(0x55, 3);
  writeBlock.setData(0x66, 4);
  pFS->write(&writeBlock);

  // Initialize block 0x50 (will be overwritten in the loop)
  FlashWriteBlock fullBlock(0x50);
  fullBlock.setData(0xAA, 0);
  fullBlock.setData(0xBB, 1);
  fullBlock.setData(0xCC, 2);
  fullBlock.setData(0xDD, 3);
  fullBlock.setData(0xEE, 4);

  // 10000 loop: Write many blocks and check values after each write
  for (int i = 0; i < 10000; i++)
  {
    if (i % 500 == 0)
    {
      Serial.printf("TestBlockReallocation progress: cycle %d\n", i);
    }

    if (!pFS->write(&fullBlock))
    {
      Serial.println("Memory full, no free blocks available.");
      break;
    }

    // Check values after each write
    FlashBlock block40, blockF3r, block50;
    bool ok40 = pFS->getBlock(block40, 0x40);
    bool okF3 = pFS->getBlock(blockF3r, 0xf3, 0xaa);
    bool ok50 = pFS->getBlock(block50, 0x50);


    // Check block 0x40
    if (ok40) {
      const uint8_t* addr = block40.getAddress();
      for (int j = 0; j < 6; j++) {
        if (addr[j] != expected40[j]) {
          Serial.printf("Error in block 0x40 at byte %d: read=0x%02X expected=0x%02X\n", j, addr[j], expected40[j]);
          error = true;
          break;
        }
      }
    }
    else {
      Serial.println("Block 0x40 not found!");
      error = true;
    }

    // Check block 0xf3/0xaa
    if (okF3) {
      const uint8_t* addr = blockF3r.getAddress();
      for (int j = 0; j < 6; j++) {
        if (addr[j] != expectedF3[j]) {
          Serial.printf("Error in block 0xf3/0xaa at byte %d: read=0x%02X expected=0x%02X\n", j, addr[j], expectedF3[j]);
          error = true;
          break;
        }
      }
    }
    else {
      Serial.println("Block 0xf3/0xaa not found!");
      error = true;
    }

    // Check block 0x50 (data changes each loop)
    if (ok50) {
      const uint8_t* addr = block50.getAddress();
      for (int j = 0; j < 6; j++) {
        if (addr[j] != expected50[j]) {
          Serial.printf("Error in block 0x50 at byte %d: read=0x%02X expected=0x%02X\n", j, addr[j], expected50[j]);
          error = true;
          break;
        }
      }
    }
    else {
      Serial.println("Block 0x50 not found!");
      error = true;
    }

    // Change data for next write to avoid duplicates
    fullBlock.setData((uint8_t)(0xAA + ((i + 1) & 0xFF)), 0);
    expected50[1] = (uint8_t)(0xAA + ((i + 1) & 0xFF));

    // Break loop if error found
    if (error) break;
  }
  return !error;
}

void TestAll()
{
  Serial.println("Starting TestAll...");

  if (!TestWriteDelete())
  {
    Serial.println("TestWriteDelete failed, aborting TestAll.");
    return;
  }

  if (!TestBlockRealloction())
  {
    Serial.println("TestBlockReallocation failed, aborting TestAll.");
    return;
  }

  Serial.println("TestAll finished.");
}

void loop()
{
  if ((millis() - lastMillis) > 1000)
  {
    lastMillis = millis();

    if (false)
    {
      FlashWriteBlock fb(0x40);
      fb.setData(0x22, 0);
      fb.setData(0x33, 1);
      fb.setData(0x44, 2);
      fb.setData(0x55, 3);
      for (int i = 0; i < 4; i++)
      {
        fb.setData(0x00, 4);
        pFS->write(&fb);
        fb.setData(0x66, 4);
        pFS->write(&fb);
      }
    }
  }
}
