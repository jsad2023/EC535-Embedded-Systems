#include <stdio.h>
#include <stdlib.h>

#define LOCAL_MEMORY_SIZE 256
#define MAIN_MEMORY_SIZE 256
#define MAX_NO_INSTRUCTIONS 1024

// Defintions for CPU instructions

typedef enum Operation {MOV, ADD_REG, ADD_NUM, CMP, JE, JMP, LD, ST} Operation;

struct instruction_t {
	char operand1;
	char operand2;
	enum Operation operation;
};



struct cache_entry_t {
	unsigned char valid : 1;
	char data : 8;
};


// Notes:
//We have to store every CPU instruction before executing them. (JMP/JE instructions may move PC forwards and backwards)


int main(int argc, char * argv[])
{

	FILE * restrict fptr; // file pointer for assembly input text file
	struct instruction_t  instructions[MAX_NO_INSTRUCTIONS]; // array for 
	register unsigned int first_address = 0; // address of first instruction
	register unsigned int PC; // our fake "program counter" register
	register unsigned int count_executed_instructions = 0;
	register unsigned int count_clock_cycles = 0;
	register unsigned int count_hits_to_local_memory = 0;
	register unsigned int count_memory_accesses = 0;
	unsigned char CMP_VAL = 0;
	struct cache_entry_t cache[LOCAL_MEMORY_SIZE];
	char registers[6]; // Array of registers
	unsigned int count_instructions = 0;
	unsigned int i = 0;

	if(argc != 2) {
		printf("Error: ./simpleISS [Assembly Input]\n");
		exit(-1);
	}

	// Open assembly file
	fptr = fopen(argv[1], "r");
	if(fptr == NULL) {
		printf("Error: Assembly input file can't be open for reading\n");
		exit(-1);
	}

	// Initialize Local memory
	for(i = 0; i < LOCAL_MEMORY_SIZE; i++) {
		cache[i].valid = 0;
	}


	// Parse assembly file for instructions
	char line[256];
	i = 0;

	while(fgets(line, sizeof(line), fptr)) {
		unsigned int address;
		unsigned int reg1;
		unsigned int reg2;
		int number;
		++count_instructions;

		// Mov Rn, <num>
		if(sscanf(line, "%d MOV R%d, %d", &address, &reg1, &number) == 3) {
			instructions[i].operand1 = reg1 - 1;
			instructions[i].operand2 = number;
			instructions[i].operation = MOV;
		// ADD Rn, Rm
		} else if(sscanf(line, "%d ADD R%d, R%d", &address, &reg1, &reg2) == 3) {
			instructions[i].operand1 = reg1 - 1;
			instructions[i].operand2 = reg2 - 1;
			instructions[i].operation = ADD_REG;
		// ADD Rn, <num>
		} else if(sscanf(line, "%d ADD R%d, %d", &address, &reg1, &number) == 3) {
			instructions[i].operand1 = reg1 - 1;
			instructions[i].operand2 = number;
			instructions[i].operation = ADD_NUM;
			// CMP Rn, Rm
		} else if(sscanf(line, "%d CMP R%d, R%d", &address, &reg1, &reg2) == 3) {
			instructions[i].operand1 = reg1 - 1;
			instructions[i].operand2 = reg2 - 1;
			instructions[i].operation = CMP;

		// JE <address>
		} else if (sscanf(line, "%d JE %d", &address, &number) == 2) {
			instructions[i].operand1 = number;
			instructions[i].operation = JE;
		// JMP <address>
		} else if (sscanf(line, "%d JMP %d", &address, &number) == 2) {
			instructions[i].operand1 = number;
			instructions[i].operation = JMP;
		// LD Rn, [Rm]
		} else if(sscanf(line, "%d LD R%d, [R%d]", &address, &reg1, &reg2) == 3) {
			instructions[i].operand1 = reg1 - 1;
			instructions[i].operand2 = reg2 - 1;
			instructions[i].operation = LD;
		// ST [Rm], Rn
		} else if(sscanf(line, "%d ST [R%d], R%d", &address, &reg1, &reg2) == 3) {
			instructions[i].operand1 = reg1 - 1;
			instructions[i].operand2 = reg2 - 1;
			instructions[i].operation = ST;
		} else {
			printf("Unknown instruction: \"%s\" ", line);
			exit(-1);
		}

		// Store the value of the first instruction address
		if(i == 0) {
			first_address = address;
		}
		++i;
	}


	// Execute CPU instructions:
	PC = first_address;
	while(PC < first_address + count_instructions) {
		struct instruction_t instr = instructions[PC - first_address]; // get instruction
		unsigned char mem_address;
		switch(instr.operation) {
			case MOV:
				registers[(unsigned char)instr.operand1] = instr.operand2; 
				++count_clock_cycles;
				break;
			case ADD_REG:
				registers[(unsigned char)instr.operand1] += registers[(unsigned char)instr.operand2];
				++count_clock_cycles;
				break;
			case ADD_NUM:
				registers[(unsigned char)instr.operand1] += instr.operand2;
				++count_clock_cycles;
				break;
			case CMP:
				CMP_VAL = (registers[(unsigned char)instr.operand1] == registers[(unsigned char)instr.operand2]);
				++count_clock_cycles;
				break;
			case JE:
				if(CMP_VAL) {
					PC = instr.operand1 - 1;
				}
				++count_clock_cycles;
				break;
			case JMP:
				PC = instr.operand1 - 1;
				++count_clock_cycles;
				break;
			case LD:
				++count_memory_accesses;
				mem_address = (unsigned char) registers[(unsigned char)instr.operand2];


				// cache hit
				if(cache[mem_address % 256].valid) {
					++count_hits_to_local_memory;
					registers[(unsigned char)instr.operand1] = cache[mem_address].data;
					count_clock_cycles += 2;
				// cache miss
				} else {
					cache[mem_address].valid = 1;
					registers[(unsigned char)instr.operand1] = cache[mem_address].data;
					count_clock_cycles += 45;
				}

				break;
			case ST:
				
				++count_memory_accesses;
				mem_address = (unsigned char) registers[(unsigned char)instr.operand1];

				// cache hit
				if(cache[mem_address % 256].valid) {
					++count_hits_to_local_memory;
					cache[mem_address].data = registers[(unsigned char)instr.operand2];
					count_clock_cycles += 2;
				} else {
					cache[mem_address].valid = 1;
					cache[mem_address].data = registers[(unsigned char)instr.operand2];
					count_clock_cycles +=  45;
				}

				break;
		}

		++PC; // advance PC
		++count_executed_instructions;
	}

	printf("Total number of executed instructions: %d\n", count_executed_instructions); 
	printf("Total number of clock cycles: %d\n", count_clock_cycles);
	printf("Number of hits to local memory: %d\n", count_hits_to_local_memory);
	printf("Total number of executed LD/ST instructions: %d\n", count_memory_accesses); 

	fclose(fptr); // close file
	return 0;
}

