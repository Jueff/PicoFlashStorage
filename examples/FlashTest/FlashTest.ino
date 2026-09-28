/*
 * SPDX-FileCopyrightText: 2025-2026 Juergen Winkler <MobaLedLib@gmx.at>
 * SPDX-License-Identifier: MIT
 *
 * FlashTest – regression tests for PicoFlashStorage on RP2040.
 *
 * Intentionally stresses sector reclaim (buffer full → migrate live blocks).
 * Cases that previously failed / would catch the fixed bugs:
 *   - many unique types (> sector count) surviving reclaim
 *   - deleted blocks must NOT reappear as live 0xFF data after reclaim
 *   - mixed plain types and type/subtype pairs
 *   - Pico2x3-Tiny workload: rare settings + frequent position updates
 *     (MLLServoConfigurator + MLLSoundTiny block layout)
 */

extern "C" {
#include <hardware/sync.h>
#include <hardware/flash.h>
};

#include "PicoFlashStorage.h"
#include "BlockIndex.h"

using namespace PicoFlashStorage;

#define SECTORS_TO_USE 4

// Filler type used only to force sector reclaim (value changes every write).
static const uint8_t TYPE_FILLER = 0x50;

// Plain types (no subtype): more than SECTORS_TO_USE so a fixed maxEntries==sectorCount index fails.
static const uint8_t PLAIN_TYPES[] = {
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1A, 0x1B
};
static const int PLAIN_TYPE_COUNT = sizeof(PLAIN_TYPES) / sizeof(PLAIN_TYPES[0]);

// Typed blocks with subtypes (type >= 0x80).
static const uint8_t SUB_TYPE = 0xA0;
static const uint8_t SUBTYPES[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
static const int SUBTYPE_COUNT = sizeof(SUBTYPES) / sizeof(SUBTYPES[0]);

// ---- Pico2x3-Tiny / MLLServoConfigurator / MLLSoundTiny layout ----
// ServoConfigurator.cpp:
#define FLASHBLOCK_TYPE_MINMAX    0xF0
#define FLASHBLOCK_TYPE_SPEED     0xF1
#define FLASHBLOCK_TYPE_POSITION  0xF2
// SoundModule.cpp:
#define FLASHBLOCK_TYPE_MODULE_TYPE 0xEF
#define MODULE_MP3_TF_16P 0
#define MODULE_JQ6500     1

// Pico2x3.ino: setupServos(..., basePin=0), setupSoundModules(..., basePin=3)
static const uint8_t PICO2X3_SERVO_COUNT = 3;
static const uint8_t PICO2X3_SERVO_OFFSET = 0;   // subtypes 0..2
static const uint8_t PICO2X3_SOUND_COUNT = 3;
static const uint8_t PICO2X3_SOUND_BASE_PIN = 3; // subtypes 3..5

FlashStorage* pFS = nullptr;

static int gPass = 0;
static int gFail = 0;

static void expectTrue(bool cond, const char* msg)
{
  if (cond) {
    gPass++;
  } else {
    gFail++;
    Serial.printf("FAIL: %s\n", msg);
  }
}

static void expectEqualU8(uint8_t actual, uint8_t expected, const char* msg)
{
  if (actual == expected)
  {
    gPass++;
  }
  else
  {
    gFail++;
    Serial.printf("FAIL: %s (got 0x%02X expected 0x%02X)\n", msg, actual, expected);
  }
}

static void expectEqualU16(uint16_t actual, uint16_t expected, const char* msg)
{
  if (actual == expected)
  {
    gPass++;
  }
  else
  {
    gFail++;
    Serial.printf("FAIL: %s (got %u expected %u)\n", msg, actual, expected);
  }
}

static bool writePlain(uint8_t type, const uint8_t data[5])
{
  FlashWriteBlock wb(type);
  for (uint8_t i = 0; i < 5; i++) {
    if (!wb.setData(data[i], i)) return false;
  }
  return pFS->write(&wb);
}

static bool writeSub(uint8_t type, uint8_t subtype, const uint8_t data[4])
{
  FlashWriteBlock wb(type, subtype);
  for (uint8_t i = 0; i < 4; i++) {
    if (!wb.setData(data[i], i)) return false;
  }
  return pFS->write(&wb);
}

static void fillPlainExpected(uint8_t type, uint8_t seed, uint8_t out[5])
{
  for (uint8_t i = 0; i < 5; i++) {
    out[i] = (uint8_t)(seed + type + i * 17);
  }
}

static void fillSubExpected(uint8_t subtype, uint8_t seed, uint8_t out[4])
{
  for (uint8_t i = 0; i < 4; i++) {
    out[i] = (uint8_t)(seed + subtype * 3 + i * 19);
  }
}

static bool verifyPlain(uint8_t type, const uint8_t expected[5], bool mustExist)
{
  FlashBlock fb;
  bool found = pFS->getBlock(fb, type);
  if (mustExist != found) {
    Serial.printf("plain 0x%02X: found=%d expected=%d\n", type, found, mustExist);
    return false;
  }
  if (!mustExist) return true;

  for (uint8_t i = 0; i < 5; i++) {
    uint8_t v = fb.getData(i);
    if (v != expected[i]) {
      Serial.printf("plain 0x%02X byte %u: got 0x%02X expected 0x%02X\n", type, i, v, expected[i]);
      return false;
    }
  }
  return true;
}

static bool verifySub(uint8_t type, uint8_t subtype, const uint8_t expected[4], bool mustExist)
{
  FlashBlock fb;
  bool found = pFS->getBlock(fb, type, subtype);
  if (mustExist != found) {
    Serial.printf("sub 0x%02X/0x%02X: found=%d expected=%d\n", type, subtype, found, mustExist);
    return false;
  }
  if (!mustExist) return true;

  for (uint8_t i = 0; i < 4; i++) {
    uint8_t v = fb.getData(i);
    if (v != expected[i]) {
      Serial.printf("sub 0x%02X/0x%02X byte %u: got 0x%02X expected 0x%02X\n",
                    type, subtype, i, v, expected[i]);
      return false;
    }
  }
  return true;
}

static bool forceReclaimCycles(int cycles, const char* label)
{
  FlashWriteBlock filler(TYPE_FILLER);
  filler.setData(0xAA, 0);
  filler.setData(0xBB, 1);
  filler.setData(0xCC, 2);
  filler.setData(0xDD, 3);
  filler.setData(0xEE, 4);

  for (int i = 0; i < cycles; i++) {
    if ((i % 500) == 0) {
      Serial.printf("%s reclaim progress: %d/%d\n", label, i, cycles);
    }
    filler.setData((uint8_t)(0xAA + (i & 0xFF)), 0);
    if (!pFS->write(&filler)) {
      Serial.printf("%s: write failed at cycle %d\n", label, i);
      return false;
    }
  }
  return true;
}

void ListBlocks()
{
  Serial.println("Listing current active blocks (BlockIndex)...");
  BlockIndex index(pFS);
  Serial.printf("BlockIndexCount: %u\n", (unsigned)index.getCount());
  for (size_t i = 0; i < index.getCount(); i++) {
    const BlockIndex::Entry* entry = index.getEntry(i);
    Serial.printf("  type=0x%02X subtype=0x%02X sector=%d block=%d\n",
                  entry->type, entry->subtype,
                  entry->block.getSector(), entry->block.getBlock());
  }
  Serial.println("End of block listing.");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

bool TestWriteDeletePlainAndSub()
{
  Serial.println("--- TestWriteDeletePlainAndSub ---");

  uint8_t plainData[5] = { 0x12, 0x34, 0x56, 0x78, 0x9A };
  expectTrue(writePlain(0x77, plainData), "write plain 0x77");
  expectTrue(verifyPlain(0x77, plainData, true), "plain 0x77 present before delete");
  expectTrue(pFS->deleteBlock(0x77), "delete plain 0x77");
  expectTrue(verifyPlain(0x77, plainData, false), "plain 0x77 gone after delete");

  uint8_t subData[4] = { 0x11, 0x22, 0x33, 0x44 };
  expectTrue(writeSub(0xF1, 0x05, subData), "write sub 0xF1/0x05");
  expectTrue(verifySub(0xF1, 0x05, subData, true), "sub present before delete");
  expectTrue(pFS->deleteBlock(0xF1, 0x05), "delete sub 0xF1/0x05");
  expectTrue(verifySub(0xF1, 0x05, subData, false), "sub gone after delete");

  // Other subtype of same type must remain independent.
  uint8_t subData2[4] = { 0x55, 0x66, 0x77, 0x88 };
  expectTrue(writeSub(0xF1, 0x06, subData2), "write sub 0xF1/0x06");
  expectTrue(pFS->deleteBlock(0xF1, 0x05), "delete already-missing subtype ok/no resurrect");
  expectTrue(verifySub(0xF1, 0x06, subData2, true), "other subtype still present");
  expectTrue(pFS->deleteBlock(0xF1, 0x06), "cleanup 0xF1/0x06");
  return true;
}

bool TestBlockIndexSkipsDeleted()
{
  Serial.println("--- TestBlockIndexSkipsDeleted ---");

  uint8_t data[5] = { 1, 2, 3, 4, 5 };
  expectTrue(writePlain(0x60, data), "write 0x60");
  expectTrue(writePlain(0x61, data), "write 0x61");
  expectTrue(pFS->deleteBlock(0x60), "delete 0x60");

  BlockIndex index(pFS);
  expectTrue(index.find(0x60) == nullptr, "BlockIndex must not list deleted 0x60");
  expectTrue(index.find(0x61) != nullptr, "BlockIndex must list active 0x61");

  expectTrue(pFS->deleteBlock(0x61), "cleanup 0x61");
  return true;
}

bool TestManyTypesSurviveReclaim()
{
  Serial.println("--- TestManyTypesSurviveReclaim ---");
  // Catches old bug: BlockIndex(maxEntries=sectorCount) silently dropped live keys.

  const uint8_t seed = 0x40;
  uint8_t plainExpected[PLAIN_TYPE_COUNT][5];
  uint8_t subExpected[SUBTYPE_COUNT][4];

  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    fillPlainExpected(PLAIN_TYPES[i], seed, plainExpected[i]);
    expectTrue(writePlain(PLAIN_TYPES[i], plainExpected[i]), "seed plain type");
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    fillSubExpected(SUBTYPES[i], seed, subExpected[i]);
    expectTrue(writeSub(SUB_TYPE, SUBTYPES[i], subExpected[i]), "seed subtype");
  }

  const int uniqueKeys = PLAIN_TYPE_COUNT + SUBTYPE_COUNT;
  expectTrue(uniqueKeys > SECTORS_TO_USE,
             "test setup: more unique keys than sectors");

  BlockIndex before(pFS);
  // Index should see all seeded keys (+ maybe leftover filler from earlier tests).
  int foundSeeded = 0;
  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    if (before.find(PLAIN_TYPES[i]) != nullptr) foundSeeded++;
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    if (before.find(SUB_TYPE, SUBTYPES[i]) != nullptr) foundSeeded++;
  }
  expectEqualU8((uint8_t)foundSeeded, (uint8_t)uniqueKeys, "all seeded keys indexed before reclaim");

  // Enough writes to force multiple sector erases/migrations.
  if (!forceReclaimCycles(3000, "ManyTypes")) return false;

  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    if (!verifyPlain(PLAIN_TYPES[i], plainExpected[i], true)) {
      expectTrue(false, "plain type lost/corrupt after reclaim");
      return false;
    }
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    if (!verifySub(SUB_TYPE, SUBTYPES[i], subExpected[i], true)) {
      expectTrue(false, "subtype lost/corrupt after reclaim");
      return false;
    }
  }

  Serial.printf("ManyTypes: %d unique keys survived reclaim\n", uniqueKeys);
  return true;
}

bool TestDeleteThenReclaimNoResurrection()
{
  Serial.println("--- TestDeleteThenReclaimNoResurrection ---");
  // Catches old bug: deleted tombstones migrated as live all-0xFF blocks.

  const uint8_t seed = 0x90;
  uint8_t plainExpected[PLAIN_TYPE_COUNT][5];
  uint8_t subExpected[SUBTYPE_COUNT][4];
  bool plainLive[PLAIN_TYPE_COUNT];
  bool subLive[SUBTYPE_COUNT];

  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    fillPlainExpected(PLAIN_TYPES[i], seed, plainExpected[i]);
    expectTrue(writePlain(PLAIN_TYPES[i], plainExpected[i]), "write plain before delete");
    plainLive[i] = true;
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    fillSubExpected(SUBTYPES[i], seed, subExpected[i]);
    expectTrue(writeSub(SUB_TYPE, SUBTYPES[i], subExpected[i]), "write sub before delete");
    subLive[i] = true;
  }

  // Delete every other plain type and every other subtype.
  for (int i = 0; i < PLAIN_TYPE_COUNT; i += 2) {
    expectTrue(pFS->deleteBlock(PLAIN_TYPES[i]), "delete plain");
    plainLive[i] = false;
  }
  for (int i = 0; i < SUBTYPE_COUNT; i += 2) {
    expectTrue(pFS->deleteBlock(SUB_TYPE, SUBTYPES[i]), "delete subtype");
    subLive[i] = false;
  }

  // Immediate check before reclaim.
  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    expectTrue(verifyPlain(PLAIN_TYPES[i], plainExpected[i], plainLive[i]),
               "plain state before reclaim");
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    expectTrue(verifySub(SUB_TYPE, SUBTYPES[i], subExpected[i], subLive[i]),
               "sub state before reclaim");
  }

  if (!forceReclaimCycles(4000, "DeleteReclaim")) return false;

  // After reclaim: live data intact, deleted must stay absent (not 0xFF ghosts).
  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    if (!verifyPlain(PLAIN_TYPES[i], plainExpected[i], plainLive[i])) {
      Serial.printf("post-reclaim plain 0x%02X failed (live=%d)\n",
                    PLAIN_TYPES[i], plainLive[i]);
      expectTrue(false, "delete/reclaim plain mismatch");
      return false;
    }
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    if (!verifySub(SUB_TYPE, SUBTYPES[i], subExpected[i], subLive[i])) {
      Serial.printf("post-reclaim sub 0x%02X/0x%02X failed (live=%d)\n",
                    SUB_TYPE, SUBTYPES[i], subLive[i]);
      expectTrue(false, "delete/reclaim subtype mismatch");
      return false;
    }
  }

  BlockIndex index(pFS);
  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    const bool inIndex = index.find(PLAIN_TYPES[i]) != nullptr;
    expectTrue(inIndex == plainLive[i], "BlockIndex plain live/deleted after reclaim");
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    const bool inIndex = index.find(SUB_TYPE, SUBTYPES[i]) != nullptr;
    expectTrue(inIndex == subLive[i], "BlockIndex subtype live/deleted after reclaim");
  }

  Serial.println("DeleteThenReclaim: no tombstone resurrection detected");
  return true;
}

bool TestUpdateAndDedup()
{
  Serial.println("--- TestUpdateAndDedup ---");

  uint8_t v1[5] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  uint8_t v2[5] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
  expectTrue(writePlain(0x41, v1), "write v1");
  expectTrue(verifyPlain(0x41, v1, true), "read v1");
  expectTrue(writePlain(0x41, v1), "rewrite identical (dedup path)");
  expectTrue(verifyPlain(0x41, v1, true), "still v1");
  expectTrue(writePlain(0x41, v2), "update to v2");
  expectTrue(verifyPlain(0x41, v2, true), "read v2");
  expectTrue(pFS->deleteBlock(0x41), "cleanup 0x41");
  return true;
}

bool TestMixedReallocationStress()
{
  Serial.println("--- TestMixedReallocationStress ---");
  // Keep a stable set of mixed keys while filler forces many reclamations.

  const uint8_t seed = 0x20;
  uint8_t plainExpected[PLAIN_TYPE_COUNT][5];
  uint8_t subExpected[SUBTYPE_COUNT][4];

  for (int i = 0; i < PLAIN_TYPE_COUNT; i++) {
    fillPlainExpected(PLAIN_TYPES[i], seed, plainExpected[i]);
    if (!writePlain(PLAIN_TYPES[i], plainExpected[i])) {
      expectTrue(false, "stress seed plain");
      return false;
    }
  }
  for (int i = 0; i < SUBTYPE_COUNT; i++) {
    fillSubExpected(SUBTYPES[i], seed, subExpected[i]);
    if (!writeSub(SUB_TYPE, SUBTYPES[i], subExpected[i])) {
      expectTrue(false, "stress seed sub");
      return false;
    }
  }

  FlashWriteBlock filler(TYPE_FILLER);
  filler.setData(0xAA, 0);
  filler.setData(0xBB, 1);
  filler.setData(0xCC, 2);
  filler.setData(0xDD, 3);
  filler.setData(0xEE, 4);

  const int cycles = 5000;
  for (int i = 0; i < cycles; i++) {
    if ((i % 500) == 0) {
      Serial.printf("MixedReallocation progress: %d/%d\n", i, cycles);
    }

    filler.setData((uint8_t)(0xAA + (i & 0xFF)), 0);
    if (!pFS->write(&filler)) {
      expectTrue(false, "filler write failed");
      return false;
    }

    // Verify periodically (every write early on, then every 50) to catch first bad migrate.
    if (i < 50 || (i % 50) == 0) {
      for (int t = 0; t < PLAIN_TYPE_COUNT; t++) {
        if (!verifyPlain(PLAIN_TYPES[t], plainExpected[t], true)) {
          Serial.printf("corrupt at cycle %d plain 0x%02X\n", i, PLAIN_TYPES[t]);
          expectTrue(false, "plain corrupt during stress");
          return false;
        }
      }
      for (int t = 0; t < SUBTYPE_COUNT; t++) {
        if (!verifySub(SUB_TYPE, SUBTYPES[t], subExpected[t], true)) {
          Serial.printf("corrupt at cycle %d sub 0x%02X/0x%02X\n", i, SUB_TYPE, SUBTYPES[t]);
          expectTrue(false, "subtype corrupt during stress");
          return false;
        }
      }
    }
  }

  Serial.println("MixedReallocation: completed without corruption");
  return true;
}

// Mirrors Pico2x3-Tiny persistence:
// - 3 servos: min/max (0xF0), speed (0xF1), position (0xF2) per channel subtype
// - 3 sound modules: module type (0xEF) at serial pins 3..5
// Positions are written often (like savePosition); settings must survive reclaim.
bool TestPico2x3Workload()
{
  Serial.println("--- TestPico2x3Workload ---");

  uint16_t minVal[PICO2X3_SERVO_COUNT];
  uint16_t maxVal[PICO2X3_SERVO_COUNT];
  uint16_t speedVal[PICO2X3_SERVO_COUNT];
  uint16_t posVal[PICO2X3_SERVO_COUNT];
  uint8_t soundType[PICO2X3_SOUND_COUNT];

  // Seed stable config (written rarely in the real product).
  for (uint8_t s = 0; s < PICO2X3_SERVO_COUNT; s++)
  {
    const uint8_t subtype = (uint8_t)(PICO2X3_SERVO_OFFSET + s);
    minVal[s] = (uint16_t)(1000 + s * 10);
    maxVal[s] = (uint16_t)(2000 + s * 10);
    speedVal[s] = (uint16_t)(50 + s);
    posVal[s] = (uint16_t)((minVal[s] + maxVal[s]) / 2);

    FlashWriteBlock minmax(FLASHBLOCK_TYPE_MINMAX, subtype);
    minmax.setWord(minVal[s], 0);
    minmax.setWord(maxVal[s], 2);
    expectTrue(pFS->write(&minmax), "seed minmax");

    FlashWriteBlock speed(FLASHBLOCK_TYPE_SPEED, subtype);
    speed.setWord(speedVal[s], 0);
    expectTrue(pFS->write(&speed), "seed speed");

    FlashWriteBlock pos(FLASHBLOCK_TYPE_POSITION, subtype);
    pos.setWord(posVal[s], 0);
    expectTrue(pFS->write(&pos), "seed position");
  }

  for (uint8_t i = 0; i < PICO2X3_SOUND_COUNT; i++)
  {
    const uint8_t pin = (uint8_t)(PICO2X3_SOUND_BASE_PIN + i);
    soundType[i] = (i == 1) ? MODULE_JQ6500 : MODULE_MP3_TF_16P;
    FlashWriteBlock mod(FLASHBLOCK_TYPE_MODULE_TYPE, pin);
    mod.setData(soundType[i], 0);
    expectTrue(pFS->write(&mod), "seed sound module type");
  }

  auto verifyStableSettings = [&](const char* where) -> bool
  {
    for (uint8_t s = 0; s < PICO2X3_SERVO_COUNT; s++)
    {
      const uint8_t subtype = (uint8_t)(PICO2X3_SERVO_OFFSET + s);
      FlashBlock fb;

      if (!pFS->getBlock(fb, FLASHBLOCK_TYPE_MINMAX, subtype))
      {
        Serial.printf("%s: missing minmax subtype %u\n", where, subtype);
        return false;
      }
      if (fb.getWord(0) != minVal[s] || fb.getWord(2) != maxVal[s])
      {
        Serial.printf("%s: minmax subtype %u corrupt\n", where, subtype);
        return false;
      }

      if (!pFS->getBlock(fb, FLASHBLOCK_TYPE_SPEED, subtype))
      {
        Serial.printf("%s: missing speed subtype %u\n", where, subtype);
        return false;
      }
      if (fb.getWord(0) != speedVal[s])
      {
        Serial.printf("%s: speed subtype %u corrupt\n", where, subtype);
        return false;
      }
    }

    for (uint8_t i = 0; i < PICO2X3_SOUND_COUNT; i++)
    {
      const uint8_t pin = (uint8_t)(PICO2X3_SOUND_BASE_PIN + i);
      FlashBlock fb;
      if (!pFS->getBlock(fb, FLASHBLOCK_TYPE_MODULE_TYPE, pin))
      {
        Serial.printf("%s: missing sound type pin %u\n", where, pin);
        return false;
      }
      if (fb.getData(0) != soundType[i])
      {
        Serial.printf("%s: sound type pin %u corrupt got=%u expected=%u\n",
                      where, pin, fb.getData(0), soundType[i]);
        return false;
      }
    }
    return true;
  };

  auto verifyPositions = [&](const char* where) -> bool
  {
    for (uint8_t s = 0; s < PICO2X3_SERVO_COUNT; s++)
    {
      const uint8_t subtype = (uint8_t)(PICO2X3_SERVO_OFFSET + s);
      FlashBlock fb;
      if (!pFS->getBlock(fb, FLASHBLOCK_TYPE_POSITION, subtype))
      {
        Serial.printf("%s: missing position subtype %u\n", where, subtype);
        return false;
      }
      if (fb.getWord(0) != posVal[s])
      {
        Serial.printf("%s: position subtype %u got=%u expected=%u\n",
                      where, subtype, fb.getWord(0), posVal[s]);
        return false;
      }
    }
    return true;
  };

  expectTrue(verifyStableSettings("after seed"), "stable settings after seed");
  expectTrue(verifyPositions("after seed"), "positions after seed");

  // Frequent position updates (product path: savePosition), force reclaim.
  const int cycles = 4000;
  for (int i = 0; i < cycles; i++)
  {
    if ((i % 500) == 0)
    {
      Serial.printf("Pico2x3Workload progress: %d/%d\n", i, cycles);
    }

    for (uint8_t s = 0; s < PICO2X3_SERVO_COUNT; s++)
    {
      const uint8_t subtype = (uint8_t)(PICO2X3_SERVO_OFFSET + s);
      posVal[s] = (uint16_t)(minVal[s] + ((i + s * 17) % (maxVal[s] - minVal[s] + 1)));
      FlashWriteBlock pos(FLASHBLOCK_TYPE_POSITION, subtype);
      pos.setWord(posVal[s], 0);
      if (!pFS->write(&pos))
      {
        Serial.printf("position write failed at cycle %d servo %u\n", i, s);
        expectTrue(false, "position write failed");
        return false;
      }
    }

    if (i < 30 || (i % 50) == 0)
    {
      if (!verifyStableSettings("during position hammer"))
      {
        expectTrue(false, "settings lost/corrupt while positions update");
        return false;
      }
      if (!verifyPositions("during position hammer"))
      {
        expectTrue(false, "positions corrupt during hammer");
        return false;
      }
    }

    // Once mid-run: update one rare setting (like saveMinMax / setModuleType).
    if (i == 1500)
    {
      minVal[1] = 1111;
      maxVal[1] = 2222;
      FlashWriteBlock minmax(FLASHBLOCK_TYPE_MINMAX, (uint8_t)(PICO2X3_SERVO_OFFSET + 1));
      minmax.setWord(minVal[1], 0);
      minmax.setWord(maxVal[1], 2);
      expectTrue(pFS->write(&minmax), "mid-run minmax update");

      soundType[2] = MODULE_JQ6500;
      FlashWriteBlock mod(FLASHBLOCK_TYPE_MODULE_TYPE, (uint8_t)(PICO2X3_SOUND_BASE_PIN + 2));
      mod.setData(soundType[2], 0);
      expectTrue(pFS->write(&mod), "mid-run sound type update");
    }
  }

  expectTrue(verifyStableSettings("after hammer"), "settings survived reclaim");
  expectTrue(verifyPositions("after hammer"), "final positions ok");

  // BlockIndex must list all live Pico2x3 keys (and not resurrect deletes).
  BlockIndex index(pFS);
  for (uint8_t s = 0; s < PICO2X3_SERVO_COUNT; s++)
  {
    const uint8_t subtype = (uint8_t)(PICO2X3_SERVO_OFFSET + s);
    expectTrue(index.find(FLASHBLOCK_TYPE_MINMAX, subtype) != nullptr, "index minmax");
    expectTrue(index.find(FLASHBLOCK_TYPE_SPEED, subtype) != nullptr, "index speed");
    expectTrue(index.find(FLASHBLOCK_TYPE_POSITION, subtype) != nullptr, "index position");
  }
  for (uint8_t i = 0; i < PICO2X3_SOUND_COUNT; i++)
  {
    const uint8_t pin = (uint8_t)(PICO2X3_SOUND_BASE_PIN + i);
    expectTrue(index.find(FLASHBLOCK_TYPE_MODULE_TYPE, pin) != nullptr, "index sound type");
  }

  // Spot-check expected values with typed asserts.
  FlashBlock fb;
  expectTrue(pFS->getBlock(fb, FLASHBLOCK_TYPE_MINMAX, 1), "read updated minmax");
  expectEqualU16(fb.getWord(0), 1111, "updated min");
  expectEqualU16(fb.getWord(2), 2222, "updated max");
  expectTrue(pFS->getBlock(fb, FLASHBLOCK_TYPE_MODULE_TYPE, 5), "read updated sound");
  expectEqualU8(fb.getData(0), MODULE_JQ6500, "updated sound type pin 5");

  Serial.println("Pico2x3Workload: settings survived frequent position reclaim");
  return true;
}

void TestAll()
{
  Serial.println("========== Starting TestAll ==========");
  gPass = 0;
  gFail = 0;

  struct {
    const char* name;
    bool (*fn)();
  } tests[] = {
    { "TestWriteDeletePlainAndSub", TestWriteDeletePlainAndSub },
    { "TestBlockIndexSkipsDeleted", TestBlockIndexSkipsDeleted },
    { "TestUpdateAndDedup", TestUpdateAndDedup },
    { "TestManyTypesSurviveReclaim", TestManyTypesSurviveReclaim },
    { "TestDeleteThenReclaimNoResurrection", TestDeleteThenReclaimNoResurrection },
    { "TestMixedReallocationStress", TestMixedReallocationStress },
    { "TestPico2x3Workload", TestPico2x3Workload },
  };

  const int n = sizeof(tests) / sizeof(tests[0]);
  for (int i = 0; i < n; i++)
  {
    Serial.println();
    const int failBefore = gFail;
    const bool ok = tests[i].fn();
    if (!ok || gFail > failBefore)
    {
      Serial.printf("*** %s FAILED – aborting remaining tests ***\n", tests[i].name);
      break;
    }
    Serial.printf("*** %s OK ***\n", tests[i].name);
  }

  ListBlocks();
  Serial.println();
  Serial.printf("========== TestAll done: %d checks passed, %d failed ==========\n",
                gPass, gFail);
  Serial.println("Short-press BOOTSEL to run tests again.");
}

// Short press on Pico Zero BOOTSEL (Arduino-Pico BOOTSEL object).
static const unsigned BOOTSEL_DEBOUNCE_MS = 30;
static const unsigned BOOTSEL_SHORT_MAX_MS = 800;

static bool consumeBootselShortPress()
{
  static bool wasPressed = false;
  static unsigned long pressStartedMs = 0;

  const bool pressed = BOOTSEL;
  if (pressed && !wasPressed)
  {
    wasPressed = true;
    pressStartedMs = millis();
    return false;
  }

  if (!pressed && wasPressed)
  {
    wasPressed = false;
    const unsigned long heldMs = millis() - pressStartedMs;
    if (heldMs >= BOOTSEL_DEBOUNCE_MS && heldMs <= BOOTSEL_SHORT_MAX_MS)
    {
      return true;
    }
    Serial.printf("BOOTSEL ignored (held %lu ms; short press = %u..%u ms)\n",
                  heldMs, BOOTSEL_DEBOUNCE_MS, BOOTSEL_SHORT_MAX_MS);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  // Do not block on USB serial – first run must start without any button/Serial.
  delay(500);

  Serial.println("FLASH_PAGE_SIZE = " + String(FLASH_PAGE_SIZE, DEC));
  Serial.println("FLASH_SECTOR_SIZE = " + String(FLASH_SECTOR_SIZE, DEC));
  Serial.println("PICO_FLASH_SIZE_BYTES = " + String(PICO_FLASH_SIZE_BYTES, DEC));
  Serial.println("XIP_BASE = 0x" + String(XIP_BASE, HEX));
  Serial.println("Target: RP2040 Pico Zero – tests start now; short-press BOOTSEL to re-run");

  uint16_t baseSectorNumber = (PICO_FLASH_SIZE_BYTES / FLASH_SECTOR_SIZE) - SECTORS_TO_USE;
  pFS = new FlashStorage(baseSectorNumber, SECTORS_TO_USE, (uint8_t*)"MLLSRV01");
  if (!pFS || !pFS->isValid())
  {
    Serial.println("can't use flash storage");
    pFS = nullptr;
    return;
  }

  Serial.printf("Using %d sectors starting at %u\n", SECTORS_TO_USE, baseSectorNumber);
  ListBlocks();
  TestAll();  // first run: no BOOTSEL required
  Serial.println("setup done.");
}

void loop()
{
  if (!pFS)
  {
    delay(100);
    return;
  }

  // BOOTSEL sampling is relatively slow; poll gently while idle.
  if (consumeBootselShortPress())
  {
    Serial.println();
    Serial.println("BOOTSEL short press – restarting TestAll...");
    TestAll();
  }
  delay(20);
}
