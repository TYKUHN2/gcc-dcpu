/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define some basic information for the DCPU architecture.
*/

//Define endianness (oh god)
#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 1
#define WORDS_BIG_ENDIAN 1

//Tell GCC memory is addressed in 16-bit units which is also a word
#define UNITS_PER_WORD 2
#define MAX_REGS_PER_ADDRESS 1

//Tell GCC we only operate on single, full registers
#define MAX_FIXED_MODE_SIZE 16
#define WORD_REGISTER_OPERATIONS 1

//Notify GCC that we can't address less than 1 word quickly
#define SLOW_BYTE_ACCESS 1

//Define memory alignments
#define FUNCTION_BOUNDARY 16
#define STACK_BOUNDARY 16
#define PARM_BOUNDARY 16
#define EMPTY_FIELD_BOUNDARY 16
#define BIGGEST_ALIGNMENT 16
#define STRICT_ALIGNMENT 1

//Define the machine modes used by the machine
#define Pmode HImode
#define FUNCTION_MODE HImode

//Define how the stack behaves
#define STACK_GROWS_DOWNWARD 1
#define FRAME_GROWS_DOWNWARD 1

//Define what capabilities we have
#define HAS_LONG_COND_BRANCH true
#define HAS_LONG_UNCOND_BRANCH true
