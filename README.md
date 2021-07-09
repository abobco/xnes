# xnes

C++ NES emulator

Made to learn about game console emulation

Try it online [here](https://www.studiostudios.net/xnes/nes.html)(mobile friendly!)

## what is this?

This is a software simulation of the computer hardware in the Nintendo Entertainment System console. 

The (NTSC) NES basically consists of:

- A modified version of the MOS 6502 microprocessor called the RP2A03
    - no decimal mode
    - includes audio generation logic
- A picture processing unit called the 2C02
    - draws images to the screen
    - talks to the cpu with memory mapped registers
    - has no instruction set, draw commands are implemented in hardware
- 2 KB flash memory
- A game cartridge
    - extend the capabilities of the NES
    - come in many different types
    - in addition to game data, usually include extra read/write memory and a configurable memory mapper
    - some cartridges include extra audio and arthmetic hardware

Faithfully implementing this entire system would be a very large project. To limit the project scope, I chose to only implement a limited set of Cartridge types (iNES mappers 000, 001, 002, and 004), and have left out the triangle and DMC audio channels. This means that some NES games will not work in this emulator.

Here's a list of games that worked correctly during my testing:
- Super Mario Bros
- Donkey Kong
- Contra
- Castlevania I
- Mega Man 2
- Lemmings

## Resources:

- [Nesdev](http://wiki.nesdev.com/w/index.php/Nesdev_Wiki): Very detailed description of the NES hardware, almost everything you need to write an emulator is here
- [Emulator tests](https://wiki.nesdev.com/w/index.php/Emulator_tests): Collection of free roms for unit testing an emulator
- [6502 Instruction Set](https://www.masswerk.at/6502/6502_instruction_set.html): Implementation details of the MOS 6502 ISA
- [This NES Emulator tutorial series by One Lone Coder](https://www.youtube.com/watch?v=F8kx56OZQhg&list=PLrOv9FMX8xJHqMvSGB_9G9nZZ_4IgteYf&index=2): Excellent overview of the NES hardware and useful emulation techniques
