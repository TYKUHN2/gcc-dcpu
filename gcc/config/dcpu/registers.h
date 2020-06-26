/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define the DCPU architecture's registers
*/

//Define all registers
#define REG_A 0
#define REG_B 1
#define REG_C 2
#define REG_X 3
#define REG_Y 4
#define REG_Z 5
#define REG_I 6
#define REG_J 7

#define REG_PC 8
#define REG_SP 9
#define REG_EX 10

//Notify GCC how many registers we have
#define FIRST_PSEUDO_REGISTER 11

//Tell GCC how to output these registers to the assembler
#define REGISTER_NAMES {\
	"A", "B", "C",		\
	"X", "Y", "Z",		\
	"I", "J",			\
	"PC", "SP", "EX"	\
}

//Define registers
#define PC_REGNUM REG_PC
#define STACK_POINTER_REGNUM REG_SP

//Define register properties
#define FIXED_REGISTERS {			\
	0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1	\
}

#define CALL_USED_REGISTERS {		\
	1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1	\
}

//Define register classes
enum reg_class
{
  NO_REGS,
  SPECIAL_REGS,
  GENERAL_REGS,
  INDIR_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define REG_CLASS_NAMES {	\
	"NO_REGS",				\
	"SPECIAL_REGS",			\
	"GENERAL_REGS",			\
	"INDIR_REGS",			\
	"ALL_REGS"				\
}

#define N_REG_CLASSES LIM_REG_CLASSES

//Define which registers are in which classes
#define REG_CLASS_CONTENTS {	\
	{ 0b00000000000 },			\
	{ 0b00000111110 },			\
	{ 0b11111110000 },			\
	{ 0b00000000001 },			\
	{ 0b11111111111 }			\
}

#define REGNO_REG_CLASS(R) ((R == REG_IA) ? INDIR_REGS :				\
							(R > REG_Z ? SPECIAL_REGS : GENERAL_REGS))
							
#define BASE_REG_CLASS GENERAL_REGS
#define INDEX_REG_CLASS NO_REGS

//Define costs
#define MEMORY_MOVE_COST(mode, class, in) 1
#define BRANCH_COST(speed_p, predictable_p)
#define MOVE_MAX 1
#define REGISTER_MOVE_COST(mode, from, to) 1

//Define the stack registers
#define STACK_POINTER_REGNUM REG_SP
#define FRAME_POINTER_REGNUM REG_Z
#define ARG_POINTER_REGNUM FRAME_POINTER_REGNUM

#define FUNCTION_ARG_REGNO_P(regno)		\
	(regno < REG_X)
	
//Define elimination registers
#define ELIMINABLE_REGS {}
	