#include "mappers.hpp"

std::vector<uint8_t> Mapper::serialize() {
  std::vector<uint8_t> buffer;
  buffer.resize(2);
  buffer[0] = prgBankCount;
  buffer[1] = chrBankCount;
  return buffer;
}

void Mapper::deserialize(std::vector<uint8_t> &buffer) {
  prgBankCount = buffer[0];
  chrBankCount = buffer[1];
}

size_t Mapper::size() { return 2; }

/**
 * iNES mapper 000
 *
 **/

bool Mapper000::cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) {
  if (addr >= 0x8000 && addr <= 0xFFFF) {
    mapped_addr = addr & (prgBankCount > 1 ? 0x7FFF : 0x3FFF);
    return true;
  }
  return false;
}

bool Mapper000::cpuMapWrite(uint16_t addr, uint32_t &mapped_addr,
                            uint8_t data) {
  if (addr >= 0x8000 && addr <= 0xFFFF) {
    mapped_addr = addr & (prgBankCount > 1 ? 0x7FFF : 0x3FFF);
    return true;
  }
  return false;
}

bool Mapper000::ppuMapRead(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    mapped_addr = addr;
    return true;
  }
  return false;
}

bool Mapper000::ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    if (chrBankCount == 0) {
      // treat as RAM
      mapped_addr = addr;
      return true;
    }
  }
  return false;
}

/**
 * iNES mapper 001
 *
 **/

Mapper001::Mapper001(uint8_t prgBankCount, uint8_t chrBankCount)
    : Mapper(prgBankCount, chrBankCount) {
  staticRam.resize(STATIC_RAM_SIZE);
}

void Mapper001::reset() {
  registers.control = 0x1C;
  registers.load = 0;
  registers.loadCount = 0;

  chrBankSelect.lo = chrBankSelect.hi = chrBankSelect.full = 0;
  prgBankSelect.lo = prgBankSelect.full = 0;
  prgBankSelect.hi = prgBankCount - 1;
}

bool Mapper001::cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) {
  if (addr >= 0x6000 && addr <= 0x7FFF) {
    // read static ram from rom
    mapped_addr = 0xFFFFFFFF;
    data = staticRam[addr & 0x1FFF];
    return true;
  }
  if (addr >= 0x8000) {
    if (registers.control & 0b01000) {
      // 16k mode
      if (addr >= 0x8000 && addr <= 0xBFFF) {
        mapped_addr = prgBankSelect.lo * 0x4000 + (addr & 0x3FFF);
        return true;
      }

      if (addr >= 0xC000 && addr <= 0xFFFF) {
        mapped_addr = prgBankSelect.hi * 0x4000 + (addr & 0x3FFF);
        return true;
      }
    } else {
      // 32k mode
      mapped_addr = prgBankSelect.full * 0x8000 + (addr & 0x7FFF);
      return true;
    }
  }
  return false;
}

bool Mapper001::cpuMapWrite(uint16_t addr, uint32_t &mapped_addr,
                            uint8_t data) {
  if (addr >= 0x6000 && addr <= 0x7FFF) {
    // write to static ram on rom
    mapped_addr = 0xFFFFFFFF;
    staticRam[addr & 0x1FFF] = data;
    return true;
  }
  if (addr >= 0x8000) {
    if (data & 0x80) {
      // MSB is set, reset serial loading
      registers.load = registers.loadCount = 0;
      registers.control |= 0x0C;
    } else {
      // serial write to load register
      // data comes LSB first, so start at bit 5
      registers.load >>= 1;
      registers.load |= (data & 0x01) << 4;
      registers.loadCount++;

      if (registers.loadCount == 5) {
        // get mapper target register from bits 13 & 14 of the address
        uint8_t targetRegister = (addr >> 13) & 0x03;
        if (targetRegister == 0) { // 0x8000 - 0x9FFF
          registers.control = registers.load & 0x1F;
          // clang-format off
            switch (registers.control & 0x03) {
            case 0: mirrorMode = MIRROR_ONESCREEN_LO; break;
            case 1: mirrorMode = MIRROR_ONESCREEN_HI; break;
            case 2: mirrorMode = MIRROR_VERTICAL; break;
            case 3: mirrorMode = MIRROR_HORIZONTAL; break;
            }
          // clang-format on
        } else if (targetRegister == 1) { // 0xA000 - 0xBFFF
          if (registers.control & 0b10000) {
            // 4k chr bank at PPU 0x0000
            chrBankSelect.lo = registers.load & 0x1F;
          } else {
            // 8k chr bank at PPU 0x0000
            chrBankSelect.full = registers.load & 0x1E;
          }
        } else if (targetRegister == 2) { // 0xC000 - 0xDFFF
          if (registers.control & 0b10000) {
            // 4k chr bank at PPU 0x1000
            chrBankSelect.hi = registers.load & 0x1F;
          }
        } else if (targetRegister == 3) { // 0xE000 - 0xFFFF
          uint8_t prg_mode = (registers.control >> 2) & 0x03;
          switch (prg_mode) {
          case 0:
          case 1:
            // set 32k prg bank at cpu 0x8000
            prgBankSelect.full = (registers.load & 0x0E) >> 1;
            break;
          case 2:
            // fix 16kb prg bank at cpu 0x8000 to first bank
            prgBankSelect.lo = 0;
            // set 16 kb prg bank at cpu 0xC000
            prgBankSelect.hi = registers.load & 0x0F;
            break;
          case 3:
            // set 16kb prg bank at cpu 0x8000
            prgBankSelect.lo = registers.load & 0x0F;
            // fix 16 kb prg bank at cpu 0xC000 to last bank
            prgBankSelect.hi = prgBankCount - 1;
            break;
          }
        }

        // 5 bits written, reset load register
        registers.loadCount = registers.load = 0;
      }
    }
  }
  return false;
}

bool Mapper001::ppuMapRead(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    if (chrBankCount == 0) {
      mapped_addr = addr;
      return true;
    } else {
      if (registers.control & 0b10000) {
        // 4k chr bank mode
        if (addr >= 0x0000 && addr <= 0x0FFF) {
          mapped_addr = chrBankSelect.lo * 0x1000 + (addr & 0x0FFF);
          return true;
        }
        if (addr >= 0x1000 && addr <= 0x1FFF) {
          mapped_addr = chrBankSelect.hi * 0x1000 + (addr & 0x0FFF);
          return true;
        }
      } else {
        // 8k chr bank mode
        mapped_addr = chrBankSelect.full * 0x2000 + (addr & 0x1FFF);
        return true;
      }
    }
  }
  return false;
}

bool Mapper001::ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    if (chrBankCount == 0) {
      // treat as RAM
      mapped_addr = addr;
    }
    return true;
  }
  return false;
}

size_t Mapper001::size() {
  return sizeof(chrBankCount) + sizeof(prgBankCount) + 2 * sizeof(BankSelect) +
         sizeof(registers) + sizeof(mirrorMode) + staticRam.size();
}

std::vector<uint8_t> Mapper001::serialize() {
  std::vector<uint8_t> buffer;
  buffer.resize(size());
  // size_t pod_size = sizeof(Mapper001) - sizeof(staticRam);
  uint32_t idx = 0;
  auto append_buf = [&](uint8_t *data, int len) {
    std::memcpy(buffer.data() + idx, data, len);
    idx += len;
  };
  append_buf(&chrBankCount, sizeof(chrBankCount));
  append_buf(&prgBankCount, sizeof(prgBankCount));
  append_buf((uint8_t *)&chrBankSelect, sizeof(chrBankSelect));
  append_buf((uint8_t *)&prgBankSelect, sizeof(prgBankSelect));
  append_buf((uint8_t *)&registers, sizeof(registers));
  append_buf((uint8_t *)&mirrorMode, sizeof(mirrorMode));
  append_buf(staticRam.data(), staticRam.size());
  return buffer;
}

void Mapper001::deserialize(std::vector<uint8_t> &buffer) {
  staticRam.clear();
  staticRam.resize(STATIC_RAM_SIZE);

  uint32_t idx = 0;
  auto read_buf = [&](uint8_t *out, int len) {
    std::memcpy(out, buffer.data() + idx, len);
    idx += len;
  };
  read_buf(&chrBankCount, sizeof(chrBankCount));
  read_buf(&prgBankCount, sizeof(prgBankCount));
  read_buf((uint8_t *)&chrBankSelect, sizeof(chrBankSelect));
  read_buf((uint8_t *)&prgBankSelect, sizeof(prgBankSelect));
  read_buf((uint8_t *)&registers, sizeof(registers));
  read_buf((uint8_t *)&mirrorMode, sizeof(mirrorMode));
  read_buf(staticRam.data(), staticRam.size());
}

/**
 * iNES mapper 002
 *
 **/

void Mapper002::reset() {
  prgBankSelectLo = 0;
  prgBankSelectHi = prgBankCount - 1;
}

bool Mapper002::cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) {
  if (addr >= 0x8000 && addr <= 0xBFFF) {
    mapped_addr = prgBankSelectLo * 0x4000 + (addr & 0x3FFF);
    return true;
  }
  if (addr >= 0xC000 && addr <= 0xFFFF) {
    mapped_addr = prgBankSelectHi * 0x4000 + (addr & 0x3FFF);
    return true;
  }
  return false;
}

bool Mapper002::cpuMapWrite(uint16_t addr, uint32_t &mapped_addr,
                            uint8_t data) {
  if (addr >= 0x8000 && addr <= 0xFFFF) {
    prgBankSelectLo = data & 0x0F;
  }
  return false;
}

bool Mapper002::ppuMapRead(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    mapped_addr = addr;
    return true;
  }
  return false;
}

bool Mapper002::ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    if (chrBankCount == 0) {
      // treat as RAM
      mapped_addr = addr;
      return true;
    }
  }
  return false;
}

/**
 * iNES mapper 004
 *
 **/

Mapper004::Mapper004(uint8_t prgBankCount, uint8_t chrBankCount)
    : Mapper(prgBankCount, chrBankCount) {
  memory.staticRam.resize(32 * 1024);
}

bool Mapper004::cpuMapRead(uint16_t addr, uint32_t &mapped_addr, uint8_t data) {
  if (addr >= 0x6000 && addr <= 0x7FFF) {
    // read static ram from rom
    mapped_addr = 0xFFFFFFFF;
    data = memory.staticRam[addr & 0x1FFF];
    return true;
  }
  if (addr >= 0x8000 && addr <= 0xFFFF) {
    uint16_t index = (addr - 0x8000) / (1 << 13);
    mapped_addr = memory.prgBank[index] + (addr & 0x1FFF);
    return true;
  }
  return false;
}

bool Mapper004::cpuMapWrite(uint16_t addr, uint32_t &mapped_addr,
                            uint8_t data) {
  if (addr >= 0x6000 && addr <= 0x7FFF) {
    // read static ram from rom
    mapped_addr = 0xFFFFFFFF;
    memory.staticRam[addr & 0x1FFF] = data;
    return true;
  }
  if (addr >= 0x8000 && addr <= 0x9FFF) {
    if (!(addr & 1)) {
      // bank select
      targetRegister = data & 0x07;
      prgBankMode = data & 0x40;
      chrInversion = data & 0x80;
    } else {
      // write to registers
      memory.registers[targetRegister] = data;

      auto writeChr = [&](uint8_t base_idx, bool invert) {
        if (invert) {
          for (auto i = 0; i < 4; i++) {
            if (i % 2)
              memory.chrBank[i + base_idx] =
                  memory.registers[i / 2] * 0x0400 + 0x0400;
            else
              memory.chrBank[i + base_idx] =
                  (memory.registers[i / 2] & 0xFE) * 0x0400;
          }
        } else {
          for (auto i = 0; i < 4; i++)
            memory.chrBank[i + base_idx] = memory.registers[i + 2] * 0x0400;
        }
      };

      // update pointer tables
      if (chrInversion) {
        writeChr(0, false);
        writeChr(4, true);
      } else {
        writeChr(0, true);
        writeChr(4, false);
      }
      memory.prgBank[0] = (memory.registers[6] & 0x3F) * 0x2000;
      memory.prgBank[2] = (prgBankCount * 2 - 2) * 0x2000;
      if (prgBankMode)
        std::swap(memory.prgBank[0], memory.prgBank[2]);

      memory.prgBank[1] = (memory.registers[7] & 0x3F) * 0x2000;
      memory.prgBank[3] = (prgBankCount * 2 - 1) * 0x2000;
    }
    return false;
  }

  if (addr >= 0xA000 && addr <= 0xBFFF) {
    if (!(addr & 1)) {
      // set mirror mode
      if (data & 0x01)
        mirrorMode = MIRROR_HORIZONTAL;
      else
        mirrorMode = MIRROR_VERTICAL;
    } else {
      // prg ram protect, not always needed
      std::cerr << "PRG ram protect not implemented\n";
    }
    return false;
  }

  // config irq
  if (addr >= 0xC000 && addr <= 0xDFFF) {
    if (!(addr & 1)) {
      irq.reload = data;
    } else {
      irq.counter = 0;
    }
    return false;
  }
  if (addr >= 0xE000 && addr <= 0xFFFF) {
    if (!(addr & 1)) {
      irq.enable = true;
      irq.active = false;
    } else {
      irq.enable = true;
    }
    return false;
  }

  return false;
}

bool Mapper004::ppuMapRead(uint16_t addr, uint32_t &mapped_addr) {
  if (addr <= 0x1FFF) {
    mapped_addr = memory.chrBank[addr / 0x0400] + (addr & 0x03FF);
    return true;
  }
  return false;
}

bool Mapper004::ppuMapWrite(uint16_t addr, uint32_t &mapped_addr) {
  return false;
}

void Mapper004::reset() {
  targetRegister = 0;
  prgBankMode = false;
  chrInversion = false;
  mirrorMode = MIRROR_HORIZONTAL;

  std::memset(&irq, 0, sizeof(irq));
  std::memset(&memory.prgBank.front(), 0, sizeof(memory.prgBank));
  std::memset(&memory.chrBank.front(), 0, sizeof(memory.chrBank));

  memory.prgBank[1] = 0x2000;
  memory.prgBank[2] = (prgBankCount * 2 - 2) * 0x2000;
  memory.prgBank[3] = (prgBankCount * 2 - 1) * 0x2000;
}

void Mapper004::scanline() {
  if (irq.counter == 0) {
    irq.counter = irq.reload;
  } else {
    irq.counter--;
  }

  if (irq.counter == 0 && irq.enable)
    irq.active = true;
}
