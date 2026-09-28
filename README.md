# Pico Flash Storage

A wear-leveling flash storage library for persistent data on **RP2040** (Arduino-Pico).  
It manages multiple flash sectors with automatic wear leveling, CRC integrity checks, and an 8-byte block API for configuration data, counters, and similar small persistent values.

**Current version: 1.2.0**

## What's new in 1.2.0

Release **1.2.0** focuses on correct behaviour when flash sectors fill up and live data must be migrated (reclaim / compaction). Earlier versions could lose or corrupt settings in that path; 1.2.0 hardens indexing, reclaim, and the regression tests around it.

### Reliability (reclaim / full buffer)

- **Live data only on reclaim** — `BlockIndex` no longer treats delete-tombstones as data to migrate. Deleted keys stay deleted; they are not resurrected as live `0xFF` payloads after a sector erase.
- **Newest-wins with tombstones** — For each type/subtype, the newest CRC-valid block decides the key. If that block is deleted, the key is omitted and older live copies are ignored.
- **Safe preserve path** — Preserved blocks are written directly to a sector (`SecureSector::write`) instead of recursing through `FlashStorage::write()`, which avoided nested reclaim / double-write hazards.
- **Reserved free sector** — Normal writes use sectors `0 .. sectorCount-2`. The last sector stays spare for migration. After reclaim, the new block is written to the first writable sector that still has space (not a single hard-coded slot).
- **Minimum two sectors** — Construction and `write()` require `sectorCount >= 2`. Fewer sectors leave the storage unusable (`isValid() == false`).

### BlockIndex API

- **Dynamic capacity** — Index entries use `std::vector` instead of a fixed `maxEntries` (previously often set to `sectorCount`). Many unique keys no longer get silently dropped when reclaim builds the index.
- **`getCount()` returns `size_t`** — Matches the vector-backed entry list.
- **Deleted blocks are never delivered** — `find()` / `getEntry()` only expose live keys.

### FlashWriteBlock / API cleanup

- **Constructors zero-fill the payload** — Both `FlashWriteBlock(type)` and `FlashWriteBlock(type, subtype)` initialize the 8-byte buffer (`0xFF`), so partial fills no longer leave undefined bytes in flash.
- **Removed unused page dump helpers** — Dead `dumpPage` APIs were dropped from `SecureSector`.
- **English comments and docs** — Public headers and reclaim comments are in English; minor typo cleanups in reclaim naming/logs.

### FlashTest example

`examples/FlashTest` is rewritten as a regression suite that intentionally fills sectors and triggers reclaim:

- Many unique types (more than the sector count) must survive reclaim
- Deleted blocks must not reappear as live data after reclaim
- Mixed plain types (`type < 0x80`) and type/subtype pairs
- Pico2x3-style workload: rare settings + frequent position updates (MLLServo / MLLSound layout)
- Optional BOOTSEL re-run on Arduino-Pico boards

## Features

- Automatic wear leveling across multiple flash sectors
- Block-based storage with type / subtype support
- CRC integrity checking for sector headers and blocks
- Read / write / delete API with change detection (unchanged data is not rewritten)
- `BlockIndex` for newest live block per key
- `getMaxEraseCount()` as a simple wear indicator
- Adjustable `FlashStorage::LogLevel` for Serial debug output

## Requirements

- Platform: **RP2040** (Arduino-Pico / PlatformIO `raspberrypi`)
- At least **2 flash sectors** reserved for this library (4 KB each)
- C++ with `std::vector` (provided by the Arduino-Pico toolchain)

## Quick usage

```cpp
#include "PicoFlashStorage.h"
#include "BlockIndex.h"

using namespace PicoFlashStorage;

// Reserve the last N sectors of the chip flash for storage
const uint16_t SECTORS = 4;
uint16_t base = (PICO_FLASH_SIZE_BYTES / FLASH_SECTOR_SIZE) - SECTORS;

FlashStorage fs(base, SECTORS, (const uint8_t*)"MLLSRV01");
FlashStorage::LogLevel = 0;  // 0 = quiet

FlashWriteBlock pos(0xF2, /*subtype*/ 0);  // type >= 0x80 → subtype + 4 data bytes
pos.getBuffer()[2] = 42;
fs.write(&pos);

FlashBlock fb;
if (fs.getBlock(fb, 0xF2, 0)) {
  // use fb ...
}

BlockIndex index(&fs);  // live keys only
```

Prefer writing only when values actually change (the library already skips identical payloads). Frequent writes still consume erase cycles — see endurance notes below.

## Sector layout

Each sector is **4096 bytes**.

**Header (offset 0, 16 bytes):**

| Offset | Size | Content |
|--------|------|---------|
| 0 | 8 | Sector identity (signature, e.g. `MLLSRV01`) |
| 8 | 3 | Erase count (wear tracking) |
| 11 | 3 | Reserved (`0`) |
| 14 | 2 | Header CRC |

**Blocks (0–509, offset `16 + blockIndex * 8`):**

| Type range | Layout |
|------------|--------|
| `type < 0x80` | 1 type + 5 data + 2 CRC |
| `type >= 0x80` | 1 type + 1 subtype + 4 data + 2 CRC |

**Delete tombstones** store an inverted CRC so they remain recognizable as valid deleted markers without looking like live data.

### Block type examples

**Servo position** (`type >= 0x80`):

- type `0xF2`, subtype = servo index  
- 4 data bytes (positions / reserved) + CRC  

**Servo min/max or speed** (product-specific):

- types such as `0xF0` / `0xF1` with subtype = servo index  

Exact product layouts (e.g. Pico2x3 / MobaLedLib) live in the application; FlashTest documents one tested combination.

## Endurance (rough guide)

Typical RP2040 onboard flash is rated around **≥ 100 000 erase cycles per 4 KB sector**, with ~20 years data retention.

With wear leveling over *N* sectors and ~510 usable 8-byte blocks per sector, lifetime depends almost entirely on **how often you write**:

| Workload | Expectation |
|----------|-------------|
| Rare config changes | Dominated by retention, not erase wear |
| Position save every few seconds | Often decades with a few reserved sectors |
| Continuous high-rate writes | Can exhaust flash in years or less |

Use more sectors for hotter update rates, and avoid writing every control loop tick. Monitor `getMaxEraseCount()` in the field if needed.

## License

LGPL-2.1-or-later — see [LICENSE](LICENSE).

## Links

- Repository: https://github.com/jueff/PicoFlashStorage  
- Example: `examples/FlashTest`  
