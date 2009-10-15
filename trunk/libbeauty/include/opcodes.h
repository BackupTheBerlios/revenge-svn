/*
 *  Copyright (C) 2004-2009 The libbeauty Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/* Intel ia32 instruction format: -
 Instruction-Prefixes (Up to four prefixes of 1-byte each. [optional] )
 Opcode (1-, 2-, or 3-byte opcode)
 ModR/M (1 byte [if required] )
 SIB (Scale-Index-Base:1 byte [if required] )
 Displacement (Address displacement of 1, 2, or 4 bytes or none)
 Immediate (Immediate data of 1, 2, or 4 bytes or none)

 Naming convention taked from Intel Instruction set manual, Appendix A. 25366713.pdf
*/

/* enums for store_table */
enum {
	STORE_DIRECT,
	STORE_REG,
};

/* enums for indirect_table */
enum {
	IND_DIRECT,
	IND_MEM,
	IND_STACK,
	IND_IO,
};

#define NOP 0
#define MOV 1
#define ADD 2
#define ADC 3
#define SUB 4
#define SBB 5
#define OR  6
#define XOR 7
#define rAND 8
#define CMP 9
#define MUL 10
#define JMP 11 /* Relative */
#define CALL 12 /* non-relative */ 
#define IF  13
#define ROL 14
#define ROR 15
#define RCL 16
#define RCR 17
#define SHL 18
#define SHR 19
#define SAR 20
#define IN  21
#define OUT 22

#define OVERFLOW 1
#define NOT_OVERFLOW 2
#define BELOW 3
#define NOT_BELOW 4
#define EQUAL 5
#define NOT_EQUAL 6
#define ABOVE 7
#define NOT_ABOVE 8
#define rSIGNED 9
#define NO_SIGNED 10
#define PARITY 11
#define NOT_PARITY 12
#define LESS 13
#define GREATER_EQUAL 14
#define LESS_EQUAL 15
#define GREATER 16


extern char * opcode_table[];
/* FIXME: The values are currently set to 64bit, so can handle 64bit and 32bit, but not 128bit regs. */
#define REG_AX 0x08
#define REG_CX 0x10
#define REG_DX 0x18
#define REG_BX 0x20
#define REG_SP 0x28
#define REG_BP 0x30
#define REG_SI 0x38
#define REG_DI 0x40
#define REG_IP 0x48
#define REG_TMP1 0x50
#define REG_TMP2 0x58

typedef struct reg_s reg_t;

struct reg_s {
  uint32_t offset;
  int32_t size;
} ;

extern reg_t reg_table[];

extern int immed_table[];

extern int shift2_table[];

extern char *store_table[];
extern char *size_table[];
extern char *indirect_table[];

/*
ia32 registers will have their own memory space in the emulator.
All little endian.



Flags= 0x0,4 (offset, length in bytes) (not sure how to handle flags yet.)
eAX = 0x04,4
eCX = 0x08,4
eDX = 0x0c,4
eBX = 0x10,4
eSP = 0x14,4
eBP = 0x18,4
eSI = 0x1c,4
eDI = 0x20,4
eIP = 0x24,4
TMP1 = 0x28,4 (Used to convert an ia32 instruction into multiple RTL instructions.)
TMP2 = 0x2c,4 (Used to convert an ia32 instruction into multiple RTL instructions.)

AX = 0x04,2
CX = 0x08,2
DX = 0x0c,2
BX = 0x10,2
SP = 0x14,2
BP = 0x18,2
SI = 0x1c,2
DI = 0x20,2

AL = 0x04,1
CL = 0x08,1
DL = 0x0c,1
BL = 0x10,1

AH = 0x05,1
CH = 0x09,1
DH = 0x0d,1
BH = 0x11,1

*/

