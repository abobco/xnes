#include "ppu.hpp"

NesRenderer::Sprite<NesRenderer::NES_WIDTH, NesRenderer::NES_HEIGHT> &
Ppu2C02::getFramebuffer(bool active) {
  if (use_vsync) {
    if (odd)
      return renderer.framebuffers[0 + active];
    else
      return renderer.framebuffers[1 - active];
  }
  return renderer.framebuffer;
}

NesPixel Ppu2C02::getColorFromPalette(uint8_t palette, uint8_t pixel) {
  return NesRenderer::palettes[ppuRead(0x3F00 + (palette << 2) + pixel) & 0x3F];
}

NesRenderer::Sprite<128, 128> &Ppu2C02::getPatternTable(uint8_t i,
                                                        uint8_t palette) {
  // draw chr ROM into the framebuffer with the given palette

  // for each 16x16 tile
  for (uint16_t y = 0; y < 16; y++) {
    for (uint16_t x = 0; x < 16; x++) {
      uint16_t offset = y * NesRenderer::NES_WIDTH + x * 16;

      for (uint16_t row = 0; row < 8; row++) {
        // read both bitplanes of character to extract 2 bit pixel value
        // each character is stored as 64 bits lsb, 64 bits msb
        uint8_t tile_lsb = ppuRead(i * 0x1000 + offset + row);
        uint8_t tile_msb = ppuRead(i * 0x1000 + offset + row + 8);

        // combine 8 bit words into pixel index
        for (uint16_t col = 0; col < 8; col++) {
          // only need the lsb b/c we are shifting 1 bit right
          uint8_t pixel = (tile_lsb & 0x01) | ((tile_msb & 0x01) << 1);
          tile_lsb >>= 1;
          tile_msb >>= 1;

          NesPixel p = getColorFromPalette(palette, pixel);
          auto t = 0;
          // DUMP(t);
          renderer.spritePatternTable[i].setPixel(x * 8 + (7 - col),
                                                  y * 8 + row, p);
        }
      }
    }
  }

  return renderer.spritePatternTable[i];
}

NesRenderer::Sprite<NesRenderer::NES_WIDTH, NesRenderer::NES_HEIGHT> &
Ppu2C02::getNameTable(uint8_t i) {
  return renderer.spriteNameTable[i];
}

uint8_t Ppu2C02::cpuRead(uint16_t addr, bool rdOnly) {
  uint8_t data;
  if (rdOnly) {
    switch (addr) {
    case PPU_CTRL:
      data = registers.CTRL.val;
      break;
    case PPU_MASK:
      data = registers.MASK.val;
      break;
    case PPU_STATUS:
      data = registers.STATUS.val;
      break;
    default:
      break;
    }
  } else {
    // (scanline == 241 && cycle == 1)
    static const unsigned vbl_cycle = 1, vbl_scanline = 241;
    switch (addr) {
    case PPU_STATUS:
      data = (registers.STATUS.val & 0xE0) | (dataBuffer & 0x1F);
      registers.STATUS.vblank = 0;
      addressLatch = 0;
      break;
    case OAM_DATA:
      data = OAM.memory.data[OAM.address];
      break;
    case PPU_DATA:
      // nametable ram reads: delay 1 cycle
      // palette memory: read with no delay
      data = dataBuffer;                     // get previous frame data
      dataBuffer = ppuRead(registers.v.val); // load next cycle's data

      if (registers.v.val >= 0x3F00)
        data = dataBuffer; // read with no delay for palette memory

      // increment vram address depending on control register value
      registers.v.val += (registers.CTRL.vramAddressInc ? 32 : 1);
      break;
    default:
      break;
    }
  }
  return data;
}

void Ppu2C02::cpuWrite(uint16_t addr, uint8_t data) {
  switch (addr) {
  case PPU_CTRL:
    registers.CTRL.val = data;
    registers.t.nameTableX = registers.CTRL.nametableX;
    registers.t.nameTableY = registers.CTRL.nametableY;
    break;
  case PPU_MASK:
    registers.MASK.val = data;
    break;
  case OAM_ADDR:
    OAM.address = data;
    break;
  case OAM_DATA:
    OAM.memory.data[OAM.address] = data;
    break;
  case PPU_SCROLL:
    if (addressLatch == 0) {
      registers.fineX = data & 0x07;
      registers.t.coarseX = data >> 3;
      addressLatch = 1;
    } else {
      registers.t.fineY = data & 0x07;
      registers.t.coarseY = data >> 3;
      addressLatch = 0;
    }
    break;
  case PPU_ADDR:
    if (addressLatch == 0) {
      registers.t.val =
          (uint16_t)((data & 0x3F) << 8) | (registers.t.val & 0x00FF);
      addressLatch = 1;
    } else {
      registers.t.val = (registers.t.val & 0xFF00) | data;
      registers.v = registers.t;
      addressLatch = 0;
    }
    break;
  case PPU_DATA:
    ppuWrite(registers.v.val, data);
    // increment vram address depending on control register value
    registers.v.val += (registers.CTRL.vramAddressInc ? 32 : 1);
    break;
  default:
    break;
  }
}

uint8_t &Ppu2C02::mirroredNameTableEntry(uint16_t addr) {
  if (rom->getMirrorMode() == Mapper::MIRROR_VERTICAL) {
    return nameTable[(addr / 0x0400) % 2][addr & 0x03FF];
  } else if (rom->getMirrorMode() == Mapper::MIRROR_HORIZONTAL) {
    return nameTable[(addr / 0x0800) % 2][addr & 0x03FF];
  }

  return nameTable[0][addr & 0x03FF];
}

uint8_t Ppu2C02::ppuRead(uint16_t addr, bool rdOnly) {
  uint8_t data = 0x00;
  addr &= 0x3FFF;

  if (rom->ppuRead(addr, data)) {
  } else if (addr <= 0x1FFF) {
    // map a phyical address
    data = patternTable[(addr & 0x1000) >> 12][addr & 0x0FFF];

  } else if (addr <= 0x3EFF) {
    addr &= 0x0FFF;
    data = mirroredNameTableEntry(addr);

  } else if (addr <= 0x3FFF) {
    addr &= 0x001F;
    if (addr == 0x0010)
      addr = 0x0000;
    if (addr == 0x0014)
      addr = 0x0004;
    if (addr == 0x0018)
      addr = 0x0008;
    if (addr == 0x001C)
      addr = 0x000C;
    data = paletteTable[addr] & (registers.MASK.grayscale ? 0x30 : 0x3F);
  }
  return data;
}

void Ppu2C02::ppuWrite(uint16_t addr, uint8_t data) {
  addr &= 0x3FFF;
  if (rom->ppuWrite(addr, data)) {
  } else if (addr <= 0x1FFF) {
    patternTable[(addr & 0x1000) >> 12][addr & 0x0FFF] = data;
  } else if (addr <= 0x3EFF) {
    addr &= 0x0FFF;
    mirroredNameTableEntry(addr) = data;
  } else if (addr <= 0x3FFF) {
    addr &= 0x001F;
    if (addr == 0x0010)
      addr = 0x0000;
    if (addr == 0x0014)
      addr = 0x0004;
    if (addr == 0x0018)
      addr = 0x0008;
    if (addr == 0x001C)
      addr = 0x000C;
    paletteTable[addr] = data;
  }
}

void Ppu2C02::reset() {
  addressLatch = dataBuffer = scanline = cycle = 0;
  std::memset(&bg, 0, sizeof(bg));
  std::memset(&registers, 0, sizeof(registers));
  odd = false;
}

bool Ppu2C02::renderEnabled() {
  return registers.MASK.showBg || registers.MASK.showSprites;
}

void Ppu2C02::scrollX() {
  if (renderEnabled()) {
    if (registers.v.coarseX == 31) {
      registers.v.coarseX = 0;
      // flip nametable bit
      registers.v.nameTableX = ~registers.v.nameTableX;
    } else {
      registers.v.coarseX++;
    }
  }
}

void Ppu2C02::scrollY() {
  if (renderEnabled()) {
    if (registers.v.fineY < 7) {
      registers.v.fineY++;
    } else {
      registers.v.fineY = 0;

      if (registers.v.coarseY == 29) {
        // swap verticle nametable targets
        registers.v.coarseY = 0;
        registers.v.nameTableY = ~registers.v.nameTableY;
      } else if (registers.v.coarseY == 31) {
        // wrap around current nametable
        registers.v.coarseY = 0;
      } else {
        registers.v.coarseY++;
      }
    }
  }
}

void Ppu2C02::txAddressX() {
  if (renderEnabled()) {
    registers.v.nameTableX = registers.t.nameTableX;
    registers.v.coarseX = registers.t.coarseX;
  }
}

void Ppu2C02::txAddressY() {
  if (renderEnabled()) {
    registers.v.nameTableY = registers.t.nameTableY;
    registers.v.coarseY = registers.t.coarseY;
    registers.v.fineY = registers.t.fineY;
  }
}

void Ppu2C02::loadBackgroundShifters() {
  // shift 1 bit, feeding the pixel compositor w/ binary frame info
  // top 8 bits = current 8 pixels being drawn
  // bot 8 bits = next 8 pixels to drawn
  bg.ShiftPatternLo = (bg.ShiftPatternLo & 0xFF00) | bg.NextTileLsb;
  bg.ShiftPatternHi = (bg.ShiftPatternHi & 0xFF00) | bg.NextTileMsb;

  // take bottom 2 bits of of the attribute word, representing the palletes
  // for the next 2 sets of 8 pixels
  bg.ShiftAttribLo =
      (bg.ShiftAttribLo & 0xFF00) | ((bg.NextTileAttrib & 0b01) ? 0xFF : 0x00);
  bg.ShiftAttribHi =
      (bg.ShiftAttribHi & 0xFF00) | ((bg.NextTileAttrib & 0b10) ? 0xFF : 0x00);
}

void Ppu2C02::updateShifters() {
  // Each cycle, pattern and attribute information is shifted by 1 bit.
  // This means the state of the shifter is in sync w/ the 8 pixels being
  // drawn to the scanline
  if (registers.MASK.showBg) {
    bg.ShiftPatternLo <<= 1;
    bg.ShiftPatternHi <<= 1;

    bg.ShiftAttribLo <<= 1;
    bg.ShiftAttribHi <<= 1;
  }

  if (registers.MASK.showSprites && cycle >= 1 && cycle < 258) {
    for (auto i = 0; i < sprites.count; i++) {
      if (sprites.scanlineSprites[i].x > 0) {
        sprites.scanlineSprites[i].x--;
      } else {
        sprites.shiftPatternLo[i] <<= 1;
        sprites.shiftPatternHi[i] <<= 1;
      }
    }
  }
}

void Ppu2C02::clock() {
  if (scanline >= -1 && scanline < 240) {
    if (scanline == 0 && cycle == 0 && odd && renderEnabled())
      cycle = 1; // odd frame, skip cycle
    if (scanline == -1 && cycle == 1) {
      // new frame
      registers.STATUS.vblank = 0;
      registers.STATUS.spriteOverflow = 0;
      registers.STATUS.spriteZeroHit = 0;
      // clear sprite shifters
      std::memset(sprites.shiftPatternLo, 0, 8);
      std::memset(sprites.shiftPatternHi, 0, 8);
    }
    if ((cycle >= 2 && cycle < 258) || (cycle >= 321 && cycle < 338)) {
      updateShifters();

      // collect visible data from the shifters
      switch ((cycle - 1) % 8) {
      case 0:
        loadBackgroundShifters();
        bg.NextTileId = ppuRead(0x2000 | (registers.v.val & 0x0FFF));
        break;
      case 2:
        bg.NextTileAttrib = ppuRead(0x23C0 | (registers.v.nameTableY << 11) |
                                    (registers.v.nameTableX << 10) |
                                    ((registers.v.coarseY >> 2) << 3) |
                                    (registers.v.coarseX >> 2));
        // find the 2 bits of palette info
        if (registers.v.coarseY & 0x02)
          bg.NextTileAttrib >>= 4;
        if (registers.v.coarseX & 0x02)
          bg.NextTileAttrib >>= 2;
        bg.NextTileAttrib &= 0x03;
        break;
      case 4:
        // fetch background tile LSB bit plane from pattern memory
        bg.NextTileLsb =
            ppuRead((registers.CTRL.bgAddress << 12) +
                    ((uint16_t)bg.NextTileId << 4) + registers.v.fineY);
        break;
      case 6:
        // fetch background tile MSB bit plane from pattern memory
        // same as LSB but with 8 bit offset added
        bg.NextTileMsb =
            ppuRead((registers.CTRL.bgAddress << 12) +
                    ((uint16_t)bg.NextTileId << 4) + registers.v.fineY + 8);
        break;
      case 7:
        // increment background tile pointer
        scrollX();
        break;
      }
    }

    if (cycle == 256) // end of scanline
      scrollY();

    if (cycle == 257) { // reset x position
      loadBackgroundShifters();
      txAddressX();
    }

    if (cycle == 338 || cycle == 340) // read tile id at end of scanline
      bg.NextTileId = ppuRead(0x2000 | (registers.v.val & 0x0FFF));

    if (scanline == -1 && cycle >= 280 && cycle < 305)
      txAddressY(); // end of vblank, reset y address for rendering

    if (cycle == 257 && scanline >= 0) {
      // clear sprite memory
      std::memset(sprites.scanlineSprites, 0xFF,
                  8 * sizeof(ObjectAttributeMemory::Entry));
      sprites.count = 0;
      std::memset(sprites.shiftPatternLo, 0, 8);
      std::memset(sprites.shiftPatternHi, 0, 8);

      // find visible sprites on next scanline
      uint8_t entry_count = 0;
      sprites.zeroHitPossible = false;
      while (entry_count < 64 && sprites.count < 9) {
        // find signed y distance from sprite to scanline
        int16_t sd_y =
            ((int16_t)scanline - (int16_t)OAM.memory.entries[entry_count].y);

        if (sd_y >= 0 && sd_y < (registers.CTRL.spriteSize ? 16 : 8)) {
          // sprite is visible, copy OAM to scanline sprite cache
          if (sprites.count < 8) {
            if (entry_count == 0) // zero sprite is visible
              sprites.zeroHitPossible = true;

            std::memcpy(&sprites.scanlineSprites[sprites.count++],
                        &OAM.memory.entries[entry_count],
                        sizeof(ObjectAttributeMemory::Entry));
          }
        }

        entry_count++;
      }
      registers.STATUS.spriteOverflow = (sprites.count > 8);
    }

    if (cycle == 340) {
      // end of scanline, prepare sprite shifters with visible sprites
      auto copy_sprite_address_lo = [&](uint16_t &s_pattern_addr_lo, uint8_t i,
                                        bool flip) {
        s_pattern_addr_lo =
            // pattern table offset (0 or 4 KB)
            (registers.CTRL.spriteAdress8x8 << 12) |
            // tile id
            (sprites.scanlineSprites[i].id << 4) |
            // row within the tile
            (flip ? (7 - (scanline - sprites.scanlineSprites[i].y))
                  : (scanline - sprites.scanlineSprites[i].y));
      };
      auto copy_sprite_address_lo_8x16 = [&](uint16_t &s_pattern_addr_lo,
                                             uint8_t i, uint8_t tile_offset,
                                             bool flip) {
        s_pattern_addr_lo =
            ((sprites.scanlineSprites[i].id & 0x01) << 12) |
            (((sprites.scanlineSprites[i].id & 0xFE) + tile_offset) << 4) |
            (flip ? (7 - (scanline - sprites.scanlineSprites[i].y) & 0x07)
                  : ((scanline - sprites.scanlineSprites[i].y) & 0x07));
      };
      for (auto i = 0; i < sprites.count; i++) {
        uint8_t s_pattern_data_lo, s_pattern_data_hi;
        uint16_t s_pattern_addr_lo, s_pattern_addr_hi;

        // find memory addresses with pattern data
        if (!registers.CTRL.spriteSize) {
          // 8x8 sprite mode
          copy_sprite_address_lo(s_pattern_addr_lo, i,
                                 sprites.scanlineSprites[i].attributes & 0x80);
        } else {
          // 8x16 sprite mode
          copy_sprite_address_lo_8x16(
              s_pattern_addr_lo, i,
              (scanline - sprites.scanlineSprites[i].y) >= 8,
              sprites.scanlineSprites[i].attributes & 0x80);
        }

        // hi bit plane is always 8 byte ofset from lo plane
        s_pattern_addr_hi = s_pattern_addr_lo + 8;
        // read sprite pattern data
        s_pattern_data_lo = ppuRead(s_pattern_addr_lo);
        s_pattern_data_hi = ppuRead(s_pattern_addr_hi);

        if (sprites.scanlineSprites[i].attributes & 0x40) {
          auto flip_byte = [](uint8_t b) {
            b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
            b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
            b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
            return b;
          };

          // flip horizontally
          s_pattern_data_lo = flip_byte(s_pattern_data_lo);
          s_pattern_data_hi = flip_byte(s_pattern_data_hi);
        }

        // load pattern data to sprite shift registers
        sprites.shiftPatternLo[i] = s_pattern_data_lo;
        sprites.shiftPatternHi[i] = s_pattern_data_hi;
      }
    }
  }

  if (scanline == 240) {
    // post render scanline
  }

  if (scanline == 241 && cycle == 1) {
    // end of frame
    registers.STATUS.vblank = 1;
    if (registers.CTRL.vblankNmi && !nmiIgnore)
      nmi = true;
    nmiIgnore = false;
  }

  // pixel composition

  // background
  uint8_t bg_pix = 0, bg_palette = 0;
  if (registers.MASK.showBg) {
    // selet relevant bit depending on fine x scroll
    uint16_t bit_mux = 0x8000 >> registers.fineX;

    // extract bit plane pixels from the shifter
    uint8_t pix_lo = (bg.ShiftPatternLo & bit_mux) > 0;
    uint8_t pix_hi = (bg.ShiftPatternHi & bit_mux) > 0;
    bg_pix = (pix_hi << 1) | pix_lo;

    // get palette
    uint8_t bg_pal_lo = (bg.ShiftAttribLo & bit_mux) > 0;
    uint8_t bg_pal_hi = (bg.ShiftAttribHi & bit_mux) > 0;
    bg_palette = (bg_pal_hi << 1) | bg_pal_lo;
  }

  // foreground
  uint8_t fg_pix = 0, fg_palette = 0, fg_priority = 0;
  if (registers.MASK.showSprites) {
    // iterate through sprites until a transparent pixel is found
    sprites.zeroDrawing = false;
    for (auto i = 0; i < sprites.count; i++) {
      if (sprites.scanlineSprites[i].x == 0) {
        // scanline collided with sprite, load shifters
        // note: fine X scrolling doesn't apply to sprites

        // get bit plane pixels from the shifter
        uint8_t pix_lo = (sprites.shiftPatternLo[i] & 0x80) > 0;
        uint8_t pix_hi = (sprites.shiftPatternHi[i] & 0x80) > 0;
        fg_pix = (pix_hi << 1) | pix_lo;

        // get pallete
        fg_palette = (sprites.scanlineSprites[i].attributes & 0x03) + 0x04;
        fg_priority = (sprites.scanlineSprites[i].attributes & 0x20) == 0;

        if (fg_pix != 0) { // if not transparent
          // draw the pixel
          if (i == 0)
            sprites.zeroDrawing = true;
          break; // earlier sprites are higher priority
        }
      }
    }
  }

  // combine bg and fg pixels
  uint8_t pix = 0, palette = 0;
  if (bg_pix == 0 && fg_pix == 0) {
    // both transparent, draw bg color
    pix = palette = 0;
  } else if (bg_pix == 0 && fg_pix > 0) {
    // bg transparent, fg visible
    pix = fg_pix;
    palette = fg_palette;
  } else if (bg_pix > 0 && fg_pix == 0) {
    // fg transparent, bg visible
    pix = bg_pix;
    palette = bg_palette;
  } else if (bg_pix > 0 && fg_pix > 0) {
    // both visible, zero check needed
    if (fg_priority) {
      pix = fg_pix;
      palette = fg_palette;
    } else {
      pix = bg_pix;
      palette = bg_palette;
    }

    if (sprites.zeroHitPossible && sprites.zeroDrawing) {
      // both fg and bg must be enabled for sprites zero
      if (registers.MASK.showBg & registers.MASK.showSprites) {
        // left edge of screen has special scrolling switches for sprites
        if (~(registers.MASK.showBgLeft | registers.MASK.showSpritesLeft)) {
          if (cycle >= 9 && cycle < 258) {
            registers.STATUS.spriteZeroHit = 1;
          }
        } else if (cycle >= 1 && cycle < 258) {
          registers.STATUS.spriteZeroHit = 1;
        }
      }
    }
  }

  if (scanline < NesRenderer::NES_HEIGHT && scanline >= 0 &&
      cycle - 1 < NesRenderer::NES_WIDTH) {
    getFramebuffer(true).setPixel((cycle - 1), scanline,
                                  getColorFromPalette(palette, pix));
  }

  cycle++;
  if (renderEnabled() && cycle == 260 && scanline < 240)
    rom->mapper->scanline();

  if (cycle >= 341) {
    cycle = 0;

    scanline++;
    if (scanline >= 261) {
      scanline = -1;
      frameComplete = true;
      framecount++;
      odd = !odd;
    }
  }
}