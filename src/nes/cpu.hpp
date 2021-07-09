#pragma once
#include "instruction_set.hpp"
#include <bitset>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <utility>

struct NesBus;

// NES 6502
struct Cpu6502 {
  struct Registers {
    enum NES_STATUS {
      NES_CARRY,
      NES_ZERO,
      NES_INTERRUPT,
      NES_DECIMAL,
      NES_BFLAG,
      NES_UNUSED,
      NES_OVERFLOW,
      NES_NEGATIVE,
    };

    uint8_t A, P, X, Y, S;
    uint16_t PC;

    void reset() {
      A = X = Y = 0;
      S = 0xfd;
      P = (1 << NES_UNUSED);
    }
  } registers;

  NesBus *bus = NULL;
  std::unordered_map<uint8_t, Instruction> instructionMap;
  uint8_t inputAlu;
  uint8_t opcode;
  uint16_t temp = 0;
  uint16_t absoluteAddress;
  uint16_t relativeAddress;
  uint16_t cycles;
  uint32_t cycleCount = 0;

  const static uint16_t MAX_DISASSEMBLY_Q_SIZE = 4;
  int16_t disassemblyIndex = -1;
  std::vector<Instruction> disassemblyQueue;

  std::fstream disasmLog;

  void connectBus(NesBus *bus);
  uint8_t read(uint16_t addr);
  void write(uint16_t addr, uint8_t data);

  void getInstructionQueue(std::vector<Instruction> &instr_out, uint16_t n = 8);

  void getFutureInstructions(std::vector<Instruction> &instr_out,
                             uint16_t n = 50);

  void reset();

  void interrupt(uint16_t pcAddress);

  void interruptRequest();

  void nonMaskableInterrupt();

  // emulate 1 clock cycle
  void clock();

  uint8_t fetch();

  void setStatus(Registers::NES_STATUS status, bool val);

  bool getStatus(Registers::NES_STATUS status) const;

  void createInstructionSet();

  uint8_t LD_Generic(uint8_t &reg);

  void CMP_Generic(uint8_t &reg);

  void setRotateRegisters();

  void branch();

// create anonymous member function that returns an int
#define ASM_IMPL(a) [this]() -> uint8_t a

  // 6502 assembly instruction implementations
  // https://www.masswerk.at/6502/6502_instruction_set.html
  const std::unordered_map<std::string, std::function<uint8_t(void)>>
      instrCallbacks = {
          // add with carry
          {"ADC", ASM_IMPL({
             fetch();
             // Add is performed in 16-bit domain for emulation to capture any
             // carry bit, which will exist in bit 8 of the 16-bit word
             temp = (uint16_t)registers.A + (uint16_t)inputAlu +
                    (uint16_t)getStatus(Registers::NES_CARRY);
             setStatus(Registers::NES_CARRY, temp > 255);
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
             setStatus(Registers::NES_OVERFLOW,
                       (~((uint16_t)registers.A ^ (uint16_t)inputAlu) &
                        ((uint16_t)registers.A ^ (uint16_t)temp)) &
                           0x0080);

             setStatus(Registers::NES_NEGATIVE, temp & 0x80);
             registers.A = temp & 0x00FF;

             return 1;
           })},
          // subtract from accumulator with borrow
          {"SBC", ASM_IMPL({
             fetch();

             // invert bottom 8 bits
             uint16_t val = ((uint16_t)inputAlu) ^ 0x00FF;

             // same as addition from here
             temp = (uint16_t)registers.A + val +
                    (uint16_t)getStatus(Registers::NES_CARRY);
             setStatus(Registers::NES_CARRY, temp & 0xFF00);
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
             setStatus(Registers::NES_OVERFLOW,
                       ((uint16_t)registers.A ^ temp) & (temp ^ val) & 0x0080);

             setStatus(Registers::NES_NEGATIVE, temp & 0x80);
             registers.A = temp & 0x00FF;

             return 1;
           })},
          // load data to accumulator
          {"LDA", ASM_IMPL({ return LD_Generic(registers.A); })},
          // load data to X register
          {"LDX", ASM_IMPL({ return LD_Generic(registers.X); })},
          // load data to Y register
          {"LDY", ASM_IMPL({ return LD_Generic(registers.Y); })},
          // store acccumulator to memory
          {"STA", ASM_IMPL({
             write(absoluteAddress, registers.A);
             return 0;
           })},
          {"STX", ASM_IMPL({
             write(absoluteAddress, registers.X);
             return 0;
           })},
          {"STY", ASM_IMPL({
             write(absoluteAddress, registers.Y);
             return 0;
           })},
          // set interrupt flag
          {"SEI", ASM_IMPL({
             setStatus(Registers::NES_INTERRUPT, true);
             return 0;
           })},
          // clear interrupt flag
          {"CLI", ASM_IMPL({
             setStatus(Registers::NES_INTERRUPT, false);
             return 0;
           })},
          // set carry flag
          {"SEC", ASM_IMPL({
             setStatus(Registers::NES_CARRY, true);
             return 0;
           })},
          // clear carry flag
          {"CLC", ASM_IMPL({
             setStatus(Registers::NES_CARRY, false);
             return 0;
           })},
          // set decimal flag
          {"SED", ASM_IMPL({
             setStatus(Registers::NES_DECIMAL, true);
             return 0;
           })},
          // clear decimal flag
          {"CLD", ASM_IMPL({
             setStatus(Registers::NES_DECIMAL, false);
             return 0;
           })},
          // clear overflow flag
          {"CLV", ASM_IMPL({
             setStatus(Registers::NES_OVERFLOW, false);
             return 0;
           })},
          // logical shift right
          {"LSR", ASM_IMPL({
             fetch();
             setStatus(Registers::NES_CARRY, inputAlu & 0x0001);
             temp = inputAlu >> 1;
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0x0000);
             setStatus(Registers::NES_NEGATIVE, temp & 0x0080);
             if (instructionMap[opcode].implied)
               registers.A = temp & 0x00FF;
             else
               write(absoluteAddress, temp & 0x00FF);
             return 0;
           })},
          // arithmetic shift left
          {"ASL", ASM_IMPL({
             fetch();
             temp = (uint16_t)inputAlu << 1;
             setStatus(Registers::NES_CARRY, (temp & 0xFF00) > 0);
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0x0000);
             setStatus(Registers::NES_NEGATIVE, temp & 0x0080);
             if (instructionMap[opcode].implied)
               registers.A = temp & 0x00FF;
             else
               write(absoluteAddress, temp & 0x00FF);
             return 0;
           })},
          // rotate 1 bit left
          {"ROL", ASM_IMPL({
             fetch();
             temp = getStatus(Registers::NES_CARRY) | (uint16_t)(inputAlu << 1);
             setStatus(Registers::NES_CARRY, temp & 0xFF00);
             setRotateRegisters();
             return 0;
           })},
          // rotate 1 bit right
          {"ROR", ASM_IMPL({
             fetch();
             temp = (uint16_t)(getStatus(Registers::NES_CARRY) << 7) |
                    (inputAlu >> 1);
             setStatus(Registers::NES_CARRY, inputAlu & 0x0001);
             setRotateRegisters();
             return 0;
           })},
          // return from interrupt
          {"RTI", ASM_IMPL({
             registers.P = read(0x0100 + (++registers.S));
             registers.P &= ~(1 << Registers::NES_BFLAG);
             registers.P &= ~(1 << Registers::NES_UNUSED);

             registers.PC = (uint16_t)read(0x0100 + (++registers.S));
             registers.PC |= (uint16_t)read(0x0100 + (++registers.S)) << 8;
             return 0;
           })},
          // compare with accumulator
          {"CMP", ASM_IMPL({
             CMP_Generic(registers.A);
             return 1;
           })},
          // compare with X register
          {"CPX", ASM_IMPL({
             CMP_Generic(registers.X);
             return 0;
           })},
          // compare with Y register
          {"CPY", ASM_IMPL({
             CMP_Generic(registers.Y);
             return 0;
           })},
          // decrement value at location
          {"DEC", ASM_IMPL({
             fetch();
             temp = inputAlu - 1;
             write(absoluteAddress, temp & 0x00FF);
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
             setStatus(Registers::NES_NEGATIVE, temp & 0x0080);
             return 0;
           })},
          // transfer x register to stack pointer
          {"TXS", ASM_IMPL({
             registers.S = registers.X;
             return 0;
           })},
          // transfer stack pointer to X register
          {"TSX", ASM_IMPL({
             registers.X = registers.S;
             setStatus(Registers::NES_ZERO, registers.X == 0);
             setStatus(Registers::NES_NEGATIVE, registers.X & 0x80);
             return 0;
           })},
          // transfer Y to accumulator
          {"TYA", ASM_IMPL({
             registers.A = registers.Y;
             setStatus(Registers::NES_ZERO, registers.A == 0);
             setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 0;
           })},
          // transfer X to accumulator
          {"TXA", ASM_IMPL({
             registers.A = registers.X;
             setStatus(Registers::NES_ZERO, registers.A == 0);
             setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 0;
           })},
          // transfer accumulator to X
          {"TAX", ASM_IMPL({
             registers.X = registers.A;
             setStatus(Registers::NES_ZERO, registers.X == 0);
             setStatus(Registers::NES_NEGATIVE, registers.X & 0x80);
             return 0;
           })},
          // transfer accumulator to Y
          {"TAY", ASM_IMPL({
             registers.Y = registers.A;
             setStatus(Registers::NES_ZERO, registers.Y == 0);
             setStatus(Registers::NES_NEGATIVE, registers.Y & 0x80);
             return 0;
           })},
          // branch if carry set
          {"BCS", ASM_IMPL({
             if (getStatus(Registers::NES_CARRY) == 1)
               branch();
             return 0;
           })},
          // branch if carry cleared
          {"BCC", ASM_IMPL({
             if (getStatus(Registers::NES_CARRY) == 0)
               branch();
             return 0;
           })},
          // branch if equal
          {"BEQ", ASM_IMPL({
             if (getStatus(Registers::NES_ZERO) == 1)
               branch();
             return 0;
           })},
          // branch if not equal
          {"BNE", ASM_IMPL({
             if (getStatus(Registers::NES_ZERO) == 0)
               branch();
             return 0;
           })},
          // branch if positive
          {"BPL", ASM_IMPL({
             if (getStatus(Registers::NES_NEGATIVE) == 0)
               branch();
             return 0;
           })},
          // branch if negative
          {"BMI", ASM_IMPL({
             if (getStatus(Registers::NES_NEGATIVE) == 1)
               branch();
             return 0;
           })},
          // branch if overflow set
          {"BVS", ASM_IMPL({
             if (getStatus(Registers::NES_OVERFLOW) == 1)
               branch();
             return 0;
           })},
          // branch if overflow cleared
          {"BVC", ASM_IMPL({
             if (getStatus(Registers::NES_OVERFLOW) == 0)
               branch();
             return 0;
           })},
          // test if 1 or more bits are set in location
          {"BIT", ASM_IMPL({
             fetch();
             temp = registers.A & inputAlu;
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
             setStatus(Registers::NES_NEGATIVE, inputAlu & (1 << 7));
             setStatus(Registers::NES_OVERFLOW, inputAlu & (1 << 6));
             return 0;
           })},

          // break (programmed interrupt)
          {"BRK", ASM_IMPL({
             registers.PC++;
             setStatus(Registers::NES_INTERRUPT, 1);
             write(0x0100 + registers.S--, (registers.PC >> 8) & 0x00FF);
             write(0x0100 + registers.S--, registers.PC & 0x00FF);

             setStatus(Registers::NES_BFLAG, 1);
             write(0x0100 + registers.S--, registers.P);
             setStatus(Registers::NES_BFLAG, 0);

             registers.PC =
                 (uint16_t)read(0xFFFE) | ((uint16_t)read(0xFFFF) << 8);
             return 0;
           })},
          // decrement x register
          {"DEX", ASM_IMPL({
             registers.X--;
             setStatus(Registers::NES_ZERO, registers.X == 0);
             setStatus(Registers::NES_NEGATIVE, registers.X & 0x80);
             return 0;
           })},
          // decrement y register
          {"DEY", ASM_IMPL({
             registers.Y--;
             setStatus(Registers::NES_ZERO, registers.Y == 0);
             setStatus(Registers::NES_NEGATIVE, registers.Y & 0x80);
             return 0;
           })},
          // increment at location
          {"INC", ASM_IMPL({
             fetch();
             temp = inputAlu + 1;
             write(absoluteAddress, temp & 0x00FF);
             setStatus(Registers::NES_ZERO, (temp & 0x00FF) == 0);
             setStatus(Registers::NES_NEGATIVE, temp & 0x0080);
             return 0;
           })},
          // increment Y register
          {"INY", ASM_IMPL({
             registers.Y++;
             setStatus(Registers::NES_ZERO, registers.Y == 0);
             setStatus(Registers::NES_NEGATIVE, registers.Y & 0x0080);
             return 0;
           })},
          // increment X register
          {"INX", ASM_IMPL({
             registers.X++;
             setStatus(Registers::NES_ZERO, registers.X == 0);
             setStatus(Registers::NES_NEGATIVE, registers.X & 0x0080);
             return 0;
           })},
          // jump to absolute address
          {"JMP", ASM_IMPL({
             registers.PC = absoluteAddress;
             return 0;
           })},
          // jump subroutine
          {"JSR", ASM_IMPL({
             registers.PC--;
             write(0x0100 + registers.S--, (registers.PC >> 8) & 0x00FF);
             write(0x0100 + registers.S--, registers.PC & 0x00FF);
             registers.PC = absoluteAddress;
             return 0;
           })},
          // bitwise OR on accumulator
          {"ORA", ASM_IMPL({
             fetch();
             registers.A |= inputAlu;
             setStatus(Registers::NES_ZERO, registers.A == 0);
             setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 1;
           })},
          // bitwise XOR on accumulator
          {"EOR", ASM_IMPL({
             fetch();
             registers.A ^= inputAlu;
             setStatus(Registers::NES_ZERO, registers.A == 0);
             setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 1;
           })},
          // push status register to stack
          {"PHP", ASM_IMPL({
             write(0x0100 + registers.S, registers.P | 16 | 32);
             setStatus(Registers::NES_BFLAG, 0);
             setStatus(Registers::NES_UNUSED, 0);
             registers.S--;
             return 0;
           })},
          // push accumulator to stack
          {"PHA", ASM_IMPL({
             write(0x0100 + registers.S--, registers.A);
             return 0;
           })},
          // pull accumulator from stack
          {"PLA", ASM_IMPL({
             registers.A = read(0x0100 + (++registers.S));
             setStatus(Registers::NES_ZERO, registers.A == 0);
             setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 0;
           })},
          // pull status register from stack
          {"PLP", ASM_IMPL({
             registers.P = read(0x0100 + (++registers.S));
             setStatus(Registers::NES_UNUSED, 1);
             //  setStatus(Registers::NES_ZERO, registers.A == 0);
             //  setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 0;
           })},
          // return from subroutine
          {"RTS", ASM_IMPL({
             registers.PC = (uint16_t)read(0x0100 + (++registers.S));
             registers.PC |= (uint16_t)read(0x0100 + (++registers.S)) << 8;
             registers.PC++;
             return 0;
           })},
          {"AND", ASM_IMPL({
             fetch();
             registers.A &= inputAlu;
             setStatus(Registers::NES_ZERO, registers.A == 0);
             setStatus(Registers::NES_NEGATIVE, registers.A & 0x80);
             return 1;
           })},
          // No operation
          {"NOP", ASM_IMPL({
             switch (opcode) {
             case 0x1C:
             case 0x3C:
             case 0x5C:
             case 0x7C:
             case 0xDC:
             case 0xFC:
               return 1;
               break;
             }
             return 0;
           })},
          {"???", ASM_IMPL({
             std::string instr;
             instructionMap[opcode].toString(instr);
             std::cerr << "Invalid instruction: " << instr << "\n";
             return 0;
           })},
  };

  // 6502 address mode implementations
  const std::unordered_map<std::string, std::function<uint8_t(void)>>
      addrModeCallbacks = {
          {"abs", ASM_IMPL({
             uint16_t lo = read(registers.PC++);
             uint16_t hi = read(registers.PC++);
             absoluteAddress = (hi << 8) | lo;
             return 0;
           })},
          {"abs,X", ASM_IMPL({
             uint16_t lo = read(registers.PC++);
             uint16_t hi = read(registers.PC++);
             absoluteAddress = (hi << 8) | lo;
             absoluteAddress += registers.X;
             if ((absoluteAddress & 0xFF00) != (hi << 8))
               return 1;
             return 0;
           })},
          {"abs,Y", ASM_IMPL({
             uint16_t lo = read(registers.PC++);
             uint16_t hi = read(registers.PC++);
             absoluteAddress = (hi << 8) | lo;
             absoluteAddress += registers.Y;
             if ((absoluteAddress & 0xFF00) != (hi << 8))
               return 1;
             return 0;
           })},
          {"#", ASM_IMPL({
             absoluteAddress = registers.PC++;
             return 0;
           })},
          {"impl", ASM_IMPL({
             inputAlu = registers.A;
             return 0;
           })},
          {"rel", ASM_IMPL({
             relativeAddress = read(registers.PC++);
             if (relativeAddress & 0x80)
               relativeAddress |= 0xFF00;
             return 0;
           })},
          {"zpg", ASM_IMPL({
             absoluteAddress = (read(registers.PC++)) & 0x00FF;
             return 0;
           })},
          {"zpg,X", ASM_IMPL({
             absoluteAddress = (read(registers.PC++) + registers.X) & 0x00FF;
             return 0;
           })},
          {"zpg,Y", ASM_IMPL({
             absoluteAddress = (read(registers.PC++) + registers.Y) & 0x00FF;
             return 0;
           })},

          /**
           * Indirect addressing:
           *  - 16 bit logical address -> 16 bit absolute address
           *  - Analogous to pointers on modern systems
           *
           * NES hardware has a bug in the implementation of this addressing
           * mode:
           *  - If the low byte of the given address is 0xFF, then a page
           * boundary must be crossed to read the high byte of the actual
           * address
           *  - This doesn't work on NES hardware. Instead, it wraps back around
           * to the same page and returns an invalid address
           *  - We have to implement this bug to accurately emulate the hardware
           */
          {"ind", ASM_IMPL({
             uint16_t lo = read(registers.PC++);
             uint16_t hi = read(registers.PC++);
             uint16_t ptr = (hi << 8) | lo;
             if (lo == 0x00FF) { // page boundary hardware bug
               absoluteAddress = (read(ptr & 0xFF00) << 8) | read(ptr);
             } else { // normal behavior
               absoluteAddress = (read(ptr + 1) << 8) | read(ptr);
             }

             return 0;
           })},
          {"X,ind", ASM_IMPL({
             uint16_t t = read(registers.PC++);
             uint16_t lo = read((uint16_t)(t + (uint16_t)registers.X) & 0x00FF);
             uint16_t hi =
                 read((uint16_t)(t + (uint16_t)registers.X + 1) & 0x00FF);
             absoluteAddress = (hi << 8) | lo;

             return 0;
           })},
          {"ind,Y", ASM_IMPL({
             uint16_t t = read(registers.PC++);
             uint16_t lo = read(t & 0x00FF);
             uint16_t hi = read((t + 1) & 0x00FF);
             absoluteAddress = (hi << 8) | lo;
             absoluteAddress += registers.Y;

             if ((absoluteAddress & 0xFF00) != (hi << 8))
               return 1; // extra clock cycle to load new page

             return 0;
           })},
          {"???", ASM_IMPL({
             std::cerr << "Invalid address mode\n";
             return 0;
           })},
  };
};
