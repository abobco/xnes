#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
// Nes cartridge memory mapper base class, used in all roms
// Maps physical memory to CPU and PPU address space
struct Mapper {
  uint8_t prgBankCount = 0;
  uint8_t chrBankCount = 0;
  enum MirrorMode {
    MIRROR_VERTICAL,
    MIRROR_HORIZONTAL,
    MIRROR_ONESCREEN_LO,
    MIRROR_ONESCREEN_HI,
    MIRROR_HARDWARE,
  };

  Mapper() {}

  Mapper(uint8_t prg_banks, uint8_t chr_banks)
      : prgBankCount(prg_banks), chrBankCount(chr_banks) {
    reset();
  }
  virtual ~Mapper() {}
  virtual void reset() {}
  virtual MirrorMode getMirror() { return MIRROR_HARDWARE; }
  // Transform CPU bus address into PRG ROM offset
  virtual bool cpuMapRead(uint16_t addr, uint32_t &mapped_addr,
                          uint8_t data) = 0;
  virtual bool cpuMapWrite(uint16_t addr, uint32_t &mapped_addr,
                           uint8_t data) = 0;

  // Transform PPU bus address into CHR ROM offset
  virtual bool ppuMapRead(uint16_t addr, uint32_t &mapped_addr) = 0;
  virtual bool ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) = 0;

  // IRQ Interface
  virtual bool irqState() { return false; }
  virtual void irqClear() {}

  // Scanline Counting
  virtual void scanline() {}

  virtual std::vector<uint8_t> serialize();

  virtual void deserialize(std::vector<uint8_t> &buffer);

  virtual size_t size();
};

/**
 * iNES mapper 000
 *
 * - PRG ROM size: 16K or 32K
 * - PRG RAM size: 2K or 4K
 * - CHR ROM size: 8K
 *
 * - No bank switching
 * - Nametable mirroring
 *
 * - Banks(fixed):
 *   - CPU $6000-$7FFF: Family Basic only:
 *                      PRG RAM, mirrored as necessary to fill entire 8 KiB
 *                      window, write protectable with an external switch
 *  - CPU $8000-$BFFF: First 16 KB of ROM.
 *  - CPU $C000-$FFFF: Last 16 KB of ROM (NROM-256) or mirror of $8000-$BFFF
 *                     (NROM-128).
 *
 * */
struct Mapper000 : public Mapper {
  Mapper000() {}

  Mapper000(uint8_t prgBankCount, uint8_t chrBankCount)
      : Mapper(prgBankCount, chrBankCount) {}

  bool cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool cpuMapWrite(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool ppuMapRead(uint16_t addr, uint32_t &mapped_addr) override;

  bool ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) override;
};

/**
 * iNES mapper 001
 * */
struct Mapper001 : public Mapper {
  static const uint32_t STATIC_RAM_SIZE = 32 * 1024;

  struct BankSelect {
    uint8_t lo = 0;
    uint8_t hi = 0;
    uint8_t full = 0;
  };

  struct Registers {
    uint8_t load = 0;
    uint8_t loadCount = 0;
    uint8_t control = 0;
  } registers;

  BankSelect chrBankSelect;
  BankSelect prgBankSelect;
  Mapper::MirrorMode mirrorMode = MIRROR_HORIZONTAL;
  std::vector<uint8_t> staticRam;

  Mapper001() {}

  Mapper001(uint8_t prgBankCount, uint8_t chrBankCount);

  ~Mapper001() {}

  void reset() override;

  bool cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool cpuMapWrite(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool ppuMapRead(uint16_t addr, uint32_t &mapped_addr) override;

  bool ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) override;

  MirrorMode getMirror() override { return mirrorMode; }

  virtual size_t size() override;

  std::vector<uint8_t> serialize() override;

  void deserialize(std::vector<uint8_t> &buffer) override;
};

/**
 * iNES mapper 002
 * */
struct Mapper002 : public Mapper {
  uint8_t prgBankSelectLo = 0;
  uint8_t prgBankSelectHi = 0;

  Mapper002() {}

  Mapper002(uint8_t prgBankCount, uint8_t chrBankCount)
      : Mapper(prgBankCount, chrBankCount) {}

  void reset() override;

  bool cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool cpuMapWrite(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool ppuMapRead(uint16_t addr, uint32_t &mapped_addr) override;

  bool ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) override;
};

/**
 * iNes Mapper 004
 * */
struct Mapper004 : public Mapper {
  struct RomMemory {
    std::array<uint32_t, 8> registers;
    std::array<uint32_t, 8> chrBank;
    std::array<uint32_t, 4> prgBank;
    std::vector<uint8_t> staticRam;
  } memory;

  struct IrqInfo {
    uint16_t counter = 0;
    uint16_t reload = 0;
    bool active = false;
    bool enable = false;
    bool update = false;
  } irq;

  uint8_t targetRegister = 0;
  bool prgBankMode = false;
  bool chrInversion = false;
  Mapper::MirrorMode mirrorMode = MIRROR_HORIZONTAL;

  Mapper004() {}

  Mapper004(uint8_t prgBankCount, uint8_t chrBankCount);

  bool cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool cpuMapWrite(uint16_t addr, uint32_t &mapped_addr, uint8_t data) override;

  bool ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) override;
  bool ppuMapRead(uint16_t addr, uint32_t &mapped_addr) override;

  void reset() override;

  bool irqState() override { return irq.active; }
  void irqClear() override { irq.active = false; }

  void scanline() override;

  MirrorMode getMirror() override { return mirrorMode; }
};
