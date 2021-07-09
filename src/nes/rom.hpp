#pragma once
#include "mappers.hpp"
#include <bitset>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <memory>

// reflection 4 dummies
#define DUMP(a)                                                                \
  { std::cout << #a ": " << (a) << std::endl; }

// python-like generic print
template <typename... Args> constexpr void print(Args &&...args) noexcept {
  ((std::cout << std::forward<Args>(args)), ...);
}

struct NesRom {
  struct Header {
    uint8_t nes[4];
    uint8_t prgSize;
    uint8_t chrSize;
    uint8_t flags6;     // mapper, mirroring, battery, trainer
    uint8_t flags7;     // mapper, vs/playchoice, nes 2.0
    uint8_t flags8;     // prg-ram size (rarely used extension)
    uint8_t flags9;     // tv sytem (rarely used extension)
    uint8_t flags10;    // tv sytem, prg-RAM presence (unofficial extension)
    uint8_t padding[5]; // unused, some rippers put their name in bytes 7-15

    // enum MirrorMode { MIRROR_HORIZONTAL, MIRROR_VERTICAL };

    bool isValid() {
      static const uint8_t nes_expected[4] = {
          0x4e, 0x45, 0x53, 0x1a}; // "NES" + MSDOS return character

      for (unsigned i = 0; i < sizeof(nes_expected); i++)
        if (nes_expected[i] != nes[i])
          return false;
      return true;
    }

    bool hasTrainer() { return std::bitset<8>{flags6}[2]; }

    Mapper::MirrorMode getMirrorMode() {
      bool b = !(std::bitset<8>{flags6}[0]);
      return (Mapper::MirrorMode)b;
    }

    unsigned getMapperNumber() {
      return static_cast<unsigned>(
          (std::bitset<8>{flags6} >> 4).to_ulong() +
          ((std::bitset<8>{flags7} >> 4) << 4).to_ulong());
    }

    void print() {
      printf("PRG ROM Size: %d * 16 KB = %d bytes\n", prgSize, 16384 * prgSize);
      printf("CHR ROM Size: %d *  8 KB = %d bytes\n", chrSize, 8192 * chrSize);
    }
  } header;

  // Mapper000 mapper;
  std::shared_ptr<Mapper> mapper;
  // std::shared_ptr<Mapper002> mapper;

  std::vector<uint8_t> trainer, prg, chr;

  NesRom(const std::string &filename) {
    std::ifstream rom_file;
    { // print rom file info
      rom_file.open(filename, std::ios::binary | std::ios::ate);
      auto rom_size = rom_file.tellg();
      DUMP(filename);
      DUMP(rom_size);
    }

    { // read header
      rom_file.seekg(0, std::ios::beg);
      rom_file.read((char *)&header, sizeof(header));

      if (!header.isValid()) {
        std::cerr << "invalid header!\n";
        return;
      }

      header.print();
    }

    uint8_t rom_file_format = 1;
    if ((header.flags7 & 0x0C) == 0x08)
      rom_file_format = 2;

    if (rom_file_format == 1) { // read rom data
      prg.resize(16 * 1024 * header.prgSize);
      if (header.chrSize == 0)
        chr.resize(8 * 1024);
      else
        chr.resize(8 * 1024 * header.chrSize);

    } else {
      header.prgSize = ((header.flags8 & 0x07) << 8) | header.prgSize;
      prg.resize(header.prgSize * 16384);
      header.chrSize = ((header.flags8 & 0x38) << 8) | header.chrSize;
      if (header.chrSize == 0)
        chr.resize(8 * 1024);
      else
        chr.resize(header.chrSize * 8192);
    }

    if (header.hasTrainer()) {
      trainer.resize(512);
      rom_file.read((char *)trainer.data(), trainer.size());
    }

    rom_file.read((char *)prg.data(), prg.size());
    if (header.chrSize > 0)
      rom_file.read((char *)chr.data(), chr.size());

    std::cout << "Successfully read "
              << trainer.size() + prg.size() + chr.size()
              << " byte ROM contents\n";

    switch (header.getMapperNumber()) {
    case 0:
      mapper = std::make_shared<Mapper000>(header.prgSize, header.chrSize);
      break;
    case 1:
      mapper = std::make_shared<Mapper001>(header.prgSize, header.chrSize);
      break;
    case 2:
      mapper = std::make_shared<Mapper002>(header.prgSize, header.chrSize);
      break;
    case 4:
      mapper = std::make_shared<Mapper004>(header.prgSize, header.chrSize);
      break;
    default:
      std::cerr << "Unsupported Rom mapper number: " << header.getMapperNumber()
                << "\n";
      break;
    }
    rom_file.close();
  }

  bool cpuRead(uint16_t addr, uint8_t &data) {
    uint32_t mapped_addr;
    if (mapper->cpuMapRead(addr, mapped_addr, data)) {
      if (mapped_addr == 0xFFFFFFFF)
        return true; // mapper set the value
      data = prg[mapped_addr];
      return true;
    }
    return false;
  }

  bool cpuWrite(uint16_t addr, uint8_t data) {
    uint32_t mapped_addr;
    if (mapper->cpuMapWrite(addr, mapped_addr, data)) {
      if (mapped_addr == 0xFFFFFFFF)
        return true; // mapper set the value
      prg[mapped_addr] = data;
      return true;
    }
    return false;
  }
  bool ppuRead(uint16_t addr, uint8_t &data) {
    uint32_t mapped_addr;
    if (mapper->ppuMapRead(addr, mapped_addr)) {
      if (mapped_addr < chr.size())
        data = chr[mapped_addr];
      return true;
    }
    return false;
  }
  bool ppuWrite(uint16_t addr, uint8_t data) {
    uint32_t mapped_addr;
    if (mapper->ppuMapWrite(addr, mapped_addr)) {
      chr[mapped_addr] = data;
      return true;
    }
    return false;
  }

  void reset() {
    if (mapper != nullptr)
      mapper->reset();
  }

  static int read_rom(const std::string &filename,
                      std::shared_ptr<NesRom> &rom) {
    rom = std::make_shared<NesRom>(filename);
    return 0;
  }

  static void writeSegment(const std::string &filename,
                           std::vector<uint8_t> &seg) {
    std::fstream ofile;
    ofile.open(filename, std::ios::out | std::ios::binary);
    ofile.write((char *)seg.data(), seg.size());
    ofile.close();
  }

  Mapper::MirrorMode getMirrorMode() {
    Mapper::MirrorMode m = mapper->getMirror();
    if (m == Mapper::MIRROR_HARDWARE) {
      return header.getMirrorMode();
    } else {
      return m;
    }
  }
};
