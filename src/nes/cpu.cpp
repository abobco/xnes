#include "bus.hpp"

void Cpu6502::connectBus(NesBus *bus) { this->bus = bus; }
uint8_t Cpu6502::read(uint16_t addr) { return bus->readCpu(addr, false); }
void Cpu6502::write(uint16_t addr, uint8_t data) { bus->writeCpu(addr, data); }

void Cpu6502::getInstructionQueue(std::vector<Instruction> &instr_out,
                                  uint16_t n) {
  uint16_t r = n - disassemblyQueue.size();
  getFutureInstructions(instr_out, r);
  instr_out.insert(instr_out.begin(), disassemblyQueue.begin(),
                   disassemblyQueue.end());
}

void Cpu6502::getFutureInstructions(std::vector<Instruction> &instr_out,
                                    uint16_t n) {
  uint32_t opcode = read(registers.PC);
  uint16_t addr = registers.PC;
  instr_out.resize(n);
  for (uint16_t i = 0; i < n; i++) {
    Instruction newInstruction{};
    uint8_t op = read(addr);
    if (Instruction::Index::unpack(op).c < 3) {
      newInstruction = instructionMap[op];
    } else {
      newInstruction.toUnknown();
    }
    addr += newInstruction.size;
    instr_out[i] = newInstruction;
  }
}

void Cpu6502::reset() {
  disasmLog.open("nestest_local.log", std::ios::out);
  disassemblyIndex = -1;
  for (auto &i : disassemblyQueue) {
    i.toUnknown();
  }
  absoluteAddress = 0xFFFC;
  uint16_t lo = read(absoluteAddress);
  uint16_t hi = read(absoluteAddress + 1);
  registers.PC = (hi << 8) | lo;

  registers.reset();
  inputAlu = relativeAddress = absoluteAddress = cycleCount = 0;
  cycles = 8;
}

void Cpu6502::interrupt(uint16_t pcAddress) {
  // push program counter to the stack (16 bit push)
  write(0x0100 + registers.S--, (registers.PC >> 8) & 0x00ff);
  write(0x0100 + registers.S--, (registers.PC) & 0x00ff);

  // push status register to stack
  setStatus(Registers::NES_BFLAG, 0);
  setStatus(Registers::NES_UNUSED, 1);
  setStatus(Registers::NES_INTERRUPT, 1);
  write(0x0100 + registers.S--, registers.P);

  // read new program counter from fixed address
  absoluteAddress = pcAddress;
  uint16_t lo = read(absoluteAddress);
  uint16_t hi = read(absoluteAddress + 1);
  registers.PC = (hi << 8) | lo;
}

void Cpu6502::interruptRequest() {
  if (getStatus(Registers::NES_INTERRUPT) == 0) {
    interrupt(0xFFFE);
    cycles = 7;
  }
}

void Cpu6502::nonMaskableInterrupt() {
  interrupt(0xFFFA);
  cycles = 8;
}

// emulate 1 clock cycle
void Cpu6502::clock() {
  if (cycles == 0) {
    uint16_t addr = registers.PC;
    opcode = read(registers.PC++);
    setStatus(Registers::NES_UNUSED, 1);
    Instruction::Index idx = Instruction::Index::unpack(opcode);
    if (idx.c > 2) {
      std::cerr << "Invalid instruction index! (" << (int)idx.a << ", "
                << (int)idx.b << ", " << (int)idx.c << ")\n";
      // exit(-1);
    } else {
      const Instruction &instr = instructionMap[opcode];
      // if (instr.opcode == "???")
      //   std::cout << "INVALID OPCODE: " << instr.opByte << "\n";
      cycles = instr.cycles;

      // perform instruction
      uint8_t extra_cycles = instr.addrmodeCallback();
      extra_cycles &= instr.callback();

      cycles += extra_cycles;
      cycleCount += cycles;

      if (0) {
        if (disassemblyQueue.size() < MAX_DISASSEMBLY_Q_SIZE) {
          disassemblyQueue.push_back(instr);
          disassemblyIndex++;
        } else {
          std::rotate(disassemblyQueue.begin(), disassemblyQueue.begin() + 1,
                      disassemblyQueue.end());
          disassemblyQueue.back() = instr;
        }
      }
    }
    setStatus(Registers::NES_UNUSED, 1);
  }

  --cycles; // move 1 timestep
}

uint8_t Cpu6502::fetch() {
  //   if (instructionMap[opcode].addrmode != "impl")
  if (!instructionMap[opcode].implied)
    inputAlu = read(absoluteAddress);
  return inputAlu;
}

void Cpu6502::setStatus(Registers::NES_STATUS status, bool val) {
  std::bitset<8> pbits{registers.P};
  pbits[status] = val;
  registers.P = (uint8_t)pbits.to_ulong();
}

bool Cpu6502::getStatus(Registers::NES_STATUS status) const {
  std::bitset<8> pbits{registers.P};
  return pbits[status];
}

void Cpu6502::createInstructionSet() {
  using Opcode = Instruction::Index;

  // populate instruction map
  for (uint8_t c = 0; c < Instruction::TABLE_SIZE.c; c++)
    for (uint8_t b = 0; b < Instruction::TABLE_SIZE.b; b++)
      for (uint8_t a = 0; a < Instruction::TABLE_SIZE.a; a++) {
        uint8_t op = Opcode::pack({a, b, c});
        Instruction newInstruction{
            default_opcodes[c][a],    default_address_modes[b],   2, 2, op,
            instrCallbacks.at("???"), addrModeCallbacks.at("???")};
        instructionMap[op] = newInstruction;
      }

  // fill in non default values
  {
    // opcodes
    for (auto &[key, opcode] : nondefault_opcodes)
      instructionMap[Opcode::pack({key.a, key.b, key.c})].opcode = opcode;

    // address modes
    Instruction::setAddressModes(instructionMap);

    // sizes
    for (auto &[key, val] : instructionMap) {
      for (const std::string &v : single_byte_instructions)
        if (val.opcode == v) {
          val.size = 1;
          break;
        }
      if (val.addrmode.find("abs") != std::string::npos)
        val.size += 1;

      if (val.addrmode == "A") {
        val.size -= 1;
        val.addrmode = "impl";
      }
    }

    // instruction implementations
    for (auto &[key, val] : instructionMap) {
      for (auto &[op, fn] : instrCallbacks) {
        if (val.opcode == op)
          val.callback = fn;
      }

      for (const auto &[k, v] : addrModeCallbacks)
        if (val.addrmode == k) {
          val.addrmodeCallback = v;
          break;
        }
    }

    Instruction::setCycleCounts(instructionMap);

    for (auto &[key, val] : instructionMap) {
      if (val.addrmode == "impl")
        val.implied = true;
    }

    // unknown instructions
    for (const auto &i : dead_cells) {
      instructionMap[Opcode::pack(i)].toUnknown();
      instructionMap[Opcode::pack(i)].callback = instrCallbacks.at("NOP");
      instructionMap[Opcode::pack(i)].addrmodeCallback =
          addrModeCallbacks.at("#");
    }
  }
}

uint8_t Cpu6502::LD_Generic(uint8_t &reg) {
  fetch();
  reg = inputAlu;
  setStatus(Registers::NES_ZERO, reg == 0);
  setStatus(Registers::NES_NEGATIVE, reg & 0x80);
  return 1;
}

void Cpu6502::CMP_Generic(uint8_t &reg) {
  fetch();
  temp = (uint16_t)reg - (uint16_t)inputAlu;
  setStatus(Registers::NES_CARRY, reg >= inputAlu);
  setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
  setStatus(Registers::NES_NEGATIVE, temp & 0x0080);
}

void Cpu6502::setRotateRegisters() {
  setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
  setStatus(Registers::NES_NEGATIVE, temp & 0x0080);
  if (instructionMap[opcode].addrmode == "impl")
    registers.A = temp & 0x00FF;
  else
    write(absoluteAddress, temp & 0x00FF);
}

void Cpu6502::branch() {
  cycles++;
  absoluteAddress = registers.PC + relativeAddress;

  if ((absoluteAddress & 0xFF00) != (registers.PC & 0xFF00))
    cycles++;
  registers.PC = absoluteAddress;
}
