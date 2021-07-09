#pragma once
#include <array>
#include <bitset>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Cpu6502;

struct Instruction {
  std::string opcode;
  std::string addrmode;
  uint8_t size;
  uint8_t cycles = 2;
  uint8_t opByte; // binary opcode bit vector {aaabbbcc}
                  // https://www.masswerk.at/6502/6502_instruction_set.html
  // uint8_t (Cpu6502::*callback)(void) = NULL;
  std::function<uint8_t(void)> callback;
  std::function<uint8_t(void)> addrmodeCallback;
  // uint8_t (Cpu6502::*addrmodeCallback)(void) = NULL;
  bool implied = false;

  struct Index {
    uint8_t a;
    uint8_t b;
    uint8_t c;

    bool operator==(const Index &p) const {
      return a == p.a && b == p.b && c == p.c;
    }

    static Index unpack(uint8_t op) {
      Index idx;
      // idx.a = op >> 5;
      // idx.b = (op << 3) >> 5;
      // idx.c = (op << 6) >> 6;
      idx.a = op >> 5;
      idx.b = (op & 0b00011100) >> 2;
      idx.c = (op & 0b00000011);
      return idx;
    }

    static uint8_t pack(const Index &idx) {
      return (idx.a << 5) | (idx.b << 2) | idx.c;
    }

    // static uint8_t pack(Index idx) {
    //   return (idx.a << 5) | (idx.b << 2) | idx.c;
    // }
  };
  static const inline Index TABLE_SIZE = {8, 8, 3};

  void print(uint32_t addr = 0) const {
    Index idx = Index::unpack(opByte);
    printf("%d: {%d, %d, %d}\t%s\t%s\tB", addr, idx.a, idx.b, idx.c,
           opcode.c_str(), addrmode.c_str());
    std::cout << std::bitset<8>{opByte} << '\n';
  }

  void toString(std::string &str_out) const {
    Index idx = Index::unpack(opByte);
    str_out = "{" + std::to_string(idx.a) + " ," + std::to_string(idx.b) +
              " ," + std::to_string(idx.c) + "}\t";
    str_out +=
        opcode + "\t" + addrmode; //+ "\t" + std::bitset<8>{opByte}.to_string();
  }

  // set instruction as unknown/illegal, but preserve opByte
  void toUnknown() {
    Instruction dead_cell{"???", "???", 1, 1};
    dead_cell.opByte = opByte;
    *this = dead_cell;
  }

  static void
  setAddressModes(std::unordered_map<uint8_t, Instruction> &instructionMap) {
    instructionMap[Index::pack({1, 0, 0})].addrmode = "abs";
    instructionMap[Index::pack({5, 0, 2})].addrmode = "#";
    instructionMap[Index::pack({0, 0, 0})].addrmode = "impl";
    instructionMap[Index::pack({3, 0, 0})].addrmode = "impl";
    instructionMap[Index::pack({2, 0, 0})].addrmode = "impl";
    instructionMap[Index::pack({3, 3, 0})].addrmode = "ind";
    instructionMap[Index::pack({3, 3, 0})].addrmode = "ind";
    instructionMap[Index::pack({4, 5, 2})].addrmode = "zpg,Y";
    instructionMap[Index::pack({5, 5, 2})].addrmode = "zpg,Y";
    instructionMap[Index::pack({5, 7, 2})].addrmode = "abs,Y";
    for (uint8_t a = 0; a < TABLE_SIZE.a; a++) {
      instructionMap[Index::pack({a, 2, 1})].addrmode = "#";
      instructionMap[Index::pack({a, 6, 1})].addrmode = "abs,Y";
      if (a > 4)
        instructionMap[Index::pack({a, 0, 0})].addrmode = "#";
      if (a < 4)
        instructionMap[Index::pack({a, 2, 2})].addrmode = "A";

      instructionMap[Index::pack({a, 4, 2})].toUnknown();
      if (a != 5) {
        instructionMap[Index::pack({a, 0, 2})].toUnknown();
        instructionMap[Index::pack({a, 7, 0})].toUnknown();
        if (a != 4) {
          instructionMap[Index::pack({a, 6, 2})].toUnknown();
          instructionMap[Index::pack({a, 5, 0})].toUnknown();
        }
      }
      instructionMap[Index::pack({a, 4, 1})].addrmode = "ind,Y";
    }
  }

  static void
  setCycleCounts(std::unordered_map<uint8_t, Instruction> &instructionMap) {

    // cycle counts
    auto getExtraCycles = [](const std::string &addrmode) -> uint8_t {
      if (addrmode == "zpg")
        return 1;
      if (addrmode == "abs" || addrmode == "abs,X" || addrmode == "abs,Y" ||
          addrmode == "zpg,X" || addrmode == "zpg,Y")
        return 2;
      if (addrmode == "X,ind")
        return 4;
      if (addrmode == "ind,Y")
        return 3;
      if (addrmode == "ind")
        return 4;

      return 0;
    };

    for (auto &[key, val] : instructionMap) {
      const std::string &oc = val.opcode;
      if (val.opcode == "INC" || val.opcode == "DEC") {
        if (val.addrmode == "#")
          val.cycles = 2;
        else
          val.cycles = 4;
        if (val.addrmode == "abs,X")
          val.cycles++;
      }
      if (val.opcode == "LSR" || val.opcode == "ASL" || val.opcode == "ROR" ||
          val.opcode == "ROL") {
        if (val.addrmode != "#" && val.addrmode != "impl" &&
            val.addrmode != "A")
          val.cycles = 4;
        if (val.addrmode == "abs,X")
          val.cycles++;
      }
      if (val.opcode == "JMP")
        val.cycles = 1;

      val.cycles += getExtraCycles(val.addrmode);

#define setCycles(op, cyc)                                                     \
  {                                                                            \
    if (val.opcode == op)                                                      \
      val.cycles = cyc;                                                        \
  }
      setCycles("JSR", 6);
      setCycles("BRK", 7);
      setCycles("PHP", 3);
      setCycles("PHA", 3);
      setCycles("PLA", 4);
      setCycles("PLP", 4);
      setCycles("RTI", 6);
      setCycles("RTS", 6);
#undef setCycles

      if (val.opcode == "STA" && val.addrmode == "ind,Y")
        val.cycles = 6;
      if (val.opcode == "STA" &&
          (val.addrmode == "abs,Y" || val.addrmode == "abs,X"))
        val.cycles = 5;
    }
  }
};

static std::vector<std::string> default_address_modes = {
    "X,ind", "zpg", "impl", "abs", "rel", "zpg,X", "impl", "abs,X",
};

static std::vector<std::string> single_byte_instructions = {
    "INX", "INY", "DEX", "DEY", "SEI", "CLD", "CLC", "CLI", "CLV",
    "BRK", "PLA", "PHA", "PLP", "PHP", "RTI", "SEC", "RTS", "TAY",
    "TAX", "TXA", "TYA", "TSX", "TXS", "SED", "NOP",
};

static std::array<std::array<std::string, 8>, 3> default_opcodes = {{
    {"CLC", "SEC", "CLI", "SEI", "STY", "LDY", "CPY", "CPX"}, // 00
    {"ORA", "AND", "EOR", "ADC", "STA", "LDA", "CMP", "SBC"}, // 01
    {"ASL", "ROL", "LSR", "ROR", "STX", "LDX", "DEC", "INC"}  // 10
}};

static const std::vector<std::pair<Instruction::Index, std::string>>
    nondefault_opcodes = {
        {{4, 4, 0}, {"BCC"}}, {{5, 4, 0}, {"BCS"}}, {{7, 4, 0}, {"BEQ"}},
        {{6, 4, 0}, {"BNE"}}, {{1, 3, 0}, {"BIT"}}, {{1, 1, 0}, {"BIT"}},
        {{1, 4, 0}, {"BMI"}}, {{0, 4, 0}, {"BPL"}}, {{0, 0, 0}, {"BRK"}},
        {{2, 4, 0}, {"BVC"}}, {{3, 4, 0}, {"BVS"}}, {{6, 6, 0}, {"CLD"}},
        {{5, 6, 0}, {"CLV"}}, {{6, 2, 2}, {"DEX"}}, {{4, 2, 0}, {"DEY"}},
        {{7, 2, 0}, {"INX"}}, {{6, 2, 0}, {"INY"}}, {{1, 0, 0}, {"JSR"}},
        {{7, 2, 2}, {"NOP"}}, {{3, 2, 0}, {"PLA"}}, {{2, 2, 0}, {"PHA"}},
        {{0, 2, 0}, {"PHP"}}, {{1, 2, 0}, {"PLP"}}, {{2, 0, 0}, {"RTI"}},
        {{3, 0, 0}, {"RTS"}}, {{7, 6, 0}, {"SED"}}, {{5, 2, 2}, {"TAX"}},
        {{5, 2, 0}, {"TAY"}}, {{5, 6, 2}, {"TSX"}}, {{4, 2, 2}, {"TXA"}},
        {{4, 6, 2}, {"TXS"}}, {{4, 6, 0}, {"TYA"}}, {{3, 3, 0}, {"JMP"}},
        {{2, 3, 0}, {"JMP"}},
};

static const std::vector<Instruction::Index> dead_cells = {
    {2, 1, 0}, {2, 5, 0}, {3, 7, 0}, {6, 7, 0}, {4, 4, 2}, {4, 0, 2}, {0, 1, 0},
    {4, 0, 0}, {1, 5, 0}, {0, 3, 0}, {3, 1, 0}, {4, 2, 1}, {4, 7, 2},
};
