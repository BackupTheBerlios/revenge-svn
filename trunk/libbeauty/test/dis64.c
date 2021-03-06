/*
 *  Copyright (C) 2004  The revenge Team
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
 *
 *
 * 11-9-2004 Initial work.
 *   Copyright (C) 2004 James Courtier-Dutton James@superbug.co.uk
 * 10-11-2007 Updates.
 *   Copyright (C) 2007 James Courtier-Dutton James@superbug.co.uk
 * 29-03-2009 Updates.
 *   Copyright (C) 2009 James Courtier-Dutton James@superbug.co.uk
 */

/* Intel ia32 instruction format: -
 Instruction-Prefixes (Up to four prefixes of 1-byte each. [optional] )
 Opcode (1-, 2-, or 3-byte opcode)
 ModR/M (1 byte [if required] )
 SIB (Scale-Index-Base:1 byte [if required] )
 Displacement (Address displacement of 1, 2, or 4 bytes or none)
 Immediate (Immediate data of 1, 2, or 4 bytes or none)

 Naming convention taked from Intel Instruction set manual,
 Appendix A. 25366713.pdf
*/

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dis.h>
#include <bfl.h>
#include <exe.h>
#include <rev.h>
#include <dis-asm.h>
#include <assert.h>

#define EIP_START 0x40000000

struct dis_instructions_s dis_instructions;
uint8_t *inst;
size_t inst_size = 0;
uint8_t *data;
size_t data_size = 0;
struct rev_eng *handle;
struct disassemble_info disasm_info;
disassembler_ftype disassemble_fn;
char *dis_flags_table[] = { " ", "f" };
uint64_t inst_log = 1;	/* Pointer to the current free instruction log entry. */
int local_counter = 1;
struct self_s *self = NULL;


char *condition_table[] = {
	"OVERFLOW_0", /* Signed */
	"NOT_OVERFLOW_1", /* Signed */
	"BELOW_2",	/* Unsigned */
	"NOT_BELOW_3",	/* Unsigned */
	"EQUAL_4",	/* Signed or Unsigned */
	"NOT_EQUAL_5",	/* Signed or Unsigned */
	"BELOW_EQUAL_6",	/* Unsigned */
	"ABOVE_7",	/* Unsigned */
	"SIGNED_8",	/* Signed */
	"NO_SIGNED_9",	/* Signed */
	"PARITY_10",	/* Signed or Unsigned */
	"NOT_PARITY_11",/* Signed or Unsigned */
	"LESS_12",	/* Signed */
	"GREATER_EQUAL_13", /* Signed */
	"LESS_EQUAL_14",    /* Signed */
	"GREATER_15"	/* Signed */
};

struct relocation_s {
	int type; /* 0 = invalid, 1 = external_entry_point, 2 = data */
	uint64_t index; /* Index into the external_entry_point or data */
};

struct mid_start_s {
	uint64_t mid_start;
	uint64_t valid;
};

struct external_entry_point_s {
	int valid;
	int type; /* 1: Internal, 2: External */
	int section_offset;
	int section_id;
	int section_index;
	uint64_t value; /* pointer to original .text entry point */
	uint64_t inst_log; /* Where the function starts in the inst_log */
	uint64_t inst_log_end; /* Where the function ends in inst_log */
	struct process_state_s process_state;
	char *name;
	/* FIXME: Handle variable amount of params */
	int params_size;
	int *params;
	int *params_order;
	int locals_size;
	int *locals;
	int *locals_order;
	/* FIXME: add function return type and param types */
};

struct extension_call_s {
	int params_size;
	int *params;
};

/* Params order:
 * int test30(int64_t param_reg0040, int64_t param_reg0038, int64_t param_reg0018, int64_t param_reg0010, int64_t param_reg0050, int64_t param_reg0058, int64_t param_stack0008, int64_t param_stack0010)
 */


#define RELOCATION_SIZE 100
#define EXTERNAL_ENTRY_POINTS_SIZE 100
struct relocation_s relocations[RELOCATION_SIZE];
struct external_entry_point_s external_entry_points[EXTERNAL_ENTRY_POINTS_SIZE];

/* For the .text segment. I.e. Instructions. */
#define MEMORY_TEXT_SIZE 1000
#define MEMORY_STACK_SIZE 1000
#define MEMORY_REG_SIZE 100
/* For the .data segment. I.e. Static data */
#define MEMORY_DATA_SIZE 1000
#define MEMORY_USED_SIZE 1000
#define INST_LOG_ENTRY_SIZE 1000
#define ENTRY_POINTS_SIZE 1000

/* Used to store details of each instruction.
 * Linked by prev/next pointers
 * so that a single list can store all program flow.
 */
struct inst_log_entry_s inst_log_entry[INST_LOG_ENTRY_SIZE];
int search_back_seen[INST_LOG_ENTRY_SIZE];

/* Used to keep record of where we have been before.
 * Used to identify program flow, branches, and joins.
 */
int memory_used[MEMORY_USED_SIZE];
/* Used to keep a non bfd version of the relocation entries */
int memory_relocation[MEMORY_USED_SIZE];

struct entry_point_s {
	int used;
	/* FIXME: Is this enough, or will full register backup be required */
	uint64_t esp_init_value;
	uint64_t esp_offset_value;
	uint64_t ebp_init_value;
	uint64_t ebp_offset_value;
	uint64_t eip_init_value;
	uint64_t eip_offset_value;
	uint64_t previous_instuction;
} ;

/* This is used to hold return values from process block */
struct entry_point_s entry_point[ENTRY_POINTS_SIZE];
uint64_t entry_point_list_length = ENTRY_POINTS_SIZE;

/* redirect is used for SSA correction, when one needs to rename a variable */
/* renaming the variable within the log entries would take too long. */
/* so use log entry value_id -> redirect -> label_s */
struct label_redirect_s {
	uint64_t redirect;
} ;

struct label_s {
	/* local = 1, param = 2, data = 3, mem = 4 */
	uint64_t scope;
	/* For local or param: reg = 1, stack = 2 */
	/* For data: data = 1, &data = 2, value = 3 */
	uint64_t type;
	/* value */
	uint64_t value;
	/* size in bits */
	uint64_t size_bits;
	/* is it a pointer */
	uint64_t lab_pointer;
	/* is it a signed */
	uint64_t lab_signed;
	/* is it a unsigned */
	uint64_t lab_unsigned;
	/* human readable name */
	char *name;
} ;

int write_inst(FILE *fd, struct instruction_s *instruction, int instruction_number)
{
	int ret = 1; /* Default to failed */
	int tmp;
	int n;
	tmp = fprintf(fd, "// 0x%04x:%s%s",
		instruction_number,
		opcode_table[instruction->opcode],
		dis_flags_table[instruction->flags]);
	switch (instruction->opcode) {
	case MOV:
	case ADD:
	case SUB:
	case MUL:
	case OR:
	case XOR:
	case SHL:
	case SHR:
	case CMP:
	case NOT:
	case SEX:
	/* FIXME: Add DIV */
	//case DIV:
	case JMP:
		if (instruction->srcA.indirect) {
			tmp = fprintf(fd, " %s[%s0x%"PRIx64"]%s,",
				indirect_table[instruction->srcA.indirect],
				store_table[instruction->srcA.store],
				instruction->srcA.index,
				size_table[instruction->srcA.value_size]);
		} else {
			tmp = fprintf(fd, " %s0x%"PRIx64"%s,",
				store_table[instruction->srcA.store],
				instruction->srcA.index,
				size_table[instruction->srcA.value_size]);
		}
		if (instruction->dstA.indirect) {
			tmp = fprintf(fd, " %s[%s0x%"PRIx64"]%s\n",
				indirect_table[instruction->dstA.indirect],
				store_table[instruction->dstA.store],
				instruction->dstA.index,
				size_table[instruction->dstA.value_size]);
		} else {
			tmp = fprintf(fd, " %s0x%"PRIx64"%s\n",
				store_table[instruction->dstA.store],
				instruction->dstA.index,
				size_table[instruction->dstA.value_size]);
		}
		ret = 0;
		break;
	case IF:
		tmp = fprintf(fd, " cond=%"PRIu64"", instruction->srcA.index);
		tmp = fprintf(fd, " JMP-REL=0x%"PRIx64"\n", instruction->dstA.index);
		ret = 0;
		break;
	case CALL:
		if (instruction->srcA.index < 100) {
			tmp = fprintf(fd, " %"PRIu64":%s(",
				instruction->srcA.index,
				external_entry_points[instruction->srcA.index].name);
			tmp = fprintf(fd, ");\n");
		} else {
			tmp = fprintf(fd, " CALL FAILED index=0x%"PRIx64"\n",
				instruction->srcA.index);
		}
		ret = 0;
		break;
	case NOP:
		tmp = fprintf(fd, "\n");
		ret = 0;
		break;
	case RET:
		tmp = fprintf(fd, "\n");
		ret = 0;
		break;
	}
	return ret;
}

int print_inst(struct instruction_s *instruction, int instruction_number)
{
	int ret;

	ret = write_inst(stdout, instruction, instruction_number);
	return ret;
}

int print_dis_instructions(void)
{
	int n;
	struct instruction_s *instruction;
	struct inst_log_entry_s *inst_log1;
	for (n = 1; n <= inst_log; n++) {
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		if (print_inst(instruction, n))
			return 1;
		printf("start_address:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.start_address,
			inst_log1->value2.start_address,
			inst_log1->value3.start_address);
		printf("init:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.init_value,
			inst_log1->value2.init_value,
			inst_log1->value3.init_value);
		printf("offset:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.offset_value,
			inst_log1->value2.offset_value,
			inst_log1->value3.offset_value);
		printf("indirect init:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.indirect_init_value,
			inst_log1->value2.indirect_init_value,
			inst_log1->value3.indirect_init_value);
		printf("indirect offset:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.indirect_offset_value,
			inst_log1->value2.indirect_offset_value,
			inst_log1->value3.indirect_offset_value);
		printf("indirect value_id:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.indirect_value_id,
			inst_log1->value2.indirect_value_id,
			inst_log1->value3.indirect_value_id);
		printf("value_type:0x%x, 0x%x -> 0x%x\n",
			inst_log1->value1.value_type,
			inst_log1->value2.value_type,
			inst_log1->value3.value_type);
		printf("value_scope:0x%x, 0x%x -> 0x%x\n",
			inst_log1->value1.value_scope,
			inst_log1->value2.value_scope,
			inst_log1->value3.value_scope);
		printf("value_id:0x%"PRIx64", 0x%"PRIx64" -> 0x%"PRIx64"\n",
			inst_log1->value1.value_id,
			inst_log1->value2.value_id,
			inst_log1->value3.value_id);
		if (inst_log1->prev_size > 0) {
			int n;
			for (n = 0; n < inst_log1->prev_size; n++) {
				printf("inst_prev:%d:0x%04x\n",
					n,
					inst_log1->prev[n]);
			}
		}
		if (inst_log1->next_size > 0) {
			int n;
			for (n = 0; n < inst_log1->next_size; n++) {
				printf("inst_next:%d:0x%04x\n",
					n,
					inst_log1->next[n]);
			}
		}
	}
	return 0;
}

int get_value_from_index(operand_t *operand, uint64_t *index)
{
	if (operand->indirect) {
		printf(" %s%s[%s0x%"PRIx64"],",
			size_table[operand->value_size],
			indirect_table[operand->indirect],
			store_table[operand->store],
			operand->index);
	} else {
		printf(" %s%s0x%"PRIx64",",
		size_table[operand->value_size],
		store_table[operand->store],
		operand->index);
	}
	return 1;
}

int ram_init(struct memory_s *memory_data)
{
	return 0;
}

int reg_init(struct memory_s *memory_reg)
{
	/* esp */
	memory_reg[0].start_address = REG_SP;
	/* 4 bytes */
	memory_reg[0].length = 8;
	/* 1 - Known */
	memory_reg[0].init_value_type = 1;
	/* Initial value when first accessed */
	memory_reg[0].init_value = 0x10000;
	/* No offset yet */
	memory_reg[0].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_reg[0].value_type = 6;
	memory_reg[0].ref_memory = 0;
	memory_reg[0].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_reg[0].value_scope = 0;
	/* Each time a new value is assigned, this value_id increases */
	memory_reg[0].value_id = 0;
	/* valid: 0 - Entry Not used yet, 1 - Entry Used */
	memory_reg[0].valid = 1;

	/* ebp */
	memory_reg[1].start_address = REG_BP;
	/* 4 bytes */
	memory_reg[1].length = 8;
	/* 1 - Known */
	memory_reg[1].init_value_type = 1;
	/* Initial value when first accessed */
	memory_reg[1].init_value = 0x20000;
	/* No offset yet */
	memory_reg[1].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_reg[1].value_type = 6;
	memory_reg[1].ref_memory = 0;
	memory_reg[1].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_reg[1].value_scope = 0;
	/* Each time a new value is assigned, this value_id increases */
	memory_reg[1].value_id = 0;
	/* valid: 0 - entry Not used yet, 1 - entry Used */
	memory_reg[1].valid = 1;

	/* eip */
	memory_reg[2].start_address = REG_IP;
	/* 4 bytes */
	memory_reg[2].length = 8;
	/* 1 - Known */
	memory_reg[2].init_value_type = 1;
	/* Initial value when first accessed */
	memory_reg[2].init_value = EIP_START;
	/* No offset yet */
	memory_reg[2].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_reg[2].value_type = 5;
	memory_reg[2].ref_memory = 0;
	memory_reg[2].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_reg[2].value_scope = 3;
	/* Each time a new value is assigned, this value_id increases */
	memory_reg[2].value_id = 0;
	/* valid: 0 - entry Not used yet, 1 - entry Used */
	memory_reg[2].valid = 1;
	return 0;
}

int stack_init(struct memory_s *memory_stack)
{
	int n = 0;
	/* eip on the stack */
	memory_stack[n].start_address = 0x10000;
	/* 4 bytes */
	memory_stack[n].length = 8;
	/* 1 - Known */
	memory_stack[n].init_value_type = 1;
	/* Initial value when first accessed */
	memory_stack[n].init_value = 0x0;
	/* No offset yet */
	memory_stack[n].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_stack[n].value_type = 5;
	memory_stack[n].ref_memory = 0;
	memory_stack[n].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_stack[n].value_scope = 0;
	/* Each time a new value is assigned, this value_id increases */
	memory_stack[n].value_id = 0;
	/* valid: 0 - Not used yet, 1 - Used */
	memory_stack[n].valid = 1;
	n++;

#if 0
	/* Param1 */
	memory_stack[n].start_address = 0x10004;
	/* 4 bytes */
	memory_stack[n].length = 4;
	/* 1 - Known */
	memory_stack[n].init_value_type = 1;
	/* Initial value when first accessed */
	memory_stack[n].init_value = 0x321;
	/* No offset yet */
	memory_stack[n].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_stack[n].value_type = 2;
	memory_stack[n].ref_memory = 0;
	memory_stack[n].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_stack[n].value_scope = 0;
	/* Each time a new value is assigned, this value_id increases */
	memory_stack[n].value_id = 0;
	/* valid: 0 - Not used yet, 1 - Used */
	memory_stack[n].valid = 1;
	n++;
#endif
	for (;n < MEMORY_STACK_SIZE; n++) {
		memory_stack[n].valid = 0;
	}
	return 0;
}

int print_mem(struct memory_s *memory, int location) {
	printf("start_address:0x%"PRIx64"\n",
		memory[location].start_address);
	printf("length:0x%x\n",
		memory[location].length);
	printf("init_value_type:0x%x\n",
		memory[location].init_value_type);
	printf("init:0x%"PRIx64"\n",
		memory[location].init_value);
	printf("offset:0x%"PRIx64"\n",
		memory[location].offset_value);
	printf("indirect_init:0x%"PRIx64"\n",
		memory[location].indirect_init_value);
	printf("indirect_offset:0x%"PRIx64"\n",
		memory[location].indirect_offset_value);
	printf("value_type:0x%x\n",
		memory[location].value_type);
	printf("ref_memory:0x%"PRIx32"\n",
		memory[location].ref_memory);
	printf("ref_log:0x%"PRIx32"\n",
		memory[location].ref_log);
	printf("value_scope:0x%x\n",
		memory[location].value_scope);
	printf("value_id:0x%"PRIx64"\n",
		memory[location].value_id);
	printf("valid:0x%"PRIx64"\n",
		memory[location].valid);
	return 0;
}

int process_block(struct process_state_s *process_state, struct rev_eng *handle, uint64_t inst_log_prev, uint64_t list_length, struct entry_point_s *entry, uint64_t eip_offset_limit) {
	uint64_t offset = 0;
	int result;
	int n = 0;
	int err;
	struct inst_log_entry_s *inst_exe_prev;
	struct inst_log_entry_s *inst_exe;
	struct instruction_s *instruction;
	int instruction_offset = 0;
	int octets = 0;
	struct memory_s *memory_text;
	struct memory_s *memory_stack;
	struct memory_s *memory_reg;
	struct memory_s *memory_data;
	int *memory_used;

	memory_text = process_state->memory_text;
	memory_stack = process_state->memory_stack;
	memory_reg = process_state->memory_reg;
	memory_data = process_state->memory_data;
	memory_used = process_state->memory_used;

	printf("process_block entry\n");
	printf("inst_log=%"PRId64"\n", inst_log);
	printf("dis:Data at %p, size=0x%"PRIx64"\n", inst, inst_size);
	for (offset = 0; ;) {
	//for (offset = 0; offset < inst_size;
			//offset += dis_instructions.bytes_used) {
		/* Update EIP */
		offset = memory_reg[2].offset_value;
		if (offset >= eip_offset_limit) {
			printf("Over ran offset=0x%"PRIx64" >= eip_offset_limit=0x%"PRIx64" \n",
				offset, eip_offset_limit);
			return 1;
		}
		dis_instructions.instruction_number = 0;
		dis_instructions.bytes_used = 0;
		printf("eip=0x%"PRIx64", offset=0x%"PRIx64"\n",
			memory_reg[2].offset_value, offset);
		result = disassemble(handle, &dis_instructions, inst, offset);
		printf("bytes used = %d\n", dis_instructions.bytes_used);
		/* Memory not used yet */
		if (0 == memory_used[offset]) {
			printf("Memory not used yet\n");
			for (n = 0; n < dis_instructions.bytes_used; n++) {
				memory_used[offset + n] = -n;
				printf(" 0x%02x", inst[offset + n]);
			}
			printf("\n");
			memory_used[offset] = inst_log;
		} else {
			int inst_this = memory_used[offset];
			/* If value == maxint, then it is the destination of a jump */
			/* But I need to separate the instruction flows */
			/* A jump/branch inst should create a new instruction tree */
			printf("Memory already used\n");
			inst_exe_prev = &inst_log_entry[inst_log_prev];
			inst_exe = &inst_log_entry[inst_this];
			printf("inst_exe_prev=%p, inst_exe=%p\n",
				inst_exe_prev, inst_exe);
			inst_exe->prev_size++;
			if (inst_exe->prev_size == 1) {
				inst_exe->prev = malloc(sizeof(inst_exe->prev));
			} else {
				inst_exe->prev = realloc(inst_exe->prev, sizeof(inst_exe->prev) * inst_exe->prev_size);
			}
			inst_exe->prev[inst_exe->prev_size - 1] = inst_log_prev;
			inst_exe_prev->next_size++;
			if (inst_exe_prev->next_size == 1) {
				inst_exe_prev->next = malloc(sizeof(inst_exe_prev->next));
				inst_exe_prev->next[inst_exe_prev->next_size - 1] = inst_this;
			} else {
				inst_exe_prev->next = realloc(inst_exe_prev->next, sizeof(inst_exe_prev->next) * inst_exe_prev->next_size);
				inst_exe_prev->next[inst_exe_prev->next_size - 1] = inst_this;
			}
			break;
		}	
		//printf("disassemble_fn\n");
		//disassemble_fn = disassembler (handle->bfd);
		//printf("disassemble_fn done\n");
		printf("disassemble: ");
		octets = (*disassemble_fn) (offset, &disasm_info);
		printf("  octets=%d\n", octets);
		if (dis_instructions.bytes_used != octets) {
			printf("Unhandled instruction. Length mismatch. Got %d, expected %d, Exiting\n", dis_instructions.bytes_used, octets);
			return 1;
		}
		/* Update EIP */
		memory_reg[2].offset_value += octets;

		printf("Number of RTL dis_instructions=%d\n",
			dis_instructions.instruction_number);
		if (result == 0) {
			printf("Unhandled instruction. Exiting\n");
			return 1;
		}
		if (dis_instructions.instruction_number == 0) {
			printf("NOP instruction. Get next inst\n");
			continue;
		}
		for (n = 0; n < dis_instructions.instruction_number; n++) {
			instruction = &dis_instructions.instruction[n];
			printf( "Printing inst1111:%d, %d, %"PRId64"\n",instruction_offset, n, inst_log);
			err = print_inst(instruction, instruction_offset + n + 1);
			if (err) {
				printf("print_inst failed\n");
				return err;
			}
			inst_exe_prev = &inst_log_entry[inst_log_prev];
			inst_exe = &inst_log_entry[inst_log];
			memcpy(&(inst_exe->instruction), instruction, sizeof(struct instruction_s));
			err = execute_instruction(self, process_state, inst_exe);
			if (err) {
				printf("execute_intruction failed err=%d\n", err);
				return err;
			}
			inst_exe->prev_size++;
			if (inst_exe->prev_size == 1) {
				inst_exe->prev = malloc(sizeof(inst_exe->prev));
			} else {
				inst_exe->prev = realloc(inst_exe->prev, sizeof(inst_exe->prev) * inst_exe->prev_size);
			}
			inst_exe->prev[inst_exe->prev_size - 1] = inst_log_prev;
			inst_exe_prev->next_size++;
			if (inst_exe_prev->next_size == 1) {
				inst_exe_prev->next = malloc(sizeof(inst_exe_prev->next));
				inst_exe_prev->next[inst_exe_prev->next_size - 1] = inst_log;
			} else {
				inst_exe_prev->next = realloc(inst_exe_prev->next, sizeof(inst_exe_prev->next) * inst_exe_prev->next_size);
				inst_exe_prev->next[inst_exe_prev->next_size - 1] = inst_log;
			}
			inst_exe_prev->next[inst_exe_prev->next_size - 1] = inst_log;

			inst_log_prev = inst_log;
			inst_log++;
			if (0 == memory_reg[2].offset_value) {
				printf("Function exited\n");
				if (inst_exe_prev->instruction.opcode == NOP) {
					inst_exe_prev->instruction.opcode = RET;
				}
				break;
			}
		}
		instruction_offset += dis_instructions.instruction_number;
		if (0 == memory_reg[2].offset_value) {
			printf("Breaking\n");
			break;
		}
		if (IF == instruction->opcode) {
			printf("Breaking at IF\n");
			printf("IF: this EIP = 0x%"PRIx64"\n",
				memory_reg[2].offset_value);
			printf("IF: jump dst abs EIP = 0x%"PRIx64"\n",
				inst_exe->value3.offset_value);
			printf("IF: inst_log = %"PRId64"\n",
				inst_log);
			for (n = 0; n < list_length; n++ ) {
				if (0 == entry[n].used) {
					entry[n].esp_init_value = memory_reg[0].init_value;
					entry[n].esp_offset_value = memory_reg[0].offset_value;
					entry[n].ebp_init_value = memory_reg[1].init_value;
					entry[n].ebp_offset_value = memory_reg[1].offset_value;
					entry[n].eip_init_value = memory_reg[2].init_value;
					entry[n].eip_offset_value = memory_reg[2].offset_value;
					entry[n].previous_instuction = inst_log - 1;
					entry[n].used = 1;
					break;
				}
			}
			/* FIXME: Would starting a "n" be better here? */
			for (n = 0; n < list_length; n++ ) {
				if (0 == entry[n].used) {
					entry[n].esp_init_value = memory_reg[0].init_value;
					entry[n].esp_offset_value = memory_reg[0].offset_value;
					entry[n].ebp_init_value = memory_reg[1].init_value;
					entry[n].ebp_offset_value = memory_reg[1].offset_value;
					entry[n].eip_init_value = inst_exe->value3.init_value;
					entry[n].eip_offset_value = inst_exe->value3.offset_value;
					entry[n].previous_instuction = inst_log - 1;
					entry[n].used = 1;
					break;
				}
			}
			break;
		}
	}
	return 0;
}

int log_to_label(int store, int indirect, uint64_t index, uint64_t relocated, uint64_t value_scope, uint64_t value_id, uint64_t indirect_offset_value, uint64_t indirect_value_id, struct label_s *label) {
	int tmp;
	/* FIXME: May handle by using first switch as switch (indirect) */
	printf("value in log_to_label: 0x%x, 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64"\n",
				store,
				indirect,
				index,
				relocated,
				value_scope,
				value_id,
				indirect_offset_value,
				indirect_value_id);


	switch (store) {
	case STORE_DIRECT:
		/* FIXME: Handle the case of an immediate value being &data */
		/* but it is very difficult to know if the value is a pointer (&data) */
		/* or an offset (data[x]) */
		/* need to use the relocation table to find out */
		/* no relocation table entry == offset */
		/* relocation table entry == pointer */
		/* this info should be gathered at disassembly point */
		/* FIXME: relocation table not present in 16bit x86 mode, so another method will need to be found */
		if (indirect == IND_MEM) {
			label->scope = 3;
			label->type = 1;
			label->value = index;
		} else if (relocated) {
			label->scope = 3;
			label->type = 2;
			label->value = index;
		} else {
			label->scope = 3;
			label->type = 3;
			label->value = index;
		}
		break;
	case STORE_REG:
		switch (value_scope) {
		case 1:
			/* params */
			if (IND_STACK == indirect) {
				label->scope = 2;
				label->type = 2;
				label->value = indirect_offset_value;
				printf("PARAM_STACK^\n"); 
			} else if (0 == indirect) {
				label->scope = 2;
				label->type = 1;
				label->value = index;
				printf("PARAM_REG^\n"); 
			}
			break;
		case 2:
			/* locals */
			if (IND_STACK == indirect) {
				label->scope = 1;
				label->type = 2;
				label->value = value_id;
			} else if (0 == indirect) {
				label->scope = 1;
				label->type = 1;
				label->value = value_id;
			}
			break;
		case 3: /* Data */
			/* FIXME: introduce indirect_value_id and indirect_value_scope */
			/* in order to resolve somewhere */
			/* It will always be a register, and therefore can re-use the */
			/* value_id to identify it. */
			/* It will always be a local and not a param */
			/* FIXME: This should be handled scope = 1, type = 1 above. */
			/* was scope = 4*/
			/* FIXME: get the label->value right */
			label->scope = 1;
			label->type = 1;
			label->value = indirect_value_id;
			break;
		default:
			label->scope = 0;
			label->type = value_scope;
			label->value = 0;
			printf("unknown value scope: %04"PRIx64";\n", (value_scope));
			return 1;
			break;
		}
		break;
	default:
		printf("Unhandled store1\n");
		return 1;
		break;
	}
	return 0;
}

int output_label_redirect(int offset, struct label_s *labels, struct label_redirect_s *label_redirect, FILE *fd) {
	int tmp;
	struct label_s *label;

	tmp = label_redirect[offset].redirect;
	label = &labels[tmp];
	tmp = output_label(label, fd);
}

int output_label(struct label_s *label, FILE *fd) {
	int tmp;

	switch (label->scope) {
	case 3:
		printf("%"PRIx64";\n", label->value);
		/* FIXME: Handle the case of an immediate value being &data */
		/* but it is very difficult to know if the value is a pointer (&data) */
		/* or an offset (data[x]) */
		/* need to use the relocation table to find out */
		/* no relocation table entry == offset */
		/* relocation table entry == pointer */
		/* this info should be gathered at disassembly point */
		switch (label->type) {
		case 1:
			tmp = fprintf(fd, "data%04"PRIx64,
				label->value);
			break;
		case 2:
			tmp = fprintf(fd, "&data%04"PRIx64,
				label->value);
			break;
		case 3:
			tmp = fprintf(fd, "0x%"PRIx64,
				label->value);
			break;
		default:
			printf("output_label error\n");
			return 1;
			break;
		}
		break;
	case 2:
		switch (label->type) {
		case 2:
			printf("param_stack%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "param_stack%04"PRIx64,
				label->value);
			break;
		case 1:
			printf("param_reg%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "param_reg%04"PRIx64,
				label->value);
			break;
		default:
			printf("output_label error\n");
			return 1;
			break;
		}
		break;
	case 1:
		switch (label->type) {
		case 2:
			printf("local_stack%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "local_stack%04"PRIx64,
				label->value);
			break;
		case 1:
			printf("local_reg%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "local_reg%04"PRIx64,
				label->value);
			break;
		default:
			printf("output_label error\n");
			return 1;
			break;
		}
		break;
	case 4:
		/* FIXME: introduce indirect_value_id and indirect_value_scope */
		/* in order to resolve somewhere */
		/* It will always be a register, and therefore can re-use the */
		/* value_id to identify it. */
		/* It will always be a local and not a param */
		/* FIXME: local_reg should be handled in case 1.1 above and
		 *        not be a separate label
		 */
		printf("xxxlocal_reg%04"PRIx64";\n", label->value);
		tmp = fprintf(fd, "xxxlocal_reg%04"PRIx64,
			label->value);
		break;
	default:
		printf("unknown label scope: %04"PRIx64";\n", label->scope);
		tmp = fprintf(fd, "unknown%04"PRIx64,
			label->scope);
		break;
	}
	return 0;
}

int output_variable(int store, int indirect, uint64_t index, uint64_t relocated, uint64_t value_scope, uint64_t value_id, uint64_t indirect_offset_value, uint64_t indirect_value_id, FILE *fd) {
	int tmp;
	/* FIXME: May handle by using first switch as switch (indirect) */
	switch (store) {
	case STORE_DIRECT:
		printf("%"PRIx64";\n", index);
		/* FIXME: Handle the case of an immediate value being &data */
		/* but it is very difficult to know if the value is a pointer (&data) */
		/* or an offset (data[x]) */
		/* need to use the relocation table to find out */
		/* no relocation table entry == offset */
		/* relocation table entry == pointer */
		/* this info should be gathered at disassembly point */
		if (indirect == IND_MEM) {
			tmp = fprintf(fd, "data%04"PRIx64,
				index);
		} else if (relocated) {
			tmp = fprintf(fd, "&data%04"PRIx64,
				index);
		} else {
			tmp = fprintf(fd, "0x%"PRIx64,
				index);
		}
		break;
	case STORE_REG:
		switch (value_scope) {
		case 1:
			/* FIXME: Should this be param or instead param_reg, param_stack */
			if (IND_STACK == indirect) {
				printf("param_stack%04"PRIx64",%04"PRIx64",%04d",
					index, indirect_offset_value, indirect);
				tmp = fprintf(fd, "param_stack%04"PRIx64",%04"PRIx64",%04d",
					index, indirect_offset_value, indirect);
			} else if (0 == indirect) {
				printf("param_reg%04"PRIx64,
					index);
				tmp = fprintf(fd, "param_reg%04"PRIx64,
					index);
			}
			break;
		case 2:
			/* FIXME: Should this be local or instead local_reg, local_stack */
			if (IND_STACK == indirect) {
				printf("local_stack%04"PRIx64,
					value_id);
				tmp = fprintf(fd, "local_stack%04"PRIx64,
					value_id);
			} else if (0 == indirect) {
				printf("local_reg%04"PRIx64,
					value_id);
				tmp = fprintf(fd, "local_reg%04"PRIx64,
					value_id);
			}
			break;
		case 3: /* Data */
			/* FIXME: introduce indirect_value_id and indirect_value_scope */
			/* in order to resolve somewhere */
			/* It will always be a register, and therefore can re-use the */
			/* value_id to identify it. */
			/* It will always be a local and not a param */
			printf("xxxlocal_mem%04"PRIx64";\n", (indirect_value_id));
			tmp = fprintf(fd, "xxxlocal_mem%04"PRIx64,
				indirect_value_id);
			break;
		default:
			printf("unknown value scope: %04"PRIx64";\n", (value_scope));
			tmp = fprintf(fd, "unknown%04"PRIx64,
				value_scope);
			break;
		}
		break;
	default:
		printf("Unhandled store1\n");
		break;
	}
	return 0;
}

int if_expression( int condition, struct inst_log_entry_s *inst_log1_flagged,
	struct label_redirect_s *label_redirect, struct label_s *labels, FILE *fd)
{
	int opcode;
	int err = 0;
	int tmp;
	int store;
	int indirect;
	uint64_t index;
	uint64_t relocated;
	uint64_t value_scope;
	uint64_t value_id;
	uint64_t indirect_offset_value;
	uint64_t indirect_value_id;
	struct label_s *label;
	const char *condition_string;

	opcode = inst_log1_flagged->instruction.opcode;
	printf("\t if opcode=%d, ",inst_log1_flagged->instruction.opcode);

	switch (opcode) {
	case CMP:
		switch (condition) {
		case LESS_EQUAL:
		case BELOW_EQUAL:   /* Unsigned */
			condition_string = " <= ";
			break;
		case GREATER_EQUAL: /* Signed */
//		case ABOVE_EQUAL:   /* Unsigned */
			condition_string = " >= ";
			break;
		case GREATER:
			condition_string = " > ";
			break;
		default:
			printf("if_expression: non-yet-handled: 0x%x\n", condition);
			err = 1;
			break;
		}
		tmp = fprintf(fd, "(");
		if (1 == inst_log1_flagged->instruction.dstA.indirect) {
			tmp = fprintf(fd, "*");
			value_id = inst_log1_flagged->value2.indirect_value_id;
		} else {
			value_id = inst_log1_flagged->value2.value_id;
		}
		tmp = label_redirect[value_id].redirect;
		label = &labels[tmp];
		//tmp = fprintf(fd, "0x%x:", tmp);
		tmp = output_label(label, fd);
		tmp = fprintf(fd, "%s", condition_string);
		if (1 == inst_log1_flagged->instruction.srcA.indirect) {
			tmp = fprintf(fd, "*");
			value_id = inst_log1_flagged->value1.indirect_value_id;
		} else {
			value_id = inst_log1_flagged->value1.value_id;
		}
		tmp = label_redirect[value_id].redirect;
		label = &labels[tmp];
		//tmp = fprintf(fd, "0x%x:", tmp);
		tmp = output_label(label, fd);
		tmp = fprintf(fd, ") ");
		break;
	default:
		printf("if_expression: CMP not present: opcode = 0x%x:0x%x, cond = 0x%x\n", CMP, opcode, condition);
		err = 1;
		break;
	}
	return err;
}

/* If relocated_data returns 1, it means that there was a
 * relocation table entry for this data location.
 * This most likely means that this is a pointer.
 * FIXME: What to do if the relocation is to the code segment? Pointer to function?
 */
uint32_t relocated_data(struct rev_eng *handle, uint64_t offset, uint64_t size)
{
	int n;
	for (n = 0; n < handle->reloc_table_data_sz; n++) {
		if (handle->reloc_table_data[n].address == offset) {
			return 1;
		}
	}
	return 0;
}


uint32_t output_function_name(FILE *fd,
		struct external_entry_point_s *external_entry_point)
{
	int commas = 0;
	int tmp, n;

	printf("int %s()\n{\n", external_entry_point->name);
	printf("value = %"PRIx64"\n", external_entry_point->value);
	tmp = fprintf(fd, "int %s(", external_entry_point->name);
	return 0;
}

int output_function_body(struct process_state_s *process_state,
			 FILE *fd, int start, int end, struct label_redirect_s *label_redirect, struct label_s *labels)
{
	int tmp, n,l;
	int err;
	int found;
	uint64_t value_id;
	struct instruction_s *instruction;
	struct instruction_s *instruction_prev;
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log1_prev;
	struct inst_log_entry_s *inst_log1_flags;
	struct memory_s *value;
	struct label_s *label;
	struct extension_call_s *call;

	if (!start || !end) {
		printf("output_function_body:Invalid start or end\n");
		return 1;
	}
	printf("output_function_body:start=0x%x, end=0x%x\n", start, end);

	for (n = start; n <= end; n++) {
		inst_log1 =  &inst_log_entry[n];
		if (!inst_log1) {
			printf("output_function_body:Invalid inst_log1[0x%x]\n", n);
			return 1;
		}
		inst_log1_prev =  &inst_log_entry[inst_log1->prev[0]];
		if (!inst_log1_prev) {
			printf("output_function_body:Invalid inst_log1_prev[0x%x]\n", n);
			return 1;
		}
		instruction =  &inst_log1->instruction;
		instruction_prev =  &inst_log1_prev->instruction;

		write_inst(fd, instruction, n);
		/* Output labels when this is a join point */
		/* or when the previous instruction was some sort of jump */
		if ((inst_log1->prev_size) > 1) {
			printf("label%04"PRIx32":\n", n);
			tmp = fprintf(fd, "label%04"PRIx32":\n", n);
		} else {
			if ((inst_log1->prev[0] != (n - 1)) &&
				(inst_log1->prev[0] != 0)) {		
				printf("label%04"PRIx32":\n", n);
				tmp = fprintf(fd, "label%04"PRIx32":\n", n);
			}
		}
			
		/* Test to see if we have an instruction to output */
		printf("Inst 0x%04x: %d: value_type = %d, %d, %d\n", n,
			instruction->opcode,
			inst_log1->value1.value_type,
			inst_log1->value2.value_type,
			inst_log1->value3.value_type);
		if ((0 == inst_log1->value3.value_type) ||
			(1 == inst_log1->value3.value_type) ||
			(2 == inst_log1->value3.value_type) ||
			(3 == inst_log1->value3.value_type) ||
			(5 == inst_log1->value3.value_type)) {
			switch (instruction->opcode) {
			case MOV:
			case SEX:
				if (inst_log1->value1.value_type == 6) {
					printf("ERROR1\n");
					break;
				}
				if (inst_log1->value1.value_type == 5) {
					printf("ERROR2\n");
					break;
				}
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				/* FIXME: Check limits */
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " = ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");

				break;
			case ADD:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " += ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case MUL:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " *= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SUB:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " -= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case OR:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " |= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case XOR:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " ^= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case NOT:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " = !");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SHL:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " <<= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SHR:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " >>= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case JMP:
				printf("JMP reached XXXX\n");
				if (print_inst(instruction, n))
					return 1;
				tmp = fprintf(fd, "\t");
				if (instruction->srcA.relocated) {
					printf("goto rel%08"PRIx64";\n", instruction->srcA.index);
					tmp = fprintf(fd, "goto rel%08"PRIx64";\n",
						instruction->srcA.index);
				} else {
					printf("goto label%04"PRIx32";\n",
						inst_log1->next[0]);
					tmp = fprintf(fd, "goto label%04"PRIx32";\n",
						inst_log1->next[0]);
				}
				break;
			case CALL:
				/* FIXME: This does nothing at the moment. */
				if (print_inst(instruction, n))
					return 1;
				/* Search for EAX */
				printf("call index = 0x%"PRIx64", params size = 0x%x\n", instruction->srcA.index,
					external_entry_points[instruction->srcA.index].params_size);
				printf("\t");
				tmp = fprintf(fd, "\t");
				tmp = label_redirect[inst_log1->value3.value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				printf(" = ");
				tmp = fprintf(fd, " = ");

				tmp = fprintf(fd, "%s(", 
					external_entry_points[instruction->srcA.index].name);
				call = inst_log1->extension;
				for (l = 0; l < call->params_size; l++) {
					if (l > 0) {
						fprintf(fd, ", ");
					}
					label = &labels[call->params[l]];
					tmp = output_label(label, fd);
				}
				tmp = fprintf(fd, ");\n");
				printf("%s();\n",
					external_entry_points[instruction->srcA.index].name);
//				tmp = fprintf(fd, "/* call(); */\n");
//				printf("/* call(); */\n");
				break;

			case CMP:
				/* Don't do anything for this instruction. */
				/* only does anything if combined with a branch instruction */
				if (print_inst(instruction, n))
					return 1;
				tmp = fprintf(fd, "\t");
				tmp = fprintf(fd, "/* cmp; */\n");
				printf("/* cmp; */\n");
				break;

			case IF:
				/* FIXME: Never gets here, why? */
				/* Don't do anything for this instruction. */
				/* only does anything if combined with a branch instruction */
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				printf("if ");
				tmp = fprintf(fd, "if ");
				found = 0;
				tmp = 5; /* Limit the scan backwards */
				l = inst_log1->prev[0];
				do {
					inst_log1_flags =  &inst_log_entry[l];
					printf("Previous flags 0x%x\n", inst_log1_flags->instruction.flags);
					if (1 == inst_log1_flags->instruction.flags) {
						found = 1;
					}
					printf("Previous flags instruction size 0x%x\n", inst_log1_flags->prev_size);
					if (inst_log1_flags->prev > 0) {
						l = inst_log1_flags->prev[0];
					} else {
						l = 0;
					}
					tmp--;
				} while ((0 == found) && (0 < tmp) && (0 != l));
				if (found == 0) {
					printf("Previous flags instruction not found. found=%d, tmp=%d, l=%d\n", found, tmp, l);
					return 1;
				} else {
					printf("Previous flags instruction found. found=%d, tmp=%d, l=%d\n", found, tmp, l);
				}
					
				err = if_expression( instruction->srcA.index, inst_log1_flags, label_redirect, labels, fd);
				printf("\t prev flags=%d, ",inst_log1_flags->instruction.flags);
				printf("\t prev opcode=%d, ",inst_log1_flags->instruction.opcode);
				printf("\t 0x%"PRIx64":%s", instruction->srcA.index, condition_table[instruction->srcA.index]);
				printf("\t LHS=%d, ",inst_log1->prev[0]);
				printf("goto label%04"PRIx32";\n", inst_log1->next[1]);
				if (err) {
					printf("IF CONDITION unknown\n");	
					return 1;
				}
				tmp = fprintf(fd, "goto label%04"PRIx32";\n", inst_log1->next[1]);
				break;

			case NOP:
				if (print_inst(instruction, n))
					return 1;
				break;
			case RET:
				if (print_inst(instruction, n))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				printf("return\n");
				tmp = fprintf(fd, "return ");
				tmp = label_redirect[inst_log1->value1.value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			default:
				printf("Unhandled output instruction1\n");
				if (print_inst(instruction, n))
					return 1;
				return 1;
				break;
			}
		}
	}
	if (0 < inst_log1->next_size && inst_log1->next[0]) {		
		printf("\tgoto label%04"PRIx32";\n", inst_log1->next[0]);
		tmp = fprintf(fd, "\tgoto label%04"PRIx32";\n", inst_log1->next[0]);
	}
	tmp = fprintf(fd, "}\n\n");
	return 0;
}

int register_label(struct external_entry_point_s *entry_point, uint64_t value_id,
	struct memory_s *value, struct label_redirect_s *label_redirect, struct label_s *labels)
{
	int n;
	int found;
	struct label_s *label;
	int label_offset;
	label_offset = label_redirect[value_id].redirect;
	label = &labels[label_offset];
	label->size_bits = value->length * 8;
	printf("Registering label: 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64"\n",
		label->scope,
		label->type,
		label->value,
		label->size_bits,
		label->lab_pointer,
		label->lab_signed,
		label->lab_unsigned);
	//int params_size;
	//int *params;
	//int *params_order;
	//int locals_size;
	//int *locals;
	//int *locals_order;
	found = 0;
	switch (label->scope) {
	case 2:
		printf("PARAM\n");
		for(n = 0; n < entry_point->params_size; n++) {
			printf("looping 0x%x\n", n);
			if (entry_point->params[n] == label_offset) {
				printf("Duplicate\n");
				found = 1;
				break;
			}
		}
		if (found) {
			break;
		}
		(entry_point->params_size)++;
		entry_point->params = realloc(entry_point->params, entry_point->params_size * sizeof(int));
		entry_point->params[entry_point->params_size - 1] = label_offset;
		break;
	case 1:
		printf("LOCAL\n");
		for(n = 0; n < entry_point->locals_size; n++) {
			printf("looping 0x%x\n", n);
			if (entry_point->locals[n] == label_offset) {
				printf("Duplicate\n");
				found = 1;
				break;
			}
		}
		if (found) {
			break;
		}
		(entry_point->locals_size)++;
		entry_point->locals = realloc(entry_point->locals, entry_point->locals_size * sizeof(int));
		entry_point->locals[entry_point->locals_size - 1] = label_offset;
	case 3:
		printf("HEX VALUE\n");
		break;
	default:
		printf("VALUE unhandled 0x%"PRIx64"\n", label->scope);
	}
	printf("params_size = 0x%x, locals_size = 0x%x\n",
		entry_point->params_size,
		entry_point->locals_size);

	printf("value: 0x%"PRIx64", 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%x, 0x%x, 0x%"PRIx64"\n",
		value->start_address,
		value->length,
		value->init_value,
		value->offset_value,
		value->value_type,
		value->value_scope,
		value->value_id);
	//tmp = register_label(label, &(inst_log1->value3));
	return 0;
}

int scan_for_labels_in_function_body(struct external_entry_point_s *entry_point,
			 int start, int end, struct label_redirect_s *label_redirect, struct label_s *labels)
{
	int tmp, n;
	int err;
	uint64_t value_id;
	struct instruction_s *instruction;
	struct inst_log_entry_s *inst_log1;
	struct memory_s *value;
	struct label_s *label;

	if (!start || !end) {
		printf("scan_for_labels_in_function:Invalid start or end\n");
		return 1;
	}
	printf("scan_for_labels:start=0x%x, end=0x%x\n", start, end);

	for (n = start; n <= end; n++) {
		inst_log1 =  &inst_log_entry[n];
		if (!inst_log1) {
			printf("scan_for_labels:Invalid inst_log1[0x%x]\n", n);
			return 1;
		}

		instruction =  &inst_log1->instruction;

		/* Test to see if we have an instruction to output */
		printf("Inst 0x%04x: %d: value_type = %d, %d, %d\n", n,
			instruction->opcode,
			inst_log1->value1.value_type,
			inst_log1->value2.value_type,
			inst_log1->value3.value_type);
		if ((0 == inst_log1->value3.value_type) ||
			(1 == inst_log1->value3.value_type) ||
			(2 == inst_log1->value3.value_type) ||
			(3 == inst_log1->value3.value_type) ||
			(5 == inst_log1->value3.value_type)) {
			switch (instruction->opcode) {
			case MOV:
			case SEX:
				if (inst_log1->value1.value_type == 6) {
					printf("ERROR1\n");
					break;
				}
				if (inst_log1->value1.value_type == 5) {
					printf("ERROR2\n");
					break;
				}
				if (1 == instruction->dstA.indirect) {
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value3), label_redirect, labels);
				if (1 == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);

				break;
			case ADD:
			case MUL:
			case SUB:
			case rAND:
			case OR:
			case XOR:
			case NOT:
			case SHL:
			case SHR:
				if (1 == instruction->dstA.indirect) {
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value3), label_redirect, labels);
				if (1 == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				break;
			case JMP:
				break;
			case CALL:
				if (1 == instruction->dstA.indirect) {
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value3), label_redirect, labels);
				break;

			case CMP:
				if (1 == instruction->dstA.indirect) {
					value_id = inst_log1->value2.indirect_value_id;
				} else {
					value_id = inst_log1->value2.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value2), label_redirect, labels);
				if (1 == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				break;

			case IF:
				printf("IF: This might give signed or unsigned info to labels\n");
				break;

			case NOP:
				break;
			case RET:
				if (1 == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				break;
			default:
				printf("Unhandled scan instruction1\n");
				if (print_inst(instruction, n))
					return 1;
				return 1;
				break;
			}
		}
	}
	return 0;
}
/***********************************************************************************
 * This is a complex routine. It utilises dynamic lists in order to reduce 
 * memory usage.
 **********************************************************************************/
int search_back_local_reg_stack(uint64_t mid_start_size, struct mid_start_s *mid_start, int reg_stack, uint64_t indirect_init_value, uint64_t indirect_offset_value, uint64_t *size, uint64_t **inst_list)
{
	struct instruction_s *instruction;
	struct inst_log_entry_s *inst_log1;
	uint64_t value_id;
	uint64_t inst_num;
	uint64_t tmp;
	int found = 0;
	int n;

	*size = 0;
	/* FIXME: This could be optimized out if the "seen" value just increased on each call */
	for (n = 0; n < INST_LOG_ENTRY_SIZE; n++) {
		search_back_seen[n] = 0;
	}

	printf("search_back_local_stack: 0x%"PRIx64", 0x%"PRIx64"\n", indirect_init_value, indirect_offset_value);
	if (0 < mid_start_size) {
		printf("search_back:prev_size=0x%"PRIx64"\n", mid_start_size);
	}
	if (0 == mid_start_size) {
		printf("search_back ended\n");
		return 1;
	}

	do {
		found = 0;
		for(n = 0; n < mid_start_size; n++) {
			if (1 == mid_start[n].valid) {
				inst_num = mid_start[n].mid_start;
				mid_start[n].valid = 0;
				found = 1;
				printf("mid_start removed 0x%"PRIx64" at 0x%x, size=0x%"PRIx64"\n", mid_start[n].mid_start, n, mid_start_size);
				break;
			}
		}
		if (!found) {
			printf("mid_start not found, exiting\n");
			goto search_back_exit_free;
		}
		if (search_back_seen[inst_num]) {
			continue;
		}
		search_back_seen[inst_num] = 1;
		inst_log1 =  &inst_log_entry[inst_num];
		instruction =  &inst_log1->instruction;
		value_id = inst_log1->value3.value_id;
		printf("inst_num:0x%"PRIx64"\n", inst_num);
		/* STACK */
		if ((reg_stack == 2) &&
			(instruction->dstA.store == STORE_REG) &&
			(inst_log1->value3.value_scope == 2) &&
			(instruction->dstA.indirect == IND_STACK) &&
			(inst_log1->value3.indirect_init_value == indirect_init_value) &&
			(inst_log1->value3.indirect_offset_value == indirect_offset_value)) {
			tmp = *size;
			tmp++;
			*size = tmp;
			if (tmp == 1) {
				*inst_list = malloc(sizeof(*inst_list));
				(*inst_list)[0] = inst_num;
			} else {
				*inst_list = realloc(*inst_list, tmp * sizeof(*inst_list));
				(*inst_list)[tmp - 1] = inst_num;
			}
		} else if ((reg_stack == 1) &&
			(instruction->dstA.store == STORE_REG) &&
			(instruction->dstA.indirect == IND_DIRECT) &&
			(instruction->dstA.index == indirect_init_value)) {
			tmp = *size;
			tmp++;
			*size = tmp;
			if (tmp == 1) {
				*inst_list = malloc(sizeof(*inst_list));
				(*inst_list)[0] = inst_num;
			} else {
				*inst_list = realloc(*inst_list, tmp * sizeof(*inst_list));
				(*inst_list)[tmp - 1] = inst_num;
			}
		} else {
			if ((inst_log1->prev_size > 0) &&
				(inst_log1->prev[0] != 0)) {
				int prev_index;
				found = 0;
				prev_index = 0;
				for(n = 0; n < mid_start_size; n++) {
					if (0 == mid_start[n].valid) {
						mid_start[n].mid_start = inst_log1->prev[prev_index];
						prev_index++;
						mid_start[n].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[n].mid_start, n);
						found = 1;
					}
					if (prev_index >= inst_log1->prev_size) {
						break;
					}
				}
				if (prev_index < inst_log1->prev_size) {
					uint64_t mid_next;
					mid_next = mid_start_size + inst_log1->prev_size - prev_index;
					mid_start = realloc(mid_start, mid_next * sizeof(struct mid_start_s));
					for(n = mid_start_size; n < mid_next; n++) {
						mid_start[n].mid_start = inst_log1->prev[prev_index];
						prev_index++;
						printf("mid_start realloc added 0x%"PRIx64" at 0x%x\n", mid_start[n].mid_start, n);
						mid_start[n].valid = 1;
					}
					mid_start_size = mid_next;
				}
				
				if (!found) {
					printf("not found\n");
					goto search_back_exit_free;
				}
			}
		}
	/* FIXME: There must be deterministic exit point */
	} while (1);
	printf("end of loop, exiting\n");

search_back_exit_free:
	free(mid_start);
	return 0;
	
}

int main(int argc, char *argv[])
{
	int n = 0;
//	uint64_t offset = 0;
//	int instruction_offset = 0;
//	int octets = 0;
//	int result;
	char *filename;
	uint32_t arch;
	uint64_t mach;
	FILE *fd;
	int tmp;
	int err;
	const char *file = "test.obj";
	size_t inst_size = 0;
//	uint64_t reloc_size = 0;
	int l, m;
	struct instruction_s *instruction;
//	struct instruction_s *instruction_prev;
	struct inst_log_entry_s *inst_log1;
//	struct inst_log_entry_s *inst_log1_prev;
	struct inst_log_entry_s *inst_exe;
//	struct memory_s *value;
	uint64_t inst_log_prev = 0;
	int param_present[100];
	int param_size[100];
	char *expression;
	int not_finished;
	struct memory_s *memory_text;
	struct memory_s *memory_stack;
	struct memory_s *memory_reg;
	struct memory_s *memory_data;
	int *memory_used;
	struct label_redirect_s *label_redirect;
	struct label_s *labels;

	expression = malloc(1000); /* Buffer for if expressions */

	handle = bf_test_open_file(file);
	if (!handle) {
		printf("Failed to find or recognise file\n");
		return 1;
	}
	tmp = bf_get_arch_mach(handle, &arch, &mach);
	if ((arch != 9) ||
		(mach != 64)) {
		printf("File not the correct arch and mach\n");
		return 1;
	}

	printf("symtab_size = %ld\n", handle->symtab_sz);
	for (l = 0; l < handle->symtab_sz; l++) {
		printf("%d\n", l);
		printf("type:0x%02x\n", handle->symtab[l]->flags);
		printf("name:%s\n", handle->symtab[l]->name);
		printf("value=0x%02"PRIx64"\n", handle->symtab[l]->value);
		printf("section=%p\n", handle->symtab[l]->section);
		printf("section name=%s\n", handle->symtab[l]->section->name);
		printf("section flags=0x%02x\n", handle->symtab[l]->section->flags);
		printf("section index=0x%02"PRIx32"\n", handle->symtab[l]->section->index);
		printf("section id=0x%02"PRIx32"\n", handle->symtab[l]->section->id);
	}

	printf("sectiontab_size = %ld\n", handle->section_sz);
	for (l = 0; l < handle->section_sz; l++) {
		printf("%d\n", l);
		printf("flags:0x%02x\n", handle->section[l]->flags);
		printf("name:%s\n", handle->section[l]->name);
		printf("index=0x%02"PRIx32"\n", handle->section[l]->index);
		printf("id=0x%02"PRIx32"\n", handle->section[l]->id);
		printf("sectio=%p\n", handle->section[l]);
	}


	printf("Setup ok\n");
	inst_size = bf_get_code_size(handle);
	inst = malloc(inst_size);
	bf_copy_code_section(handle, inst, inst_size);
	printf("dis:.text Data at %p, size=0x%"PRIx64"\n", inst, inst_size);
	for (n = 0; n < inst_size; n++) {
		printf(" 0x%02x", inst[n]);
	}
	printf("\n");

	data_size = bf_get_data_size(handle);
	data = malloc(data_size);
	self = malloc(sizeof *self);
	printf("sizeof struct self_s = 0x%"PRIx64"\n", sizeof *self);
	self->data_size = data_size;
	self->data = data;
	bf_copy_data_section(handle, data, data_size);
	printf("dis:.data Data at %p, size=0x%"PRIx64"\n", data, data_size);
	for (n = 0; n < data_size; n++) {
		printf(" 0x%02x", data[n]);
	}
	printf("\n");

	bf_get_reloc_table_code_section(handle);
	printf("reloc_table_code_sz=0x%"PRIx64"\n", handle->reloc_table_code_sz);
	for (n = 0; n < handle->reloc_table_code_sz; n++) {
		printf("reloc_table_code:addr = 0x%"PRIx64", size = 0x%"PRIx64", value = 0x%"PRIx64", section_index = 0x%"PRIx64", section_name=%s, symbol_name=%s\n",
			handle->reloc_table_code[n].address,
			handle->reloc_table_code[n].size,
			handle->reloc_table_code[n].value,
			handle->reloc_table_code[n].section_index,
			handle->reloc_table_code[n].section_name,
			handle->reloc_table_code[n].symbol_name);
	}

	bf_get_reloc_table_data_section(handle);
	for (n = 0; n < handle->reloc_table_data_sz; n++) {
		printf("reloc_table_data:addr = 0x%"PRIx64", size = 0x%"PRIx64", section = 0x%"PRIx64"\n",
			handle->reloc_table_data[n].address,
			handle->reloc_table_data[n].size,
			handle->reloc_table_data[n].section_index);
	}
	
	printf("handle=%p\n", handle);
	
	printf("handle=%p\n", handle);
	init_disassemble_info(&disasm_info, stdout, (fprintf_ftype) fprintf);
	disasm_info.flavour = bfd_get_flavour(handle->bfd);
	disasm_info.arch = bfd_get_arch(handle->bfd);
	disasm_info.mach = bfd_get_mach(handle->bfd);
	disasm_info.disassembler_options = NULL;
	disasm_info.octets_per_byte = bfd_octets_per_byte(handle->bfd);
	disasm_info.skip_zeroes = 8;
	disasm_info.skip_zeroes_at_end = 3;
	disasm_info.disassembler_needs_relocs = 0;
	disasm_info.buffer_length = inst_size;
	disasm_info.buffer = inst;

	printf("disassemble_fn\n");
	disassemble_fn = disassembler(handle->bfd);
	printf("disassemble_fn done %p, %p\n", disassemble_fn, print_insn_i386);
	dis_instructions.bytes_used = 0;
	inst_exe = &inst_log_entry[0];
	/* Where should entry_point_list_length be initialised */
	entry_point_list_length = ENTRY_POINTS_SIZE;
	/* Print the symtab */
	printf("symtab_sz = %lu\n", handle->symtab_sz);
	if (handle->symtab_sz >= 100) {
		printf("symtab too big!!! EXITING\n");
		return 1;
	}
	n = 0;
	for (l = 0; l < handle->symtab_sz; l++) {
		size_t length;
		/* FIXME: value == 0 for the first function in the .o file. */
		/*        We need to be able to handle more than
		          one function per .o file. */
		printf("section_id = %d, section_index = %d, flags = 0x%04x, value = 0x%04"PRIx64"\n",
			handle->symtab[l]->section->id,
			handle->symtab[l]->section->index,
			handle->symtab[l]->flags,
			handle->symtab[l]->value);
		if ((handle->symtab[l]->flags & 0x8) ||
			(handle->symtab[l]->flags == 0)) {
			external_entry_points[n].valid = 1;
			/* 1: Public function entry point
			 * 2: Private function entry point
			 * 3: Private label entry point
			 */
			if (handle->symtab[l]->flags & 0x8) {
				external_entry_points[n].type = 1;
			} else {
				external_entry_points[n].type = 2;
			}
			external_entry_points[n].section_offset = l;
			external_entry_points[n].section_id = 
				handle->symtab[l]->section->id;
			external_entry_points[n].section_index = 
				handle->symtab[l]->section->index;
			external_entry_points[n].value = handle->symtab[l]->value;
			length = strlen(handle->symtab[l]->name);
			external_entry_points[n].name = malloc(length+1);
			strncpy(external_entry_points[n].name, handle->symtab[l]->name, length+1);
			external_entry_points[n].process_state.memory_text =
				calloc(MEMORY_TEXT_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_stack =
				calloc(MEMORY_STACK_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_reg =
				calloc(MEMORY_REG_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_data =
				calloc(MEMORY_DATA_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_used =
				calloc(MEMORY_USED_SIZE, sizeof(int));
			memory_text = external_entry_points[n].process_state.memory_text;
			memory_stack = external_entry_points[n].process_state.memory_stack;
			memory_reg = external_entry_points[n].process_state.memory_reg;
			memory_data = external_entry_points[n].process_state.memory_data;
			memory_used = external_entry_points[n].process_state.memory_used;

			ram_init(memory_data);
			reg_init(memory_reg);
			stack_init(memory_stack);
			/* Set EIP entry point equal to symbol table entry point */
			//memory_reg[2].init_value = EIP_START;
			memory_reg[2].offset_value = external_entry_points[n].value;

			print_mem(memory_reg, 1);

			n++;
		}

	}
	printf("Number of functions = %d\n", n);
	for (n = 0; n < EXTERNAL_ENTRY_POINTS_SIZE; n++) {
		if (external_entry_points[n].valid != 0) {
		printf("type = %d, sect_offset = %d, sect_id = %d, sect_index = %d, &%s() = 0x%04"PRIx64"\n",
			external_entry_points[n].type,
			external_entry_points[n].section_offset,
			external_entry_points[n].section_id,
			external_entry_points[n].section_index,
			external_entry_points[n].name,
			external_entry_points[n].value);
		}
	}
	for (n = 0; n < handle->reloc_table_code_sz; n++) {
		int len, len1;

		len = strlen(handle->reloc_table_code[n].symbol_name);
		for (l = 0; l < EXTERNAL_ENTRY_POINTS_SIZE; l++) {
			if (external_entry_points[l].valid != 0) {
				len1 = strlen(external_entry_points[l].name);
				if (len != len1) {
					continue;
				}
				tmp = strncmp(external_entry_points[l].name, handle->reloc_table_code[n].symbol_name, len);
				if (0 == tmp) {
					handle->reloc_table_code[n].external_functions_index = l;
					handle->reloc_table_code[n].type =
						external_entry_points[l].type;
				}
			}
		}
	}
	for (n = 0; n < handle->reloc_table_code_sz; n++) {
		printf("reloc_table_code:addr = 0x%"PRIx64", size = 0x%"PRIx64", type = %d, function_index = 0x%"PRIx64", section_name=%s, symbol_name=%s\n",
			handle->reloc_table_code[n].address,
			handle->reloc_table_code[n].size,
			handle->reloc_table_code[n].type,
			handle->reloc_table_code[n].external_functions_index,
			handle->reloc_table_code[n].section_name,
			handle->reloc_table_code[n].symbol_name);
	}
			
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_SIZE; l++) {
		if ((external_entry_points[l].valid != 0) &&
			(external_entry_points[l].type == 1)) {
			struct process_state_s *process_state;
			
			process_state = &external_entry_points[l].process_state;
			memory_text = process_state->memory_text;
			memory_stack = process_state->memory_stack;
			memory_reg = process_state->memory_reg;
			memory_data = process_state->memory_data;
			memory_used = process_state->memory_used;
			external_entry_points[l].inst_log = inst_log;
			/* EIP is a parameter for process_block */
			/* Update EIP */
			//memory_reg[2].offset_value = 0;
			//inst_log_prev = 0;
			entry_point[0].used = 1;
			entry_point[0].esp_init_value = memory_reg[0].init_value;
			entry_point[0].esp_offset_value = memory_reg[0].offset_value;
			entry_point[0].ebp_init_value = memory_reg[1].init_value;
			entry_point[0].ebp_offset_value = memory_reg[1].offset_value;
			entry_point[0].eip_init_value = memory_reg[2].init_value;
			entry_point[0].eip_offset_value = memory_reg[2].offset_value;
			entry_point[0].previous_instuction = 0;
			entry_point_list_length = ENTRY_POINTS_SIZE;

			print_mem(memory_reg, 1);
			printf ("LOGS: inst_log = 0x%"PRIx64"\n", inst_log);
			do {
				not_finished = 0;
				for (n = 0; n < entry_point_list_length; n++ ) {
					/* EIP is a parameter for process_block */
					/* Update EIP */
					//printf("entry:%d\n",n);
					if (entry_point[n].used) {
						memory_reg[0].init_value = entry_point[n].esp_init_value;
						memory_reg[0].offset_value = entry_point[n].esp_offset_value;
						memory_reg[1].init_value = entry_point[n].ebp_init_value;
						memory_reg[1].offset_value = entry_point[n].ebp_offset_value;
						memory_reg[2].init_value = entry_point[n].eip_init_value;
						memory_reg[2].offset_value = entry_point[n].eip_offset_value;
						inst_log_prev = entry_point[n].previous_instuction;
						not_finished = 1;
						printf ("LOGS: EIPinit = 0x%"PRIx64"\n", memory_reg[2].init_value);
						printf ("LOGS: EIPoffset = 0x%"PRIx64"\n", memory_reg[2].offset_value);
						err = process_block(process_state, handle, inst_log_prev, entry_point_list_length, entry_point, inst_size);
						/* clear the entry after calling process_block */
						entry_point[n].used = 0;
						if (err) {
							printf("process_block failed\n");
							return err;
						}
					}
				}
			} while (not_finished);	
			external_entry_points[l].inst_log_end = inst_log - 1;
			printf ("LOGS: inst_log_end = 0x%"PRIx64"\n", inst_log);
		}
	}
/*
	if (entry_point_list_length > 0) {
		for (n = 0; n < entry_point_list_length; n++ ) {
			printf("eip = 0x%"PRIx64", prev_inst = 0x%"PRIx64"\n",
				entry_point[n].eip_offset_value,
				entry_point[n].previous_instuction);
		}
	}
*/
	//inst_log--;
	printf("Instructions=%"PRId64", entry_point_list_length=%"PRId64"\n",
		inst_log,
		entry_point_list_length);

	/* Correct inst_log to identify how many dis_instructions there have been */
	inst_log--;
	print_dis_instructions();
	if (entry_point_list_length > 0) {
		for (n = 0; n < entry_point_list_length; n++ ) {
			printf("%d, eip = 0x%"PRIx64", prev_inst = 0x%"PRIx64"\n",
				entry_point[n].used,
				entry_point[n].eip_offset_value,
				entry_point[n].previous_instuction);
		}
	}
	/************************************************************
	 * This section deals with correcting SSA for branches/joins.
	 * This bit creates the labels table, ready for the next step.
	 ************************************************************/
	printf("Number of labels = 0x%x\n", local_counter);
	label_redirect = calloc(local_counter, sizeof(struct label_redirect_s));
	labels = calloc(local_counter, sizeof(struct label_s));
	/* n <= inst_log verified to be correct limit */
	for (n = 1; n <= inst_log; n++) {
		struct label_s label;
		uint64_t value_id;

		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		printf("value to log_to_label:0x%x: 0x%x, 0x%"PRIx64", 0x%x, 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64"\n",
				n,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id);

		switch (instruction->opcode) {
		case MOV:
		case ADD:
		case ADC:
		case SUB:
		case MUL:
		case OR:
		case XOR:
		case rAND:
		case NOT:
		case SHL:
		case SHR:
		case CMP:
		case SEX:
			if (1 == instruction->dstA.indirect) {
				value_id = inst_log1->value3.indirect_value_id;
			} else {
				value_id = inst_log1->value3.value_id;
			}
			if (value_id > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			tmp = log_to_label(instruction->dstA.store,
				instruction->dstA.indirect,
				instruction->dstA.index,
				instruction->dstA.relocated,
				inst_log1->value3.value_scope,
				inst_log1->value3.value_id,
				inst_log1->value3.indirect_offset_value,
				inst_log1->value3.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value3 unknown label %x\n", n);
			}
			if (!tmp && value_id > 0) {
				label_redirect[value_id].redirect = value_id;
				labels[value_id].scope = label.scope;
				labels[value_id].type = label.type;
				labels[value_id].value = label.value;
			}

			if (1 == instruction->srcA.indirect) {
				value_id = inst_log1->value1.indirect_value_id;
			} else {
				value_id = inst_log1->value1.value_id;
			}
			if (value_id > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			tmp = log_to_label(instruction->srcA.store,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value1 unknown label %x\n", n);
			}
			if (!tmp && value_id > 0) {
				label_redirect[value_id].redirect = value_id;
				labels[value_id].scope = label.scope;
				labels[value_id].type = label.type;
				labels[value_id].value = label.value;
			}
			break;
		case CALL:
			printf("SSA CALL inst_log 0x%x\n", n);
			if (1 == instruction->dstA.indirect) {
				value_id = inst_log1->value3.indirect_value_id;
			} else {
				value_id = inst_log1->value3.value_id;
			}
			if (value_id > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			tmp = log_to_label(instruction->dstA.store,
				instruction->dstA.indirect,
				instruction->dstA.index,
				instruction->dstA.relocated,
				inst_log1->value3.value_scope,
				inst_log1->value3.value_id,
				inst_log1->value3.indirect_offset_value,
				inst_log1->value3.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value3 unknown label %x\n", n);
			}
			if (!tmp && value_id > 0) {
				label_redirect[value_id].redirect = value_id;
				labels[value_id].scope = label.scope;
				labels[value_id].type = label.type;
				labels[value_id].value = label.value;
			}
			break;
		case IF:
		case RET:
		case JMP:
			break;
		default:
			printf("SSA1 failed for Inst:0x%x, OP 0x%x\n", n, instruction->opcode);
			return 1;
			break;
		}
	}
	for (n = 0; n < local_counter; n++) {
		printf("labels 0x%x: redirect=0x%"PRIx64", scope=0x%"PRIx64", type=0x%"PRIx64", value=0x%"PRIx64"\n",
			n, label_redirect[n].redirect, labels[n].scope, labels[n].type, labels[n].value);
	}
	
	/************************************************************
	 * This section deals with correcting SSA for branches/joins.
	 * It build bi-directional links to instruction operands.
	 * This section does work for local_reg case.
	 ************************************************************/
#if 0
	for (n = 1; n < inst_log; n++) {
		struct label_s label;
		uint64_t value_id1;
		uint64_t value_id2;

		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		value_id1 = inst_log1->value1.value_id;
		value_id2 = inst_log1->value2.value_id;
		switch (instruction->opcode) {
		case MOV:
		case ADD:
		case ADC:
		case MUL:
		case OR:
		case XOR:
		case rAND:
		case SHL:
		case SHR:
		case CMP:
		default:
			break;
		/* FIXME: TODO */
		}
	}
#endif
	/************************************************************
	 * This section deals with correcting SSA for branches/joins.
	 * It build bi-directional links to instruction operands.
	 * This section does work for local_stack case.
	 ************************************************************/
	for (n = 1; n < inst_log; n++) {
		uint64_t value_id;
		uint64_t value_id1;
		uint64_t size;
		uint64_t *inst_list;
		uint64_t mid_start_size;
		struct mid_start_s *mid_start;

		size = 0;
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		value_id1 = inst_log1->value1.value_id;
		
		if (value_id1 > local_counter) {
			printf("SSA Failed at inst_log 0x%x\n", n);
			return 1;
		}
		switch (instruction->opcode) {
		case MOV:
		case ADD:
		case ADC:
		case SUB:
		case MUL:
		case OR:
		case XOR:
		case rAND:
		case NOT:
		case SHL:
		case SHR:
		case CMP:
		case SEX:
			value_id = label_redirect[value_id1].redirect;
			if ((1 == labels[value_id].scope) &&
				(2 == labels[value_id].type)) {
				printf("Found local_stack Inst:0x%x:value_id:0x%"PRIx64"\n", n, value_id1);
				if (0 == inst_log1->prev_size) {
					printf("search_back ended\n");
					return 1;
				}
				if (0 < inst_log1->prev_size) {
					mid_start = calloc(inst_log1->prev_size, sizeof(struct mid_start_s));
					mid_start_size = inst_log1->prev_size;
					for (l = 0; l < inst_log1->prev_size; l++) {
						mid_start[l].mid_start = inst_log1->prev[l];
						mid_start[l].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[l].mid_start, l);
					}
				}
				tmp = search_back_local_reg_stack(mid_start_size, mid_start, 2, inst_log1->value1.indirect_init_value, inst_log1->value1.indirect_offset_value, &size, &inst_list);
				if (tmp) {
					printf("SSA search_back Failed at inst_log 0x%x\n", n);
					return 1;
				}
			}
			printf("SSA inst:0x%x:size=0x%"PRIx64"\n", n, size);
			/* Renaming is only needed if there are more than one label present */
			if (size > 0) {
				uint64_t value_id_highest = value_id;
				inst_log1->value1.prev = calloc(size, sizeof(inst_log1->value1.prev));
				inst_log1->value1.prev_size = size;
				for (l = 0; l < size; l++) {
					struct inst_log_entry_s *inst_log_l;
					inst_log_l = &inst_log_entry[inst_list[l]];
					inst_log1->value1.prev[l] = inst_list[l];
					inst_log_l->value3.next = realloc(inst_log_l->value3.next, (inst_log_l->value3.next_size + 1) * sizeof(inst_log_l->value3.next));
					inst_log_l->value3.next[inst_log_l->value3.next_size] =
						 inst_list[l];
					inst_log_l->value3.next_size++;
					if (label_redirect[inst_log_l->value3.value_id].redirect > value_id_highest) {
						value_id_highest = label_redirect[inst_log_l->value3.value_id].redirect;
					}
					printf("rel inst:0x%"PRIx64"\n", inst_list[l]);
				}
				printf("Renaming label 0x%"PRIx64" to 0x%"PRIx64"\n",
					label_redirect[value_id1].redirect,
					value_id_highest);
				label_redirect[value_id1].redirect =
					value_id_highest;
				for (l = 0; l < size; l++) {
					struct inst_log_entry_s *inst_log_l;
					inst_log_l = &inst_log_entry[inst_list[l]];
					printf("Renaming label 0x%"PRIx64" to 0x%"PRIx64"\n",
						label_redirect[inst_log_l->value3.value_id].redirect,
						value_id_highest);
					label_redirect[inst_log_l->value3.value_id].redirect =
						value_id_highest;
				}
			}
			break;
		case IF:
		case RET:
		case JMP:
			break;
		case CALL:
			//printf("SSA2 failed for inst:0x%x, CALL\n", n);
			//return 1;
			break;
		default:
			printf("SSA2 failed for inst:0x%x, OP 0x%x\n", n, instruction->opcode);
			return 1;
			break;
		/* FIXME: TODO */
		}
	}
	/********************************************************
	 * This section filters out duplicate param_reg entries.
         * from the labels table: FIXME: THIS IS NOT NEEDED NOW
	 ********************************************************/
#if 0
	for (n = 0; n < (local_counter - 1); n++) {
		int tmp1;
		tmp1 = label_redirect[n].redirect;
		printf("param_reg:scanning base label 0x%x\n", n);
		if ((tmp1 == n) &&
			(labels[tmp1].scope == 2) &&
			(labels[tmp1].type == 1)) {
			int tmp2;
			/* This is a param_stack */
			for (l = n + 1; l < local_counter; l++) {
				printf("param_reg:scanning label 0x%x\n", l);
				tmp2 = label_redirect[l].redirect;
				if ((tmp2 == n) &&
					(labels[tmp2].scope == 2) &&
					(labels[tmp2].type == 1) &&
					(labels[tmp1].value == labels[tmp2].value) ) {
					printf("param_stack:found duplicate\n");
					label_redirect[l].redirect = n;
				}
			}
		}
	}
#endif
	/***************************************************
	 * Register labels in order to print:
	 * 	Function params,
	 *	local vars.
	 ***************************************************/
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_SIZE; l++) {
		if (external_entry_points[l].valid &&
			external_entry_points[l].type == 1) {
		tmp = scan_for_labels_in_function_body(&external_entry_points[l],
				external_entry_points[l].inst_log,
				external_entry_points[l].inst_log_end,
				label_redirect,
				labels);
		if (tmp) {
			printf("Unhandled scan instruction 0x%x\n", l);
			return 1;
		}

		/* Expected param order: %rdi, %rsi, %rdx, %rcx, %r08, %r09 
		                         0x40, 0x38, 0x18, 0x10, 0x50, 0x58, then stack */
		
		printf("scanned: params = 0x%x, locals = 0x%x\n",
			external_entry_points[l].params_size,
			external_entry_points[l].locals_size);
		}
	}

	/***************************************************
	 * This section, PARAM, deals with converting
	 * function params to reference locals.
	 * e.g. Change local0011 = function(param_reg0040);
	 *      to     local0011 = function(local0009);
	 ***************************************************/
	for (n = 1; n < inst_log; n++) {
		struct label_s *label;
		uint64_t value_id;
		uint64_t value_id1;
		uint64_t size;
		uint64_t *inst_list;
		struct extension_call_s *call;
		struct external_entry_point_s *external_entry_point;
		uint64_t mid_start_size;
		struct mid_start_s *mid_start;

		size = 0;
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		value_id1 = inst_log1->value1.value_id;
		
		if (value_id1 > local_counter) {
			printf("PARAM Failed at inst_log 0x%x\n", n);
			return 1;
		}
		switch (instruction->opcode) {
		case CALL:
			external_entry_point = &external_entry_points[instruction->srcA.index];
			inst_log1->extension = calloc(1, sizeof(call));
			call = inst_log1->extension;
			call->params_size = external_entry_point->params_size;
			call->params = calloc(call->params_size, sizeof(call->params));
			if (!call) {
				printf("PARAM failed for inst:0x%x, CALL. Out of memory\n", n);
				return 1;
			}
			printf("PARAM:call size=%x\n", call->params_size);
			printf("PARAM:params size=%x\n", external_entry_point->params_size);
			for (m = 0; m < external_entry_point->params_size; m++) {
				label = &labels[external_entry_point->params[m]];
				if (0 == inst_log1->prev_size) {
					printf("search_back ended\n");
					return 1;
				}
				if (0 < inst_log1->prev_size) {
					mid_start = calloc(inst_log1->prev_size, sizeof(struct mid_start_s));
					mid_start_size = inst_log1->prev_size;
					for (l = 0; l < inst_log1->prev_size; l++) {
						mid_start[l].mid_start = inst_log1->prev[l];
						mid_start[l].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[l].mid_start, l);
					}
				}
				/* param_regXXX */
				if ((2 == label->scope) &&
					(1 == label->type)) {
					tmp = search_back_local_reg_stack(mid_start_size, mid_start, 1, label->value, 0, &size, &inst_list);
				} else {
				/* param_stackXXX */
				/* SP value held in value1 */
					printf("PARAM: Searching for SP(0x%"PRIx64":0x%"PRIx64") + label->value(0x%"PRIx64") - 8\n", inst_log1->value1.init_value, inst_log1->value1.offset_value, label->value);
					tmp = search_back_local_reg_stack(mid_start_size, mid_start, 2, inst_log1->value1.init_value, inst_log1->value1.offset_value + label->value - 8, &size, &inst_list);
				/* FIXME: Some renaming of local vars will also be needed if size > 1 */
				}
				if (tmp) {
					printf("PARAM search_back Failed at inst_log 0x%x\n", n);
					return 1;
				}
				tmp = output_label(label, stdout);
				tmp = fprintf(stdout, ");\n");
				tmp = fprintf(stdout, "PARAM size = 0x%"PRIx64"\n", size);
				if (size > 1) {
					printf("number of param locals found too big at instruction 0x%x\n", n);
					return 1;
					break;
				}
				if (size > 0) {
					for (l = 0; l < size; l++) {
						struct inst_log_entry_s *inst_log_l;
						inst_log_l = &inst_log_entry[inst_list[l]];
						call->params[m] = inst_log_l->value3.value_id;
						//tmp = label_redirect[inst_log_l->value3.value_id].redirect;
						//label = &labels[tmp];
						//tmp = output_label(label, stdout);
					}
				}
			}
			//printf("SSA2 failed for inst:0x%x, CALL\n", n);
			//return 1;
			break;

		default:
			break;
		}
	}


	/***************************************************
	 * This section deals with outputting the .c file.
	 ***************************************************/
	filename = "test.c";
	fd = fopen(filename, "w");
	if (!fd) {
		printf("Failed to open file %s, error=%p\n", filename, fd);
		return 1;
	}
	printf(".c fd=%p\n", fd);
	printf("writing out to file\n");
	tmp = fprintf(fd, "#include <stdint.h>\n\n");
	printf("\nPRINTING MEMORY_DATA\n");
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_SIZE; l++) {
		struct process_state_s *process_state;
		if (external_entry_points[l].valid) {
			process_state = &external_entry_points[l].process_state;
			memory_data = process_state->memory_data;
			for (n = 0; n < 4; n++) {
				printf("memory_data:0x%x: 0x%"PRIx64"\n", n, memory_data[n].valid);
				if (memory_data[n].valid) {
	
					tmp = relocated_data(handle, memory_data[n].start_address, 4);
					if (tmp) {
						printf("int *data%04"PRIx64" = &data%04"PRIx64"\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
						tmp = fprintf(fd, "int *data%04"PRIx64" = &data%04"PRIx64";\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
					} else {
						printf("int data%04"PRIx64" = 0x%04"PRIx64"\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
						tmp = fprintf(fd, "int data%04"PRIx64" = 0x%"PRIx64";\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
					}
				}
			}
		}
	}
	tmp = fprintf(fd, "\n");
	printf("\n");
#if 0
	for (n = 0; n < 100; n++) {
		param_present[n] = 0;
	}
		
	for (n = 0; n < 10; n++) {
		if (memory_stack[n].start_address > 0x10000) {
			uint64_t present_index;
			present_index = memory_stack[n].start_address - 0x10000;
			if (present_index >= 100) {
				printf("param limit reached:memory_stack[%d].start_address == 0x%"PRIx64"\n",
					n, memory_stack[n].start_address);
				continue;
			}
			param_present[present_index] = 1;
			param_size[present_index] = memory_stack[n].length;
		}
	}
	for (n = 0; n < 100; n++) {
		if (param_present[n]) {
			printf("param%04x\n", n);
			tmp = param_size[n];
			n += tmp;
		}
	}
#endif

	for (l = 0; l < EXTERNAL_ENTRY_POINTS_SIZE; l++) {
		/* FIXME: value == 0 for the first function in the .o file. */
		/*        We need to be able to handle more than
		          one function per .o file. */
		printf("%d:%s:start=%"PRIu64", end=%"PRIu64"\n", l,
				external_entry_points[l].name,
				external_entry_points[l].inst_log,
				external_entry_points[l].inst_log_end);
		if (external_entry_points[l].valid &&
			external_entry_points[l].type == 1) {
			struct process_state_s *process_state;
			
			process_state = &external_entry_points[l].process_state;

			tmp = fprintf(fd, "\n");
			output_function_name(fd, &external_entry_points[l]);
			for (n = 0; n < external_entry_points[l].params_size; n++) {
				struct label_s *label;
				if (n > 0) {
					fprintf(fd, ", ");
				}
				label = &labels[external_entry_points[l].params[n]];
				fprintf(fd, "int%"PRId64"_t ",
					label->size_bits);
				tmp = output_label(label, fd);
			}
			tmp = fprintf(fd, ")\n{\n");
			for (n = 0; n < external_entry_points[l].locals_size; n++) {
				struct label_s *label;
				label = &labels[external_entry_points[l].locals[n]];
				fprintf(fd, "\tint%"PRId64"_t ",
					label->size_bits);
				tmp = output_label(label, fd);
				fprintf(fd, ";\n");
			}
			fprintf(fd, "\n");
					
			tmp = output_function_body(process_state,
				fd,
				external_entry_points[l].inst_log,
				external_entry_points[l].inst_log_end,
				label_redirect,
				labels);
			if (tmp) {
				return 1;
			}
			for (n = external_entry_points[l].inst_log; n <= external_entry_points[l].inst_log_end; n++) {
			}			
		}
	}

	fclose(fd);
	bf_test_close_file(handle);
	print_mem(memory_reg, 1);
	for (n = 0; n < inst_size; n++) {
		printf("0x%04x: %d\n", n, memory_used[n]);
	}
	printf("\nPRINTING MEMORY_DATA\n");
	for (n = 0; n < 4; n++) {
		print_mem(memory_data, n);
		printf("\n");
	}
	printf("\nPRINTING STACK_DATA\n");
	for (n = 0; n < 10; n++) {
		print_mem(memory_stack, n);
		printf("\n");
	}
	for (n = 0; n < 100; n++) {
		param_present[n] = 0;
	}
		
	for (n = 0; n < 10; n++) {
		if (memory_stack[n].start_address >= tmp) {
			uint64_t present_index;
			present_index = memory_stack[n].start_address - 0x10000;
			if (present_index >= 100) {
				printf("param limit reached:memory_stack[%d].start_address == 0x%"PRIx64"\n",
					n, memory_stack[n].start_address);
				continue;
			}
			param_present[present_index] = 1;
			param_size[present_index] = memory_stack[n].length;
		}
	}

	for (n = 0; n < 100; n++) {
		if (param_present[n]) {
			printf("param%04x\n", n);
			tmp = param_size[n];
			n += tmp;
		}
	}
	
	return 0;
}

