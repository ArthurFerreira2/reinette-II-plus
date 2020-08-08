/*

  puce6502 - MOS 6502 cpu emulator
  Last modified 1st of August 2020
  Copyright (c) 2018 Arthur Ferreira (arthur.ferreira2@gmail.com)

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

#include "puce6502.h"

// function to be provided by user to handle read and writes to locations not
// in ROM or in RAM : Soft Switches, extension cards ROMs, PIA, VIA, ACIA etc...
extern uint8_t softSwitches(uint16_t address, uint8_t value);


#define CARRY 0x01
#define ZERO  0x02
#define INTR  0x04
#define DECIM 0x08
#define BREAK 0x10
#define UNDEF 0x20
#define OFLOW 0x40
#define SIGN  0x80

struct Operand {
  uint8_t code;
  bool setAcc;
  uint16_t value, address;
} ope;

struct Register {
  uint8_t A,X,Y,SR,SP;
  uint16_t PC;
} reg;


// 	instruction timing :
//  http://nparker.llx.com/a2/opcodes.html
//  http://wouter.bbcmicro.net/general/6502/6502_opcodes.html

// IMPLEMENTED :
// The conditional branch instructions require a second extra cycle if the
// branch happens and crosses a page boundary.

// NOT IMPLEMENTED :
// Absolute-X, absolute-Y, and Zpage-Y addressing modes need an extra cycle
// if indexing crosses a page boundary, or if the instruction writes to memory.

static int cycles[256] = {    // cycle count per instruction
  7,6,0,0,0,3,5,0,3,2,2,0,0,4,6,0,3,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
  6,6,0,0,3,3,5,0,4,2,2,0,4,4,6,0,3,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
  6,6,0,0,0,3,5,0,3,2,2,0,3,4,6,0,3,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
  6,6,0,0,0,3,5,0,4,2,2,0,5,4,6,0,3,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
  0,6,0,0,3,3,3,0,2,0,2,0,4,4,4,0,3,6,0,0,4,4,4,0,2,5,2,0,0,5,0,0,
  2,6,2,0,3,3,3,0,2,2,2,0,4,4,4,0,3,5,0,0,4,4,4,0,2,4,2,0,4,4,4,0,
  2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,3,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
  2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,3,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0
};


//=============================================================== MEMORY AND I/O

inline static uint8_t readMem(uint16_t address){
  if (address <  RAMSIZE)  return(ram[address]);
  if (address >= ROMSTART) return(rom[address - ROMSTART]);
  return softSwitches(address, 0);                          // MEMORY MAPPED I/O
}

inline static void writeMem(uint16_t address, uint8_t value){
  if (address < RAMSIZE) ram[address] = value;
  else if (address < ROMSTART) softSwitches(address, value);
}


//=============================================== STACK, SIGN AND OTHER ROUTINES

inline static void push(uint8_t value){
  writeMem(0x100 + reg.SP--, value);
}

inline static uint8_t pull(){
  return(readMem(0x100 + ++reg.SP));
}

inline static void setSZ(uint8_t value){  // update both the Sign & Zero FLAGS
  if (value & 0x00FF) reg.SR &= ~ZERO;
  else reg.SR |= ZERO;
  if (value & 0x80) reg.SR |= SIGN;
  else reg.SR &= ~SIGN;
}

inline static void branch(){  // used by the 8 branch instructions
  ticks++;
  if (((reg.PC & 0xFF) + ope.address) & 0xFF00) ticks++;
  reg.PC += ope.address;
}

inline static void makeUpdates(uint8_t val){ // used by ASL, LSR, ROL and ROR
  if (ope.setAcc){
    reg.A = val;
    ope.setAcc = false;
  }
  else writeMem(ope.address, val);
  setSZ(val);
}


//============================================================= ADDRESSING MODES

static void IMP(){  // IMPlicit
}

static void ACC(){  // ACCumulator
  ope.value = reg.A;
  ope.setAcc = true;
}

static void IMM(){  // IMMediate
  ope.address = reg.PC++;
  ope.value = readMem(ope.address);
}

static void ZPG(){  // Zero PaGe
  ope.address = readMem(reg.PC++);
  ope.value = readMem(ope.address);
}

static void ZPX(){  // Zero Page,X
  ope.address = (readMem(reg.PC++) + reg.X) & 0xFF;
  ope.value = readMem(ope.address);
}

static void ZPY(){  // Zero Page,Y
  ope.address = (readMem(reg.PC++) + reg.Y) & 0xFF;
  ope.value = readMem(ope.address);
}

static void REL(){  // RELative (for branch instructions)
  ope.address = readMem(reg.PC++);
  if (ope.address & 0x80) ope.address |= 0xFF00;  // branch backward
}

static void ABS(){  // ABSolute
  ope.address = readMem(reg.PC) | (readMem(reg.PC + 1) << 8);
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void ABX(){  // ABsolute,X
  ope.address = (readMem(reg.PC) | (readMem(reg.PC + 1) << 8)) + reg.X;
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void ABY(){  // ABsolute,Y
  ope.address = (readMem(reg.PC) | (readMem(reg.PC + 1) << 8)) + reg.Y;
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void IND(){  // INDirect - JMP ($ABCD) with page-boundary wraparound bug
  uint16_t vector1 = readMem(reg.PC) | (readMem(reg.PC + 1) << 8);
  uint16_t vector2 = (vector1 & 0xFF00) | ((vector1 + 1) & 0x00FF);
  ope.address  = readMem(vector1) | (readMem(vector2) << 8);
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void IDX(){  // InDexed indirect X
  uint16_t vector1 = ((readMem(reg.PC++) + reg.X) & 0xFF);
  ope.address = readMem(vector1 & 0x00FF)|(readMem((vector1+1) & 0x00FF) << 8);
  ope.value = readMem(ope.address);
}

static void IDY(){  // InDirect Indexed Y
  uint16_t vector1 = readMem(reg.PC++);
  uint16_t vector2 = (vector1 & 0xFF00) | ((vector1 + 1) & 0x00FF);
  ope.address = (readMem(vector1) | (readMem(vector2) << 8)) + reg.Y;
  ope.value = readMem(ope.address);
}


//================================================================= INSTRUCTIONS

static void NOP(){  // NO Operation
}

void BRK(){  // BReaK
  push(((++reg.PC) >> 8) & 0xFF);
  push(reg.PC & 0xFF);
  push(reg.SR | BREAK);
  reg.SR |= INTR;
  reg.PC = readMem(0xFFFE) | (readMem(0xFFFF) << 8);
}

static void CLD(){  // CLear Decimal
  reg.SR &= ~DECIM;
}

static void SED(){  // SEt Decimal
  reg.SR |= DECIM;
}

static void CLC(){  // CLear Carry
  reg.SR &= ~CARRY;
}

static void SEC(){  // SEt Carry
  reg.SR |= CARRY;
}

static void CLI(){  // CLear Interrupt
  reg.SR &= ~INTR;
}

static void SEI(){  // SEt Interrupt
  reg.SR |= INTR;
}

static void CLV(){  // CLear oVerflow
  reg.SR &= ~OFLOW;
}

static void LDA(){  // LoaD Accumulator
  reg.A = ope.value;
  setSZ(reg.A);
}

static void LDX(){  // LoaD X
  reg.X = ope.value;
  setSZ(reg.X);
}

static void LDY(){  // LoaD Y
  reg.Y = ope.value;
  setSZ(reg.Y);
}

static void STA(){  // STore Accumulator
  writeMem(ope.address, reg.A);
}

static void STX(){  // STore X
  writeMem(ope.address, reg.X);
}

static void STY(){  // STore Y
  writeMem(ope.address, reg.Y);
}

static void DEC(){  // DECrement
  writeMem(ope.address, --ope.value);
  setSZ(ope.value);
}

static void DEX(){  // DEcrement X
  setSZ(--reg.X);
}

static void DEY(){  // DEcrement Y
  setSZ(--reg.Y);
}

static void INC(){  // INCrement
  writeMem(ope.address, ++ope.value);
  setSZ(ope.value);
}

static void INX(){  // INcrement X
  setSZ(++reg.X);
}

static void INY(){  // INcrement Y
  setSZ(++reg.Y);
}

static void TAX(){  // Transfer Accumulator to X
  reg.X = reg.A;
  setSZ(reg.X);
}

static void TAY(){  // Transfer Accumulator to Y
  reg.Y = reg.A;
  setSZ(reg.Y);
}

static void TXA(){  // Transfer X to Accumulator
  reg.A = reg.X;
  setSZ(reg.A);
}

static void TYA(){  // Transfer Y to Accumulator
  reg.A = reg.Y;
  setSZ(reg.A);
}

static void TSX(){  // Transfer Sp to X
  reg.X = reg.SP;
  setSZ(reg.X);
}

static void TXS(){  // Transfer X to Sp
  reg.SP = reg.X;
}

static void BEQ(){  // Branch on EQual (zero set)
  if (reg.SR & ZERO)     branch();
}

static void BNE(){  // Branch on Not Equal (zero clear)
  if (!(reg.SR & ZERO))  branch();
}

static void BMI(){  // Branch if MInus : when negative, when SIGN is set
  if (reg.SR & SIGN)     branch();
}

static void BPL(){  // Branch if PLus : when positive, when SIGN is clear
  if (!(reg.SR & SIGN))  branch();
}

static void BVS(){  // Branch on oVerflow Set
  if (reg.SR & OFLOW)    branch();
}

static void BVC(){  // Branch on oVerflow Clear
  if (!(reg.SR & OFLOW)) branch();
}

static void BCS(){  // Branch on Carry Set
  if (reg.SR & CARRY)    branch();
}

static void BCC(){  // Branch on Carry Clear
  if (!(reg.SR & CARRY)) branch();
}

static void PHA(){  // PusH A to the stack
  push(reg.A);
}

static void PLA(){  // PulL stack into A
  reg.A = pull();
  setSZ(reg.A);
}

static void PHP(){  // PusH Programm (Status) register to the stack
  push(reg.SR | BREAK);
}

static void PLP(){  // PulL stack into Programm (SR) register
  reg.SR = pull() | UNDEF;
}

static void JMP(){  // JuMP
  reg.PC = ope.address;
}

static void JSR(){  // Jump Sub-Routine
  push((--reg.PC >> 8) & 0xFF);
  push(reg.PC & 0xFF);
  reg.PC = ope.address;
}

static void RTS(){  // ReTurn from Sub-routine
  reg.PC = (pull() | (pull() << 8)) + 1;
}

static void RTI(){  // ReTurn from Interrupt
  reg.SR = pull();
  reg.PC = pull() | (pull() << 8);
}

static void CMP(){  // Compare with A
  setSZ(reg.A - ope.value);
  if (reg.A >= ope.value) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
}

static void CPX(){  // Compare with X
  setSZ(reg.X - ope.value);
  if (reg.X >= ope.value) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
}

static void CPY(){  // Compare with Y
  setSZ(reg.Y - ope.value);
  if (reg.Y >= ope.value) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
}

static void AND(){  // AND with A
  reg.A &= ope.value;
  setSZ(reg.A);
}

static void ORA(){  // OR with A
  reg.A |= ope.value;
  setSZ(reg.A);
}

static void EOR(){  // Exclusive Or with A
  reg.A ^= ope.value;
  setSZ(reg.A);
}

static void BIT(){  // BIT with A - http://www.6502.org/tutorials/vflag.html
  if (reg.A & ope.value) reg.SR &= ~ZERO;
  else reg.SR |= ZERO;
  reg.SR = (reg.SR & 0x3F) | (ope.value & 0xC0);  // update SIGN & OFLOW
}

static void ASL(){  // Arithmetic Shift Left
  uint16_t result = (ope.value << 1);
  if (result & 0xFF00) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)(result & 0xFF));
}

static void LSR(){  // Logical Shift Right
  if (ope.value & 1) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)((ope.value >> 1) & 0xFF));
}

static void ROL(){  // ROtate Left
  uint16_t result = ((ope.value << 1) | (reg.SR & CARRY));
  if (result & 0x100) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)(result & 0xFF));
}

static void ROR(){  // ROtate Right
  uint16_t result = (ope.value >> 1) | ((reg.SR & CARRY) << 7);
  if (ope.value & 0x1) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)(result & 0xFF));
}

static void ADC(){  // ADd with Carry
  uint16_t result = reg.A + ope.value + (reg.SR & CARRY);
  setSZ(result);
  if (((result)^(reg.A))&((result)^(ope.value))&0x0080) reg.SR |= OFLOW;
  else reg.SR &= ~OFLOW;
  if (reg.SR&DECIM) result += ((((result+0x66)^reg.A^ope.value)>>3)&0x22)*3;
  if (result & 0xFF00) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  reg.A = (result & 0xFF);
}

static void SBC(){  // SuBtract with Carry
  ope.value ^= 0xFF;
  if (reg.SR & DECIM) ope.value -= 0x0066;
  uint16_t result = reg.A + ope.value + (reg.SR & CARRY);
  setSZ(result);
  if (((result)^(reg.A))&((result)^(ope.value))&0x0080) reg.SR |= OFLOW;
  else reg.SR &= ~OFLOW;
  if (reg.SR&DECIM) result += ((((result+0x66)^reg.A^ope.value)>>3)&0x22)*3;
  if (result & 0xFF00) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  reg.A = (result & 0xFF);
}

static void UND(){  // UNDefined (not a valid or supported 6502 opcode)
  BRK();
}


//================================================================== JUMP TABLES

static void (*instruction[])(void) = {
 BRK, ORA, UND, UND, UND, ORA, ASL, UND, PHP, ORA, ASL, UND, UND, ORA, ASL, UND,
 BPL, ORA, UND, UND, UND, ORA, ASL, UND, CLC, ORA, UND, UND, UND, ORA, ASL, UND,
 JSR, AND, UND, UND, BIT, AND, ROL, UND, PLP, AND, ROL, UND, BIT, AND, ROL, UND,
 BMI, AND, UND, UND, UND, AND, ROL, UND, SEC, AND, UND, UND, UND, AND, ROL, UND,
 RTI, EOR, UND, UND, UND, EOR, LSR, UND, PHA, EOR, LSR, UND, JMP, EOR, LSR, UND,
 BVC, EOR, UND, UND, UND, EOR, LSR, UND, CLI, EOR, UND, UND, UND, EOR, LSR, UND,
 RTS, ADC, UND, UND, UND, ADC, ROR, UND, PLA, ADC, ROR, UND, JMP, ADC, ROR, UND,
 BVS, ADC, UND, UND, UND, ADC, ROR, UND, SEI, ADC, UND, UND, UND, ADC, ROR, UND,
 UND, STA, UND, UND, STY, STA, STX, UND, DEY, UND, TXA, UND, STY, STA, STX, UND,
 BCC, STA, UND, UND, STY, STA, STX, UND, TYA, STA, TXS, UND, UND, STA, UND, UND,
 LDY, LDA, LDX, UND, LDY, LDA, LDX, UND, TAY, LDA, TAX, UND, LDY, LDA, LDX, UND,
 BCS, LDA, UND, UND, LDY, LDA, LDX, UND, CLV, LDA, TSX, UND, LDY, LDA, LDX, UND,
 CPY, CMP, UND, UND, CPY, CMP, DEC, UND, INY, CMP, DEX, UND, CPY, CMP, DEC, UND,
 BNE, CMP, UND, UND, UND, CMP, DEC, UND, CLD, CMP, UND, UND, UND, CMP, DEC, UND,
 CPX, SBC, UND, UND, CPX, SBC, INC, UND, INX, SBC, NOP, UND, CPX, SBC, INC, UND,
 BEQ, SBC, UND, UND, UND, SBC, INC, UND, SED, SBC, UND, UND, UND, SBC, INC, UND
};

static void (*addressing[])(void) = {
 IMP, IDX, IMP, IMP, IMP, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, IMP, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 ABS, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMP, IDX, IMP, IMP, IMP, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMP, IDX, IMP, IMP, IMP, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, IND, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMP, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMP, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, ZPX, ZPX, ZPY, IMP, IMP, ABY, IMP, IMP, IMP, ABX, IMP, IMP,
 IMM, IDX, IMM, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, ZPX, ZPX, ZPY, IMP, IMP, ABY, IMP, IMP, ABX, ABX, ABY, IMP,
 IMM, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMM, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP
};


//========================================================= USER INTERFACE (API)

void puce6502Reset(){
  reg.PC = readMem(0xFFFC) | (readMem(0xFFFD) << 8);
  reg.SP = 0xFF;
  reg.SR = (reg.SR | INTR) & ~DECIM;
  ope.setAcc = false;
  ticks += 7;
}

void puce6502Exec(long long int cycleCount){
  cycleCount += ticks;             // cycleCount becomes the target ticks value
  while (ticks < cycleCount) {
    ope.code = readMem(reg.PC++);  // FETCH and increment the Program Counter
    addressing[ope.code]();        // DECODE against the addressing mode
    instruction[ope.code]();       // EXECUTE the instruction
    ticks += cycles[ope.code];     // update ticks count
  }
}

void puce6502Break() {
  BRK();
}

void puce6502Goto(uint16_t address) {
  reg.PC = address;
}
