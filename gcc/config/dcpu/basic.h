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
#undef BITS_PER_UNIT
#define BITS_PER_UNIT 16

#define UNITS_PER_WORD 1

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

//Redefine some C storage types
#define CHAR_TYPE_SIZE 8
#define LONG_TYPE_SIZE 32
#define LONG_LONG_TYPE_SIZE 64
