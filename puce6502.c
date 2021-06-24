/*
  puce6502 - MOS 6502 cpu emulator
  Last modified 21st of June 2021
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


// set to zero for 'normal' use
// or to 1 if you want to run the functionnal tests
#define _FUNCTIONNAL_TESTS 0

#include "puce6502.h"

#define CARRY 0x01
#define ZERO  0x02
#define INTR  0x04
#define DECIM 0x08
#define BREAK 0x10
#define UNDEF 0x20
#define OFLOW 0x40
#define SIGN  0x80

#if _FUNCTIONNAL_TESTS

	// for functionnal tests, see main()
	uint8_t RAM[65536];
	inline uint8_t readMem(uint16_t address) { return RAM[address]; }
	inline void writeMem(uint16_t address, uint8_t value) { RAM[address] = value; }

#else

	// user provided functions
	extern uint8_t readMem(uint16_t address);
	extern void writeMem(uint16_t address, uint8_t value);

#endif


unsigned long long int ticks = 0;  // accumulated number of clock cycles


static uint16_t PC;  //  Program Counter
static uint8_t A, X, Y, SP;  // Accumulator, X and y indexes and Stack Pointer
static union {
	uint8_t byte;
  struct {
    uint8_t C : 1;  // Carry
    uint8_t Z : 1;  // Zero
    uint8_t I : 1;  // Interupt-disable
    uint8_t D : 1;  // Decimal
    uint8_t B : 1;  // Break
    uint8_t U : 1;  // Undefined
    uint8_t V : 1;  // Overflow
    uint8_t S : 1;  // Sign
  };
} P;  // Processor Status

void puce6502RST() {  // Reset
	PC = readMem(0xFFFC) | (readMem(0xFFFD) << 8);
	SP = 0xFD;
	P.I = 1;
	P.U = 1;
	ticks += 7;
}


void puce6502IRQ() {  // Interupt Request
	if (!P.I) return;
	P.I = 1;
	PC++;
	writeMem(0x100 + SP, (PC >> 8) & 0xFF);
	SP--;
	writeMem(0x100 + SP, PC & 0xFF);
	SP--;
	writeMem(0x100 + SP, P.byte & ~BREAK);
	SP--;
	PC = readMem(0xFFFE) | (readMem(0xFFFF) << 8);
	ticks += 7;
}


void puce6502NMI() {  // Non Maskable Interupt
	P.I = 1;
	PC++;
	writeMem(0x100 + SP, (PC >> 8) & 0xFF);
	SP--;
	writeMem(0x100 + SP, PC & 0xFF);
	SP--;
	writeMem(0x100 + SP, P.byte & ~BREAK);
	SP--;
	PC = readMem(0xFFFA) | (readMem(0xFFFB) << 8);
	ticks += 7;
}


/*
  Addressing modes abreviations used in the comments down below :

  IMP	: Implied or Implicit : DEX, RTS, CLC - 25 instructions
  ACC	: Accumulator : ASL A, ROR A, DEC A - 4 instructions
  IMM	: Immediate : LDA #$A5 - 11
  ZPG	: Zero Page : LDA $81 - 21 instructions
  ZPX	: Zero Page Indexed with X : LDA $55,X - 16 instructions
  ZPY	: Zero Page Indexed with Y : LDX $55,Y - 2 instructions
  REL	: Relative : BEQ LABEL12 - 8 instructions
  ABS	: Absolute : LDA $2000 - 23 instructions
  ABX	: Absolute Indexed with X : LDA $2000,X - 15 instructions
  ABY	: Absolute Indexed with Y : LDA $2000,Y - 9 instructions
  IND	: Indirect : JMP ($1020) - 1 instruction
	IZX	: ZP Indexed Indirect with X (Preindexed) : LDA ($55,X) - 8 instructions
	IZY	: ZP Indirect Indexed with Y (Postindexed) : LDA ($55),Y - 8 instructions
 */

uint16_t puce6502Exec(unsigned long long int cycleCount) {
	register uint16_t address;
	register uint8_t  value8;
	register uint16_t value16;

	cycleCount += ticks;	// cycleCount becomes the targeted ticks value
	while (ticks < cycleCount) {

		switch (readMem(PC++)) {  // fetch instruction and increment Program Counter

			case 0x00 :  // IMP BRK
				PC++;
				writeMem(0x100 + SP, ((PC) >> 8) & 0xFF);
				SP--;
				writeMem(0x100 + SP, PC & 0xFF);
				SP--;
				writeMem(0x100 + SP, P.byte | BREAK);
				SP--;
				P.I = 1;
				P.D = 0;
				PC = readMem(0xFFFE) | (readMem(0xFFFF) << 8);
				ticks += 7;
			break;

			case 0x01 :  // IZX ORA
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				A |= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 6;
			break;

			case 0x05 :  // ZPG ORA
				A |= readMem(readMem(PC));
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 3;
			break;

			case 0x06 :  // ZPG ASL
				address = readMem(PC);
				PC++;
				value16 = readMem(address) << 1;
				P.C = value16 > 0xFF;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 5;
			break;

			case 0x08 :  // IMP PHP
				writeMem(0x100 + SP, P.byte | BREAK);
				SP--;
				ticks += 3;
			break;

			case 0x09 :  // IMM ORA
				A |= readMem(PC);
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x0A :  // ACC ASL
				value16 = A << 1;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x0D :  // ABS ORA
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				A |= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x0E :  // ABS ASL
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value16 = readMem(address) << 1;
				P.C = value16 > 0xFF;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 6;
			break;

			case 0x10 :  // REL BPL
				address = readMem(PC);
				PC++;
				if (!P.S) {  // jump taken
					ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					PC += address;
				}
				ticks += 2;
			break;

			case 0x11 :  // IZY ORA
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				ticks += (((address & 0xFF) + Y) & 0xFF00) ? 6 : 5;  // page crossing
				address += Y;
				A |= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x15 :  // ZPX ORA
				A |= readMem(readMem(PC) + X);
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x16 :  // ZPX ASL
				address = readMem(PC) + X;
				PC++;
				value16 = readMem(address) << 1;
				writeMem(address, value16 & 0xFF);
				P.C = value16 > 0xFF;
				P.Z = value16 == 0;
				P.S = (value16 & 0xFF) > 0x7F;
				ticks += 6;
			break;

			case 0x18 :  // IMP CLC
				P.C = 0;
				ticks += 2;
			break;

			case 0x19 :  // ABY ORA
				address = readMem(PC);
				PC++;
				ticks += ((address + Y) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				A |= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x1D :  // ABX ORA
				address = readMem(PC);
				PC++;
				ticks += ((address + X) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				A |= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x1E :  // ABX ASL
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value16 = readMem(address) << 1;
				P.C = value16 > 0xFF;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 7;
			break;

			case 0x20 :  // ABS JSR
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				writeMem(0x100 + SP, (PC >> 8) & 0xFF);
				SP--;
				writeMem(0x100 + SP, PC & 0xFF);
				SP--;
				PC = address;
				ticks += 6;
			break;

			case 0x21 :  // IZX AND
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				A &= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 6;
			break;

			case 0x24 :  // ZPG BIT
				address = readMem(PC);
				PC++;
				value8 = readMem(address);
				P.Z = (A & value8) == 0;
				P.byte = (P.byte & 0x3F) | (value8 & 0xC0);
				ticks += 3;
			break;

			case 0x25 :  // ZPG AND
				A &= readMem(readMem(PC));
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 3;
			break;

			case 0x26 :  // ZPG ROL
				address = readMem(PC);
				PC++;
				value16 = (readMem(address) << 1) | P.C;
				P.C = (value16 & 0x100) != 0;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 5;
			break;

			case 0x28 :  // IMP PLP
				SP++;
				P.byte = readMem(0x100 + SP) | UNDEF;
				ticks += 4;
			break;

			case 0x29 :  // IMM AND
				A &= readMem(PC);
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x2A :  // ACC ROL
				value16 = (A << 1) | P.C;
				P.C = (value16 & 0x100) != 0;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x2C :  // ABS BIT
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				P.Z = (A & value8) == 0;
				P.byte = (P.byte & 0x3F) | (value8 & 0xC0);
				ticks += 4;
			break;

			case 0x2D :  // ABS AND
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				A &= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x2E :  // ABS ROL
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value16 = (readMem(address) << 1) | P.C;
				P.C = (value16 & 0x100) != 0;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 6;
			break;

			case 0x30 :  // REL BMI
				address = readMem(PC);
				PC++;
				if (P.S) {  // branch taken
					ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					PC += address;
				}
				ticks += 2;
			break;

			case 0x31 :  // IZY AND
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				ticks += (((address & 0xFF) + Y) & 0xFF00) ? 6 : 5;  // page crossing
				address += Y;
				A &= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x35 :  // ZPX AND
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				A &= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x36 :  // ZPX ROL
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value16 = (readMem(address) << 1) | P.C;
				P.C = value16 > 0xFF;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 6;
			break;

			case 0x38 :  // IMP SEC
				P.C = 1;
				ticks += 2;
			break;

			case 0x39 :  // ABY AND
				address = readMem(PC);
				PC++;
				ticks += ((address + Y) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				A &= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x3D :  // ABX AND
				address = readMem(PC);
				PC++;
				ticks += ((address + X) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				A &= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x3E :  // ABX ROL
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value16 = (readMem(address) << 1) | P.C;
				P.C = value16 > 0xFF;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 7;
			break;

			case 0x40 :  // IMP RTI
				SP++;
				P.byte = readMem(0x100 + SP);
				SP++;
				PC = readMem(0x100 + SP);
				SP++;
				PC |= readMem(0x100 + SP) << 8;
				ticks += 6;
			break;

			case 0x41 :  // IZX EOR
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				A ^= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 6;
			break;

			case 0x45 :  // ZPG EOR
				address = readMem(PC);
				PC++;
				A ^= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 3;
			break;

			case 0x46 :  // ZPG LSR
				address = readMem(PC);
				PC++;
				value8 = readMem(address);
				P.C = (value8 & 1) != 0;
				value8 = value8 >> 1;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 5;
			break;

			case 0x48 :  // IMP PHA
				writeMem(0x100 + SP, A);
				SP--;
				ticks += 3;
			break;

			case 0x49 :  // IMM EOR
				A ^= readMem(PC);
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x4A :  // ACC LSR
				P.C = (A & 1) != 0;
				A = A >> 1;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x4C :  // ABS JMP
				PC = readMem(PC) | (readMem(PC + 1) << 8);
				ticks += 3;
			break;

			case 0x4D :  // ABS EOR
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				A ^= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x4E :  // ABS LSR
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				P.C = (value8 & 1) != 0;
				value8 = value8 >> 1;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 6;
			break;

			case 0x50 :  // REL BVC
				address = readMem(PC);
				PC++;
				if (!P.V) {  // branch taken
					ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					PC += address;
				}
				ticks += 2;
			break;

			case 0x51 :  // IZY EOR
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				ticks += (((address & 0xFF) + Y) & 0xFF00) ? 6 : 5;  // page crossing
				A ^= readMem(address + Y);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x55 :  // ZPX EOR
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				A ^= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x56 :  // ZPX LSR
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				P.C = (value8 & 1) != 0;
				value8 = value8 >> 1;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 6;
			break;

      case 0x58 :  // IMP CLI
        P.I = 0;
        ticks += 2;
      break;

			case 0x59 :  // ABY EOR
				address = readMem(PC);
				PC++;
				ticks += ((address + Y) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				A ^= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x5D :  // ABX EOR
				address = readMem(PC);
				PC++;
				ticks += ((address + X) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				A ^= readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0x5E :  // ABX LSR
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				P.C = (value8 & 1) != 0;
				value8 = value8 >> 1;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 7;
			break;

			case 0x60 :  // IMP RTS
				SP++;
				PC = readMem(0x100 + SP);
				SP++;
				PC |= readMem(0x100 + SP) << 8;
				PC++;
				ticks += 6;
			break;

			case 0x61 :  // IZX ADC
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 6;
			break;

			case 0x65 :  // ZPG ADC
				address = readMem(PC);
				PC++;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 3;
			break;

			case 0x66 :  // ZPG ROR
				address = readMem(PC);
				PC++;
				value8 = readMem(address);
				value16 = (value8 >> 1) | (P.C << 7);
				P.C = (value8 & 0x1) != 0;
				value16 &= 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 5;
			break;

			case 0x68 :  // IMP PLA
				SP++;
				A = readMem(0x100 + SP);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x69 :  // IMM ADC
				value8 = readMem(PC);
				PC++;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x6A :  // ACC ROR
				value16 = (A >> 1) | (P.C << 7);
				P.C = (A & 0x1) != 0;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x6C :  // IND JMP
				address = readMem(PC) | readMem(PC + 1) << 8;
				PC = readMem(address) | (readMem(address + 1) << 8);
				ticks += 5;
			break;

			case 0x6D :  // ABS ADC
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x6E :  // ABS ROR
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				value16 = (value8 >> 1) | (P.C << 7);
				P.C = (value8 & 0x1) != 0;
				value16 = value16 & 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 6;
			break;

			case 0x70 :  // REL BVS
				address = readMem(PC);
				PC++;
				if (P.V) {  // branch taken
					ticks++;
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					PC += address;
				}
				ticks += 2;
			break;

			case 0x71 :  // IZY ADC
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				if ((address + Y) & 0xFF00)  // page crossing
					ticks++;
				value8++;
				address |= readMem(value8) << 8;
				address += Y;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 5;
			break;

			case 0x75 :  // ZPX ADC
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x76 :  // ZPX ROR
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				value16 = (value8 >> 1) | (P.C << 7);
				P.C = (value8 & 0x1) != 0;
				value16 = value16 & 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 6;
			break;

      case 0x78 :  // IMP SEI
        P.I = 1;
        ticks += 2;
      break;

			case 0x79 :  // ABY ADC
				if ((readMem(PC) + Y) & 0xFF00)
					ticks++;
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x7D :  // ABX ADC
				if ((readMem(PC) + X) & 0xFF00)
					ticks++;
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0x7E :  // ABX ROR
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				value16 = (value8 >> 1) | (P.C << 7);
				P.C = (value8 & 0x1) != 0;                          // TBR
				value16 = value16 & 0xFF;
				writeMem(address, value16);
				P.Z = value16 == 0;
				P.S = value16 > 0x7F;
				ticks += 7;
			break;

			case 0x81 :  // IZX STA
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				writeMem(address, A);
				ticks += 6;
			break;

			case 0x84 :  // ZPG STY
				writeMem(readMem(PC), Y);
				PC++;
				ticks += 3;
			break;

			case 0x85 :  // ZPG STA
				writeMem(readMem(PC), A);
				PC++;
				ticks += 3;
			break;

			case 0x86 :  // ZPG STX
				writeMem(readMem(PC), X);
				PC++;
				ticks += 3;
			break;

			case 0x88 :  // IMP DEY
				Y--;
				P.Z = (Y & 0xFF) == 0;
				P.S = (Y & SIGN) != 0;
				ticks += 2;
			break;

			case 0x8A :  // IMP TXA
				A = X;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x8C :  // ABS STY
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				writeMem(address, Y);
				ticks += 4;
			break;

			case 0x8D :  // ABS STA
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				writeMem(address, A);
				ticks += 4;
			break;

			case 0x8E :  // ABS STX
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				writeMem(address, X);
				ticks += 4;
			break;

			case 0x90 :  // REL BCC
				address = readMem(PC);
				PC++;
				if (!P.C) {  // branch taken
					ticks++;
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					PC += address;
				}
				ticks += 2;
			break;

			case 0x91 :  // IZY STA
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				address += Y;
				writeMem(address, A);
				ticks += 6;
			break;

			case 0x94 :  // ZPX STY
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				writeMem(address, Y);
				ticks += 4;
			break;

			case 0x95 :  // ZPX STA
				writeMem((readMem(PC) + X) & 0xFF, A);
				PC++;
				ticks += 4;
			break;

			case 0x96 :  // ZPY STX
				writeMem((readMem(PC) + Y) & 0xFF, X);
				PC++;
				ticks += 4;
			break;

			case 0x98 :  // IMP TYA
				A = Y;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0x99 :  // ABY STA
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				writeMem(address, A);
				ticks += 5;
			break;

			case 0x9A :  // IMP TXS
				SP = X;
				ticks += 2;
			break;

			case 0x9D :  // ABX STA
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				writeMem(address, A);
				ticks += 5;
			break;

			case 0xA0 :  // IMM LDY
				Y = readMem(PC);
				PC++;
				P.Z = Y == 0;
				P.S = Y > 0x7F;
				ticks += 2;
			break;

			case 0xA1 :  // IZX LDA
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				A = readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 6;
			break;

			case 0xA2 :  // IMM LDX
				address = PC;
				PC++;
				X = readMem(address);
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 2;
			break;

			case 0xA4 :  // ZPG LDY
				Y = readMem(readMem(PC));
				PC++;
				P.Z = Y == 0;
				P.S = Y > 0x7F;
				ticks += 3;
			break;

			case 0xA5 :  // ZPG LDA
				A = readMem(readMem(PC));
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 3;
			break;

			case 0xA6 :  // ZPG LDX
				X = readMem(readMem(PC));
				PC++;
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 3;
			break;

			case 0xA8 :  // IMP TAY
				Y = A;
				P.Z = Y == 0;
				P.S = Y > 0x7F;
				ticks += 2;
			break;

			case 0xA9 :  // IMM LDA
				A = readMem(PC);
				PC++;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

			case 0xAA :  // IMP TAX
				X = A;
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 2;
			break;

			case 0xAC :  // ABS LDY
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				Y = readMem(address);
				P.Z = Y == 0;
				P.S = Y > 0x7F;
				ticks += 4;
			break;

			case 0xAD :  // ABS LDA
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				A = readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0xAE :  // ABS LDX
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				X = readMem(address);
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 4;
			break;

			case 0xB0 :  // REL BCS
				address = readMem(PC);
				PC++;
				if (P.C) {  // branch taken
					ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					PC += address;
				}
				ticks += 2;
			break;

			case 0xB1 :  // IZY LDA
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				A = readMem(address + Y);
				ticks += (((address & 0xFF) + Y) & 0xFF00) ? 6 : 5;  // page crossing
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0xB4 :  // ZPX LDY
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				Y = readMem(address);
				P.Z = Y == 0;
				P.S = Y > 0x7F;
				ticks += 4;
			break;

			case 0xB5 :  // ZPX LDA
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				A = readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0xB6 :  // ZPY LDX
				address = (readMem(PC) + Y) & 0xFF;
				PC++;
				X = readMem(address);
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 4;
			break;

      case 0xB8 :  // IMP CLV
        P.V = 0;
        ticks += 2;
      break;

			case 0xB9 :  // ABY LDA
				address = readMem(PC);
				PC++;
				ticks += ((address + Y) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				A = readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0xBA :  // IMP TSX
				X = SP;
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 2;
			break;

			case 0xBC :  // ABX LDY
				address = readMem(PC);
				PC++;
				ticks += ((address + X) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				Y = readMem(address);
				P.Z = Y == 0;
				P.S = Y > 0x7F;
			break;

			case 0xBD :  // ABX LDA
				address = readMem(PC);
				PC++;
				ticks += ((address + X) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				A = readMem(address);
				P.Z = A == 0;
				P.S = A > 0x7F;
			break;

			case 0xBE :  // ABY LDX
				address = readMem(PC);
				PC++;
				ticks += ((address + Y) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				X = readMem(address);
				P.Z = X == 0;
				P.S = X > 0x7F;
			break;

			case 0xC0 :  // IMM CPY
				value8 = readMem(PC);
				PC++;
				P.Z = ((Y - value8) & 0xFF) == 0;
				P.S = ((Y - value8) & SIGN) != 0;
				P.C = (Y >= value8) != 0;
				ticks += 2;
			break;

			case 0xC1 :  // IZX CMP
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				value8 = readMem(address);
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
				ticks += 6;
			break;

			case 0xC4 :  // ZPG CPY
				value8 = readMem(readMem(PC));
				PC++;
				P.Z = ((Y - value8) & 0xFF) == 0;
				P.S = ((Y - value8) & SIGN) != 0;
				P.C = (Y >= value8) != 0;
				ticks += 3;
			break;

			case 0xC5 :  // ZPG CMP
				value8 = readMem(readMem(PC));
				PC++;
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
				ticks += 3;
			break;

			case 0xC6 :  // ZPG DEC
				address = readMem(PC);
				PC++;
				value8 = readMem(address);
				--value8;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 5;
			break;

			case 0xC8 :  // IMP INY
				Y++;
				P.Z = Y  == 0;
				P.S = Y > 0x7F;
				ticks += 2;
			break;

			case 0xC9 :  // IMM CMP
				value8 = readMem(PC);
				PC++;
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
				ticks += 2;
			break;

			case 0xCA :  // IMP DEX
			  X--;
				P.Z = (X & 0xFF) == 0;
				P.S = X > 0x7F;
				ticks += 2;
			break;

			case 0xCC :  // ABS CPY
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				P.Z = ((Y - value8) & 0xFF) == 0;
				P.S = ((Y - value8) & SIGN) != 0;
				P.C = (Y >= value8) != 0;
				ticks += 4;
			break;

			case 0xCD :  // ABS CMP
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
				ticks += 4;
			break;

			case 0xCE :  // ABS DEC
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				value8--;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 3;
			break;

			case 0xD0 :  // REL BNE
				address = readMem(PC);
				PC++;
				if (!P.Z) {  // branch taken
					ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					PC += address;
				}
				ticks += 2;
			break;

			case 0xD1 :  // IZY CMP
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				ticks += ((address + Y) & 0xFF00) ? 6 : 5;  // page crossing
				value8++;
				address |= readMem(value8) << 8;
				address += Y;
				value8 = readMem(address);
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
			break;

			case 0xD5 :  // ZPX CMP
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
				ticks += 4;
			break;

			case 0xD6 :  // ZPX DEC
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				value8--;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 6;
			break;

			case 0xD8 :  // IMP CLD
				P.D = 0;
				ticks += 2;
			break;

			case 0xD9 :  // ABY CMP
				address = readMem(PC);
				PC++;
				ticks += ((address + Y) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				value8 = readMem(address);
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
			break;

			case 0xDD :  // ABX CMP
				address = readMem(PC);
				PC++;
				ticks += ((address + X) & 0xFF00) ? 5 : 4;  // page crossing
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				P.Z = ((A - value8) & 0xFF) == 0;
				P.S = ((A - value8) & SIGN) != 0;
				P.C = (A >= value8) != 0;
			break;

			case 0xDE :  // ABX DEC
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				value8--;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = (value8 & SIGN) != 0;
				ticks += 7;
			break;

			case 0xE0 :  // IMM CPX
				value8 = readMem(PC);
				PC++;
				P.Z = ((X - value8) & 0xFF) == 0;
				P.S = ((X - value8) & SIGN) != 0;
				P.C = (X >= value8) != 0;
				ticks += 2;
			break;

			case 0xE1 :  // IZX SBC
				value8 = readMem(PC) + X;
				PC++;
				address = readMem(value8);
				value8++;
				address |= readMem(value8) << 8;
				value8 = readMem(address);
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 6;
			break;

			case 0xE4 :  // ZPG CPX
				value8 = readMem(readMem(PC));
				PC++;
				P.Z = ((X - value8) & 0xFF) == 0;
				P.S = ((X - value8) & SIGN) != 0;
				P.C = (X >= value8) != 0;
				ticks += 3;
			break;

			case 0xE5 :  // ZPG SBC
				value8 = readMem(readMem(PC));
				PC++;
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 3;
			break;

			case 0xE6 :  // ZPG INC
				address = readMem(PC);
				PC++;
				value8 = readMem(address);
				value8++;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 5;
			break;

			case 0xE8 :  // IMP INX
				X++;
				P.Z = X == 0;
				P.S = X > 0x7F;
				ticks += 2;
			break;

			case 0xE9 :  // IMM SBC
				value8 = readMem(PC);
				PC++;
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + (P.C);
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 2;
			break;

      case 0xEA:  // IMP NOP
        ticks += 2;
      break;

			case 0xEC :  // ABS CPX
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				P.Z = ((X - value8) & 0xFF) == 0;
				P.S = ((X - value8) & SIGN) != 0;
				P.C = (X >= value8) != 0;
				ticks += 4;
			break;

			case 0xED :  // ABS SBC
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0xEE :  // ABS INC
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				value8 = readMem(address);
				value8++;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 6;
			break;

			case 0xF0 :  // REL BEQ
				address = readMem(PC);
				PC++;
				if (P.Z) {  // branch taken
					ticks++;
					if (address & SIGN)
						address |= 0xFF00;  // jump backward
					if (((PC & 0xFF) + address) & 0xFF00)  // page crossing
						ticks++;
					PC += address;
				}
				ticks += 2;
			break;

			case 0xF1 :  // IZY SBC
				value8 = readMem(PC);
				PC++;
				address = readMem(value8);
				if ((address + Y) & 0xFF00)  // page crossing
					ticks++;
				value8++;
				address |= readMem(value8) << 8;
				address += Y;
				value8 = readMem(address);
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 5;
			break;

			case 0xF5 :  // ZPX SBC
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0xF6 :  // ZPX INC
				address = (readMem(PC) + X) & 0xFF;
				PC++;
				value8 = readMem(address);
				value8++;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 6;
			break;

      case 0xF8 :  // IMP SED
        P.D = 1;
        ticks += 2;
      break;

			case 0xF9 :  // ABY SBC
				address = readMem(PC);
				PC++;
				if ((address + Y) & 0xFF00)  // page crossing
					ticks++;
				address |= readMem(PC) << 8;
				PC++;
				address += Y;
				value8 = readMem(address);
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C = value16 > 0xFF;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0xFD :  // ABX SBC
				address = readMem(PC);
				PC++;
				if ((address + X) & 0xFF00)  // page crossing
					ticks++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				value8 ^= 0xFF;
				if (P.D)
					value8 -= 0x0066;
				value16 = A + value8 + P.C;
				P.V = ((value16 ^ A) & (value16 ^ value8) & 0x0080) != 0;
				if (P.D)
					value16 += ((((value16 + 0x66) ^ A ^ value8) >> 3) & 0x22) * 3;
				P.C =  (value16 & 0xFF00) != 0;
				A = value16 & 0xFF;
				P.Z = A == 0;
				P.S = A > 0x7F;
				ticks += 4;
			break;

			case 0xFE :  // ABX INC
				address = readMem(PC);
				PC++;
				address |= readMem(PC) << 8;
				PC++;
				address += X;
				value8 = readMem(address);
				value8++;
				writeMem(address, value8);
				P.Z = value8 == 0;
				P.S = value8 > 0x7F;
				ticks += 7;
			break;

			default:  // invalid / undocumented opcode
				ticks += 2;  // as NOP
			break;
    }  // end of switch
  }
	return PC;
}



// the code below was used during developpment for test and debug
// and is not required for normal operation

#include <stdio.h>

static const char* mn[256] = {
	"BRK","ORA","UND","UND","UND","ORA","ASL","UND","PHP","ORA","ASL","UND","UND","ORA","ASL","UND",
  "BPL","ORA","UND","UND","UND","ORA","ASL","UND","CLC","ORA","UND","UND","UND","ORA","ASL","UND",
  "JSR","AND","UND","UND","BIT","AND","ROL","UND","PLP","AND","ROL","UND","BIT","AND","ROL","UND",
  "BMI","AND","UND","UND","UND","AND","ROL","UND","SEC","AND","UND","UND","UND","AND","ROL","UND",
  "RTI","EOR","UND","UND","UND","EOR","LSR","UND","PHA","EOR","LSR","UND","JMP","EOR","LSR","UND",
  "BVC","EOR","UND","UND","UND","EOR","LSR","UND","CLI","EOR","UND","UND","UND","EOR","LSR","UND",
  "RTS","ADC","UND","UND","UND","ADC","ROR","UND","PLA","ADC","ROR","UND","JMP","ADC","ROR","UND",
  "BVS","ADC","UND","UND","UND","ADC","ROR","UND","SEI","ADC","UND","UND","UND","ADC","ROR","UND",
  "UND","STA","UND","UND","STY","STA","STX","UND","DEY","UND","TXA","UND","STY","STA","STX","UND",
  "BCC","STA","UND","UND","STY","STA","STX","UND","TYA","STA","TXS","UND","UND","STA","UND","UND",
  "LDY","LDA","LDX","UND","LDY","LDA","LDX","UND","TAY","LDA","TAX","UND","LDY","LDA","LDX","UND",
  "BCS","LDA","UND","UND","LDY","LDA","LDX","UND","CLV","LDA","TSX","UND","LDY","LDA","LDX","UND",
  "CPY","CMP","UND","UND","CPY","CMP","DEC","UND","INY","CMP","DEX","UND","CPY","CMP","DEC","UND",
  "BNE","CMP","UND","UND","UND","CMP","DEC","UND","CLD","CMP","UND","UND","UND","CMP","DEC","UND",
  "CPX","SBC","UND","UND","CPX","SBC","INC","UND","INX","SBC","NOP","UND","CPX","SBC","INC","UND",
  "BEQ","SBC","UND","UND","UND","SBC","INC","UND","SED","SBC","UND","UND","UND","SBC","INC","UND"
};

static const int am[256] = {
   0x0 , 0xC , 0x0 , 0x0 , 0x0 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x1 , 0x0 , 0x0 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x0 , 0x4 , 0x4 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x8 , 0x0 ,
   0x7 , 0xC , 0x0 , 0x0 , 0x3 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x1 , 0x0 , 0x7 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x0 , 0x4 , 0x4 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x8 , 0x0 ,
   0x0 , 0xC , 0x0 , 0x0 , 0x0 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x1 , 0x0 , 0x7 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x0 , 0x4 , 0x4 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x8 , 0x0 ,
   0x0 , 0xC , 0x0 , 0x0 , 0x0 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x1 , 0x0 , 0xA , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x0 , 0x4 , 0x4 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x8 , 0x0 ,
   0x0 , 0xC , 0x0 , 0x0 , 0x3 , 0x3 , 0x3 , 0x0 , 0x0 , 0x0 , 0x0 , 0x0 , 0x7 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x4 , 0x4 , 0x5 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x0 , 0x0 ,
   0x2 , 0xC , 0x2 , 0x0 , 0x3 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x0 , 0x0 , 0x7 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x4 , 0x4 , 0x5 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x8 , 0x8 , 0x9 , 0x0 ,
   0x2 , 0xC , 0x0 , 0x0 , 0x3 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x0 , 0x0 , 0x7 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x0 , 0x4 , 0x4 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x8 , 0x0 ,
   0x2 , 0xC , 0x0 , 0x0 , 0x3 , 0x3 , 0x3 , 0x0 , 0x0 , 0x2 , 0x0 , 0x0 , 0x7 , 0x7 , 0x7 , 0x0 ,
   0x6 , 0xD , 0x0 , 0x0 , 0x0 , 0x4 , 0x4 , 0x0 , 0x0 , 0x9 , 0x0 , 0x0 , 0x0 , 0x8 , 0x8 , 0x0
 };

void dasm(uint16_t address) {

  uint8_t op = readMem(address);
  uint8_t b1 = readMem((address + 1) & 0xFFFF);
  uint8_t b2 = readMem((address + 2) & 0xFFFF);

  printf("%04X %02X ", address, op);

  switch(am[op]) {
    case 0x0: printf("       %s          ",              mn[op]      ); break;  // implied
    case 0x1: printf("       %s A        ",              mn[op]      ); break;  // accumulator
    case 0x2: printf("%02X     %s #$%02X     ",    b1,   mn[op],b1   ); break;  // immediate
    case 0x3: printf("%02X     %s $%02X      ",    b1,   mn[op],b1   ); break;  // zero page
    case 0x4: printf("%02X     %s $%02X,X    ",    b1,   mn[op],b1   ); break;  // zero page, X indexed
    case 0x5: printf("%02X     %s $%02X,Y    ",    b1,   mn[op],b1   ); break;  // zero page, Y indexed
    case 0x6: printf("%02X     %s $%02X      ",    b1,   mn[op],b1   ); break;  // relative
    case 0xC: printf("%02X     %s ($%02X,X)  ",    b1,   mn[op],b1   ); break;  // X indexed, indirect
    case 0xD: printf("%02X     %s ($%02X),Y  ",    b1,   mn[op],b1   ); break;  // indirect, Y indexed
    case 0x7: printf("%02X%02X   %s $%02X%02X    ",b1,b2,mn[op],b2,b1); break;  // absolute
    case 0x8: printf("%02X%02X   %s $%02X%02X,X  ",b1,b2,mn[op],b2,b1); break;  // absolute, X indexed
    case 0x9: printf("%02X%02X   %s $%02X%02X,Y  ",b1,b2,mn[op],b2,b1); break;  // absolute, Y indexed
    case 0xA: printf("%02X%02X   %s ($%02X%02X)  ",b1,b2,mn[op],b2,b1); break;  // indirect
  }
}

void printRegs() {
  printf("A=%02X  X=%02X  Y=%02X  S=%02X  *S=%02X  %c%c%c%c%c%c%c%c", \
	A, X, Y, SP, readMem(0x100 + SP), \
	P.S?'N':'-', P.V?'V':'-', P.U?'U':'.', P.B?'B':'-', \
	P.D?'D':'-', P.I?'I':'-', P.Z?'Z':'-', P.C?'C':'-');
}

void setPC(uint16_t address) {
	PC = address;
}

uint16_t getPC(){
	return PC;
}



#if _FUNCTIONNAL_TESTS

		// 6502 functonnal tests
		// using Klaus Dormann's functonnal tests published at :
		// https://github.com/Klaus2m5/6502_65C02_functional_tests

		int main(int argc, char* argv[]){

			char *filename = "6502_functional_test.bin";
			FILE *f = fopen(filename, "rb");
		  if (!f || fread(RAM, 1, 65536, f) != 65536) {
		    printf("ERROR : can't load %s\n", filename);
		    return(0);
		  }
		  fclose(f);

			puce6502RST();  // reset the CPU
			PC = 0x400;  // set Program Counter to start of code

			unsigned long long int oldticks = 0;
			uint16_t oldPC = PC, newPC = PC;  // to detect the BNE $FE when an error occurs

			// while(1) {
			// 	dasm(newPC);
			// 	printf("  ");
			// 	newPC = puce6502Exec(1);
			// 	printRegs();
			// 	printf("   Cycles: %llu   Total: %llu\n", ticks - oldticks, ticks);
			// 	oldticks = ticks;
			//
			//   if (newPC == 0x3469){  // 6502_functional_test SUCCESS
			// 		printf("\nReached end of 6502_functional_test @ %04X : SUCCESS !\n", newPC);
			// 	  break;
			// 	}
			//
			// 	if (newPC == oldPC ) {
			// 		printf("\n\nLoop detected @ %04X - Press ENTER to proceed with next test or CTRL<C> to stop\n\n", newPC);
			// 		return(-1);
			// 		getchar();
			// 		PC = newPC + 2;
			// 	}
			// 	oldPC = newPC;
			// }

		  // Benchmark : replace the above while loop by this one
			  while(puce6502Exec(100) != 0x3469);
			  printf("%llu\n", ticks);
		  // and use the time utility to avaluate the speed the emulated 65C02

			return(0);
		}

#endif


/*   test results :


## Using 6502_functional_test.bin :

<...>
3457 69 55     ADC #$55       A=AA  X=0E  Y=FF  S=FF  *S=34  NVUB----   Cycles: 2   Total: 96240555
3459 C9 AA     CMP #$AA       A=AA  X=0E  Y=FF  S=FF  *S=34  -VUB--ZC   Cycles: 2   Total: 96240557
345B D0 FE     BNE $FE        A=AA  X=0E  Y=FF  S=FF  *S=34  -VUB--ZC   Cycles: 2   Total: 96240559
345D AD 0002   LDA $0200      A=2B  X=0E  Y=FF  S=FF  *S=34  -VUB---C   Cycles: 4   Total: 96240563
3460 C9 2B     CMP #$2B       A=2B  X=0E  Y=FF  S=FF  *S=34  -VUB--ZC   Cycles: 2   Total: 96240565
3462 D0 FE     BNE $FE        A=2B  X=0E  Y=FF  S=FF  *S=34  -VUB--ZC   Cycles: 2   Total: 96240567
3464 A9 F0     LDA #$F0       A=F0  X=0E  Y=FF  S=FF  *S=34  NVUB---C   Cycles: 2   Total: 96240569
3466 8D 0002   STA $0200      A=F0  X=0E  Y=FF  S=FF  *S=34  NVUB---C   Cycles: 4   Total: 96240573

Reached end of 6502_functional_test @ 3469 : SUCCESS !


=> which means puce65c02 passes the 6502 functionnal tests :

                        ; S U C C E S S ************************************************
                        ; -------------
                                success         ;if you get here everything went well
3469 : 4c6934          >        jmp *           ;test passed, no errors

                        ; -------------
                        ; S U C C E S S ************************************************




## Benchmarks

  No info printed during execution
	Using gcc -O3 option

	$ gcc -O3 puce6502.c -o puce6502
	$ time ./puce6502
	96240630

	real    0m0,093s
	user    0m0,000s
	sys     0m0,000s
	$ time ./puce6502
	96240630

	real    0m0,107s
	user    0m0,000s
	sys     0m0,000s
	$ time ./puce6502
	96240630

	real    0m0,096s
	user    0m0,000s
	sys     0m0,000s
	$ time ./puce6502
	96240630

	real    0m0,097s
	user    0m0,000s
	sys     0m0,000s
	$ time ./puce6502
	96240630

	real    0m0,110s
	user    0m0,000s
	sys     0m0,000s
	$ time ./puce6502
	96240630

	real    0m0,110s
	user    0m0,000s
	sys     0m0,000s
	$ time ./puce6502
	96240630

	real    0m0,095s
	user    0m0,000s
	sys     0m0,000s


--> emulated CPU running at around 1 GHz !!!?!???

*/
