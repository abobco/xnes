#pragma once
#include "renderer.hpp"
#include "rom.hpp"
#include <array>
#include <cstdint>

class Ppu2C02 {
public:
  enum RegisterAddress {
    PPU_CTRL,
    PPU_MASK,
    PPU_STATUS,
    OAM_ADDR,
    OAM_DATA,
    PPU_SCROLL,
    PPU_ADDR,
    PPU_DATA,
  };

  struct Registers {
    union {
      struct {
        unsigned unused : 5;
        unsigned spriteOverflow : 1;
        unsigned spriteZeroHit : 1;
        unsigned vblank : 1;
      };
      uint8_t val;
    } STATUS;

    union {
      struct {
        unsigned grayscale : 1;
        unsigned showBgLeft : 1;
        unsigned showSpritesLeft : 1;
        unsigned showBg : 1;
        unsigned showSprites : 1;
        unsigned r : 1;
        unsigned g : 1;
        unsigned b : 1;
      };
      uint8_t val;
    } MASK;

    union {
      struct {
        unsigned nametableX : 1;
        unsigned nametableY : 1;
        unsigned vramAddressInc : 1;
        unsigned spriteAdress8x8 : 1;
        unsigned bgAddress : 1;
        unsigned spriteSize : 1;
        unsigned cs : 1;
        unsigned vblankNmi : 1;
      };
      uint8_t val;
    } CTRL;

    union ScrollRegister {
      struct {
        unsigned coarseX : 5;
        unsigned coarseY : 5;
        unsigned nameTableX : 1;
        unsigned nameTableY : 1;
        unsigned fineY : 3;
        unsigned padding : 1;
      };
      uint16_t val;
    };

    ScrollRegister v;
    ScrollRegister t;
    uint8_t fineX;
  } registers;

  struct BackgroundData {
    uint8_t NextTileId = 0;
    uint8_t NextTileAttrib = 0;
    uint8_t NextTileLsb = 0;
    uint8_t NextTileMsb = 0;
    uint16_t ShiftPatternLo = 0;
    uint16_t ShiftPatternHi = 0;
    uint16_t ShiftAttribLo = 0;
    uint16_t ShiftAttribHi = 0;
  } bg;

  struct ObjectAttributeMemory {
    struct Entry {
      uint8_t y;          // sprite y position
      uint8_t id;         // tile id
      uint8_t attributes; // attribute flags
      uint8_t x;          // sprite x position
    };
    union {
      Entry entries[64];
      uint8_t data[64 * sizeof(Entry)];
    } memory;
    uint8_t address;
  } OAM;

  struct SpriteInfo {
    ObjectAttributeMemory::Entry scanlineSprites[8];
    uint8_t count;
    uint8_t shiftPatternLo[8];
    uint8_t shiftPatternHi[8];
    bool zeroHitPossible = false;
    bool zeroDrawing = false;
  } sprites;

  std::array<std::array<uint8_t, 1024>, 2> nameTable;
  std::array<std::array<uint8_t, 4096>, 2> patternTable;
  std::array<uint8_t, 32> paletteTable;
  std::shared_ptr<NesRom> rom;
  uint8_t addressLatch = 0, dataBuffer = 0;
  int16_t scanline = 0, cycle = 0;

  NesRenderer renderer;

  bool frameComplete = false, nmi = false, nmiIgnore = false;
  bool use_vsync = true;
  uint32_t framecount = 0;
  bool odd = false;

  NesRenderer::Sprite<NesRenderer::NES_WIDTH, NesRenderer::NES_HEIGHT> &
  getFramebuffer(bool active = false);

  NesPixel getColorFromPalette(uint8_t palette, uint8_t pixel);

  NesRenderer::Sprite<128, 128> &getPatternTable(uint8_t i, uint8_t palette);

  NesRenderer::Sprite<NesRenderer::NES_WIDTH, NesRenderer::NES_HEIGHT> &
  getNameTable(uint8_t i);

  uint8_t cpuRead(uint16_t addr, bool rdOnly = false);

  void cpuWrite(uint16_t addr, uint8_t data);

  uint8_t &mirroredNameTableEntry(uint16_t addr);

  uint8_t ppuRead(uint16_t addr, bool rdOnly = false);

  void ppuWrite(uint16_t addr, uint8_t data);

  void connectRom(const std::shared_ptr<NesRom> &rom) { this->rom = rom; }

  void reset();

  void clock();

private:
  bool renderEnabled();

  void scrollX();

  void scrollY();

  void txAddressX();

  void txAddressY();

  void loadBackgroundShifters();

  void updateShifters();
};