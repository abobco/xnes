#pragma once
#include "apu.hpp"
#include "cpu.hpp"
#include "ppu.hpp"
#include "rom.hpp"
#include <mutex>
#include <sys/stat.h>

struct NesBus {
  static const size_t MEMORY_SIZE = 0x07ff;
  static const size_t BLOCK_SIZE = 1024;

  struct DmaInfo {
    uint8_t page = 0;
    uint8_t addr = 0;
    uint8_t data = 0;
    bool dummy = false;
    bool transfer = false;
  } DMA;

  uint32_t systemClockCount = 0;
  std::array<uint8_t, 2048> memory;

  Cpu6502 cpu;
  APU apu;
  Ppu2C02 ppu;
  // NesRom rom;
  std::shared_ptr<NesRom> rom;

  uint8_t controller[2], controller_state[2];

  AudioFloat audioSample = 0;
  AudioFloat audioTime = 0;
  AudioFloat audioTimePerNesClock = 0;
  AudioFloat audioTimePerSystemSample = 0;

  std::mutex guard;

  void init() {
    // memory.resize(2 * BLOCK_SIZE);
    rom->reset();
    cpu.connectBus(this);
    cpu.createInstructionSet();
  }

  int loadRom(const std::string &filepath) {
    int res = NesRom::read_rom(filepath, rom);
    ppu.connectRom(rom);
    return res;
  }

  void setSampleFrequency(uint32_t sample_rate, float speed = 1.0) {
    audioTimePerSystemSample = 1.0 / (sample_rate);
    audioTimePerNesClock = 1.0 / (5369318.0 * speed); // PPU clock frequency
  }

  void writeCpu(uint16_t addr, uint8_t data) {
    if (rom->cpuWrite(addr, data)) {
      // cartridge can veto any bus transaction
    } else if (addr <= 0x1FFF) {
      // write to ram
      memory[addr & 0x07FF] = data;
    } else if (addr >= 0x2000 && addr <= 0x3FFF) {
      // write to ppu
      ppu.cpuWrite(addr & 0x0007, data);
    } else if ((addr >= 0x4000 && addr <= 0x4013) || addr == 0x4015) {
      // apu memory
      apu.cpuWrite(addr, data);
    } else if (addr == 0x4014) {
      // DMA transfer
      DMA.page = data;
      DMA.addr = 0x00;
      DMA.transfer = true;
    } else if (addr >= 0x4016 && addr <= 0x4017) {
      controller_state[addr & 0x0001] = controller[addr & 0x0001];
    }
  }

  uint8_t readCpu(uint16_t addr, bool readOnly = false) {
    uint8_t data = 0;
    if (rom->cpuRead(addr, data))
      return data;
    // if (addr >= memory.size())
    //   return 0;
    if (addr <= 0x1FFF)
      return memory[addr & 0x07FF];
    if (addr <= 0x3FFF)
      return ppu.cpuRead(addr & 0x0007, readOnly);
    if (addr == 0x4015) {
      // apu memory
      return apu.cpuRead(addr);
    }
    if (addr >= 0x4016 && addr <= 0x4017) {
      data = (controller_state[addr & 0x0001] & 0x80) > 0;
      controller_state[addr & 0x0001] <<= 1;
    }
    return data;
  }

  void reset() {
    std::memset(&memory.front(), 0, memory.size());
    rom->reset();
    cpu.reset();
    ppu.reset();
    systemClockCount = 0;
    std::memset(&DMA, 0x00, sizeof(DMA));
    DMA.dummy = true;
  }

  bool clock() {

    ppu.clock();
    apu.clock();

    if (systemClockCount % 3 == 0) {
      if (DMA.transfer) {
        if (DMA.dummy) {
          // wait until next even clock cycle
          if (systemClockCount % 2 == 1)
            DMA.dummy = false;
        } else {
          // perform DMA transfer
          if (systemClockCount % 2 == 0) {
            // read from CPU bus on even clock cycles
            DMA.data = readCpu(DMA.page << 8 | DMA.addr);
          } else {
            // write to PPU OAM on odd clock cycles
            ppu.OAM.memory.data[DMA.addr++] = DMA.data;
            if (DMA.addr == 0) {
              // address wrapped around at 256 bytes, end DMA transfer
              DMA.transfer = false;
              DMA.dummy = true;
            }
          }
        }
      } else
        cpu.clock();
    }

    // audio sync
    bool audio_sample_ready = false;
    audioTime += audioTimePerNesClock;
    if (audioTime >= audioTimePerSystemSample) {
      audioTime -= audioTimePerSystemSample;
      audioSample = apu.getSample();
      audio_sample_ready = true;
    }

    if (ppu.nmi) {
      ppu.nmi = false;
      cpu.nonMaskableInterrupt();
    }

    if (rom->mapper->irqState()) {
      rom->mapper->irqClear();
      cpu.interruptRequest();
    }

    ++systemClockCount;

    return audio_sample_ready;
  }

  void drawFrame() {
    do {
      clock(); // cycle until end of frame
    } while (!ppu.frameComplete);
    do {
      clock(); // finish current instruction
    } while (cpu.cycles != 0);
    ppu.frameComplete = false;
  }

  /**
   *  NES save file format
   *
   *  Bytes   type                content
   * -----------------------------------
   *  16    NesRom::Header      rom header
   *  0:512 uint8_t             rom trainer
   *  n     uint8_t             rom prg
   *  m     uint8_t             rom chr
   *
   *  7     Cpu6502::Registers  Cpu register values
   *  1     uint8_t             Cpu inputAlu
   *  1     uint8_t             Cpu opcode
   *  2     uint16_t            Cpu temp var
   *  2     uint16_t            Cpu absolute address
   *  2     uint16_t            Cpu relative address
   *  2     uint16_t            Cpu cycles
   *
   *  8     Ppu2C02::Registers  Ppu register values
   *  12    Ppu2C02::BackgroundData
   *  256   Ppu2CO2::OAM        Ppu Object attribute memory
   *  51    Ppu2C02::SpriteInfo
   *  2048  uint8_t             Ppu name table
   *  8192  uint8_t             Ppu pattern table
   *  32    uint8_t             Ppu palette table
   *  1     uint8_t             Ppu address latch
   *  1     uint8_t             Ppu data buffer
   *  2     uint16_t            Ppu scanline
   *  2     uint16_t            Ppu cycle
   *  1     bool                Ppu nmi
   *
   *  5     Bus::DmaInfo        Bus DMA
   *  2048  uint8_t             Bus RAMs
   */

  void saveState(const std::string &filename) {
    guard.lock();
    std::fstream savefile(filename, std::ios_base::binary | std::ios::out);

    savefile.write((char *)&rom->header, sizeof(rom->header));
    if (rom->header.hasTrainer())
      savefile.write((char *)&rom->trainer, 512);

    size_t prgSize = rom->prg.size(), chrSize = rom->chr.size();
    savefile.write((char *)&prgSize, sizeof(prgSize));
    savefile.write((char *)rom->prg.data(), rom->prg.size());
    savefile.write((char *)&chrSize, sizeof(chrSize));
    savefile.write((char *)rom->chr.data(), rom->chr.size());
    if (rom->header.getMapperNumber() == 1) {
      auto mapper_buf = rom->mapper->serialize();
      savefile.write((char *)mapper_buf.data(), mapper_buf.size());
    }

    savefile.write((char *)&cpu.registers, sizeof(cpu.registers));
    savefile.write((char *)&cpu.inputAlu, sizeof(cpu.inputAlu));
    savefile.write((char *)&cpu.opcode, sizeof(cpu.opcode));
    savefile.write((char *)&cpu.temp, sizeof(cpu.temp));
    savefile.write((char *)&cpu.absoluteAddress, sizeof(cpu.absoluteAddress));
    savefile.write((char *)&cpu.relativeAddress, sizeof(cpu.relativeAddress));
    savefile.write((char *)&cpu.cycles, sizeof(cpu.cycles));

    savefile.write((char *)&ppu.registers, sizeof(ppu.registers));
    savefile.write((char *)&ppu.bg, sizeof(ppu.bg));
    savefile.write((char *)&ppu.OAM, sizeof(ppu.OAM));
    savefile.write((char *)&ppu.sprites, sizeof(ppu.sprites));
    for (const std::array<uint8_t, 1024> &block : ppu.nameTable)
      savefile.write((char *)block.data(), block.size());
    for (const std::array<uint8_t, 4096> &block : ppu.patternTable)
      savefile.write((char *)block.data(), block.size());
    savefile.write((char *)ppu.paletteTable.data(), ppu.paletteTable.size());
    savefile.write((char *)&ppu.addressLatch, sizeof(ppu.addressLatch));
    savefile.write((char *)&ppu.dataBuffer, sizeof(ppu.dataBuffer));
    savefile.write((char *)&ppu.scanline, sizeof(ppu.scanline));
    savefile.write((char *)&ppu.cycle, sizeof(ppu.cycle));
    savefile.write((char *)&ppu.nmi, sizeof(ppu.nmi));

    savefile.write((char *)&DMA, sizeof(DMA));
    savefile.write((char *)memory.data(), memory.size());

    savefile.close();
    guard.unlock();
  }

  void loadState(const std::string &filename, bool resize = true) {
    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0)
      return;
    guard.lock();
    std::fstream savefile(filename, std::ios_base::binary | std::ios::in);

    savefile.read((char *)&rom->header, sizeof(rom->header));

    if (rom->header.hasTrainer())
      savefile.read((char *)&rom->trainer, 512);

    size_t prgSize, chrSize;

    if (resize) {
      savefile.read((char *)&prgSize, sizeof(prgSize));
      rom->prg.resize(prgSize);
    }
    savefile.read((char *)rom->prg.data(), rom->prg.size());

    if (resize) {
      savefile.read((char *)&chrSize, sizeof(chrSize));
      rom->chr.resize(chrSize);
    }
    savefile.read((char *)rom->chr.data(), rom->chr.size());

    if (rom->header.getMapperNumber() == 1) {
      std::vector<uint8_t> mapper_buf;
      mapper_buf.resize(rom->mapper->size());
      savefile.read((char *)mapper_buf.data(), mapper_buf.size());
      rom->mapper->deserialize(mapper_buf);
    }

    savefile.read((char *)&cpu.registers, sizeof(cpu.registers));
    savefile.read((char *)&cpu.inputAlu, sizeof(cpu.inputAlu));
    savefile.read((char *)&cpu.opcode, sizeof(cpu.opcode));
    savefile.read((char *)&cpu.temp, sizeof(cpu.temp));
    savefile.read((char *)&cpu.absoluteAddress, sizeof(cpu.absoluteAddress));
    savefile.read((char *)&cpu.relativeAddress, sizeof(cpu.relativeAddress));
    savefile.read((char *)&cpu.cycles, sizeof(cpu.cycles));

    savefile.read((char *)&ppu.registers, sizeof(ppu.registers));
    savefile.read((char *)&ppu.bg, sizeof(ppu.bg));
    savefile.read((char *)&ppu.OAM, sizeof(ppu.OAM));
    savefile.read((char *)&ppu.sprites, sizeof(ppu.sprites));
    for (std::array<uint8_t, 1024> &block : ppu.nameTable)
      savefile.read((char *)block.data(), block.size());
    for (std::array<uint8_t, 4096> &block : ppu.patternTable)
      savefile.read((char *)block.data(), block.size());
    savefile.read((char *)ppu.paletteTable.data(), ppu.paletteTable.size());
    savefile.read((char *)&ppu.addressLatch, sizeof(ppu.addressLatch));
    savefile.read((char *)&ppu.dataBuffer, sizeof(ppu.dataBuffer));
    savefile.read((char *)&ppu.scanline, sizeof(ppu.scanline));
    savefile.read((char *)&ppu.cycle, sizeof(ppu.cycle));
    savefile.read((char *)&ppu.nmi, sizeof(ppu.nmi));

    savefile.read((char *)&DMA, sizeof(DMA));
    savefile.read((char *)memory.data(), memory.size());

    savefile.close();
    ppu.connectRom(rom);

    guard.unlock();
  }
};
