/* Memory and Registers are a list of accessed stores. */
/* A record is only valid when it has been accessed. */
/* Initially the search algorithm will be slow,
 * but if the method works, fast algorithms will be used. */

struct memory_s {
	/* Start address of multibyte access. */
	uint64_t start_address;
	/* Number of bytes accessed at one time */
	int length;
	/* 0 - Unknown, 1 - Known */
	int init_value_type;
	/* Initial value when first accessed */
	uint64_t init_value;
	/* init_value + offset_value = absolute value to be used */
	uint64_t offset_value;
	/* Indirect value */
	uint64_t indirect_init_value;
	/* Indirect offset */
	uint64_t indirect_offset_value;
	/* Special case of a value_id before dereferencing */
	uint64_t indirect_value_id;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	int	value_type;
	/* last_accessed_from_instruction_at_memory_location */
	uint32_t ref_memory;
	/* last_accessed_from_instruction_log_at_location */
	uint32_t ref_log;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Global */
	int value_scope;
	/* Each time a new value is assigned, this value_id increases */
	uint64_t value_id;
	/* valid: 0 - Entry Not used yet, 1 - Entry Used */
	uint64_t valid;
	/* The instruction that assigned the value within SSA scope */
	/* If size > 1 there is more than one path between there and here */
	int prev_size;
	int *prev;
	/* The instruction that uses the value within SSA scope */
	/* If size > 1 there is more than one path between there and here */
	int next_size;
	int *next;
} ;

struct inst_log_entry_s {
	struct instruction_s instruction;	/* The instruction */
	int prev_size;
	int *prev;
	int next_size;
	int *next;
	struct memory_s value1;		/* First input value */
	struct memory_s value2;		/* Second input value */
	struct memory_s value3;		/* Result */
	void *extension;		/* Instruction specific extention */
} ;

int execute_instruction(void *self, struct process_state_s *process_state, struct inst_log_entry_s *inst);
struct memory_s *search_store(
        struct memory_s *memory, uint64_t index, int size);
struct memory_s *add_new_store(
	struct memory_s *memory, uint64_t index, int size);

//extern instructions_t instructions;
extern uint8_t *inst;
extern struct rev_eng *handle;
extern struct disassemble_info disasm_info;
extern char *dis_flags_table[];
extern uint64_t inst_log;      /* Pointer to the current free instruction log entry. */
extern char out_buf[1024];
extern int local_counter;

