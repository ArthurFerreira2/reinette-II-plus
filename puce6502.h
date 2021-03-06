/*

  Puce6502 - MOS 6502 cpu emulator
  Last modified 1st of August 2020
  Copyright (c) 2018 Arthur Ferreira (arthur.ferreira2@gmail.com)

  This version has been modified for reinette II plus, a french Apple II plus
  emulator using SDL2 (https://github.com/ArthurFerreira2/reinette-II-plus).

  Please download the latest version from
  https://github.com/ArthurFerreira2/puce6502

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#ifndef _PUCE6502_H
#define _PUCE6502_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef enum {false, true} bool;

#define RAMSIZE  0xC000
#define ROMSTART 0xD000
#define ROMSIZE  0x3000
#define LGCSTART 0xD000
#define LGCSIZE  0x3000
#define BK2START 0xD000
#define BK2SIZE  0x1000
#define SL6START 0xC600
#define SL6SIZE  0x0100

uint8_t ram[RAMSIZE];  // 48K of ram in $000-$BFFF
uint8_t rom[ROMSIZE];  // 12K of rom in $D000-$FFFF
uint8_t lgc[LGCSIZE];  // Language Card 12K in $D000-$FFFF
uint8_t bk2[BK2SIZE];  // bank 2 of Language Card 4K in $D000-$DFFF
uint8_t sl6[SL6SIZE];  // P5A disk ][ PROM in slot 6

long long int ticks;

void puce6502Exec(long long int cycleCount);
void puce6502Reset();
void puce6502Break();

#endif
