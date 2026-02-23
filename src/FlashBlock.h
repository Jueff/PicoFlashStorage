#pragma once
#include <cstdint>

namespace PicoFlashStorage {

  class FlashBlock
  {
  public:
    FlashBlock(const uint8_t* address = nullptr);
    void setAddress(const uint8_t* address);
    bool isEmpty() const;
    bool isValid() const;
    bool isDeleted() const;
    uint8_t getType() const;
    uint8_t getSubtype() const;
    const uint8_t* getAddress() const;
    bool getData(uint8_t* dest, uint8_t srcOffset, uint8_t length) const;
    uint8_t getData(uint8_t offset) const;
    uint16_t getWord(uint8_t offset) const;
    uint32_t getLong(uint8_t offset) const;
    bool matchesType(uint8_t type, uint8_t subType = 0) const;

  private:
    const uint8_t* address;
  };

  class FlashWriteBlock
  {
  public:
    FlashWriteBlock(int8_t blockType);
    FlashWriteBlock(int8_t blockType, uint8_t subType);
    FlashWriteBlock(const FlashBlock& fb);
    ~FlashWriteBlock();
    uint8_t getType() const;
    uint8_t getSubtype() const;
    bool setData(const uint8_t* source, uint8_t offset, uint8_t length);
    bool setData(uint8_t value, uint8_t offset);
    bool setWord(uint16_t value, uint8_t offset);
    bool setLong(uint32_t value, uint8_t offset);
    uint8_t setBlockSubtype(uint8_t value);
    uint8_t* getBuffer() const;
    bool matches(const FlashBlock* flashBlock) const;
    bool isDeleted() const;
    void setIsDeleted(bool value);
    void setCRC();

  private:
    uint8_t* newData;
    bool isDeletedFlag = false;
  };

  class IndexedFlashBlock : public FlashBlock {
  public:
    IndexedFlashBlock(uint8_t* address = nullptr, int16_t sector = -1, int16_t block = -1);
    int16_t getSector() const;
    void setSector(int16_t value);
    int16_t getBlock() const;
    void setBlock(int16_t value);

  private:
    int16_t sector;
    int16_t block;
  };
}