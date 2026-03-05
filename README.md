# Pico Flash Storage

A wear-leveling flash storage library for persistent data storage on RP2040 micro controllers.  
Manages multiple flash sectors with automatic wear leveling, CRC data integrity checking, and block-based data organization for reliable long-term storage of configuration data, counters, and other persistent information.

## Features

- Automatic wear leveling across multiple flash sectors
- Block-based data storage with type/subtype support
- CRC integrity checking for all blocks and headers
- Easy block read/write/delete API
- Sector and block indexing for fast lookup
- Designed for Arduino and RP2040

## Sector Layout

Each sector has 4096 bytes.

**Header (Offset 0, length 16 bytes):**
- 8 bytes: Sector Identity (signature, e.g. `MLLSERVO`)
- 3 bytes: Erase count (incremented on every page erase, tracks wear)
- 3 bytes: Reserved (0)
- 2 bytes: CRC

**Blocks (0-509, Offset 16 + BlockIndex * 8):**
- If block type < 0x80:
  - 1 byte: block type
  - 5 bytes: block data
  - 2 bytes: block CRC
- If block type >= 0x80:
  - 1 byte: block type
  - 1 byte: block subtype
  - 4 bytes: block data
  - 2 bytes: block CRC

**Block Types Example:**

- **Servo position block**
  - 1 byte: type 0xf0
  - 1 byte: subtype 0x01
  - 1 byte: Pos#0
  - 1 byte: Pos#1
  - 1 byte: Pos#2
  - 1 byte: reserved (0)
  - 2 bytes: CRC

- **Servo settings block**
  - 1 byte: type 0xA0
  - 1 byte: Server#
  - 1 byte: MinPos
  - 1 byte: MaxPos
  - 1 byte: Speed
  - 1 byte: options
  - 2 bytes: CRC
