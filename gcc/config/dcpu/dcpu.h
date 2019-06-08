/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define the DCPU architecture by including sub-headers and directly defining miscellaneous definitions.
*/

#pragma once

//PROMOTE_MODE -- Needed to ensure GCC doesn't think it can operate on a reduced register

//Basic architecture details
#include "basic.h"

//Architecture registers
#include "registers.h"

//Define how the stack behaves
#define STACK_GROWS_DOWNWARD 1

//Define the stack registers
#define STACK_POINTER_REG_NUM REG_SP
#define FRAME_POINTER_REG_NUM REG_Z

/*
	Define costs of instructions
*/
#define MEMORY_MOVE_COST(mode, class, in) 1
#define BRANCH_COST(speed_p, predictable_p)
#define MOVE_MAX 1

/*
	Define how to pass arguments and call functions
*/
#define CUMULATIVE_ARGS int

//Define exeception handling for C exceptions
#define EH_RETURN_DATA_REGN(N) N < 4 ? N - 1 : INVALID_REGNUM

//More exeception handling directly ripped from Moxie
#define EH_RETURN_HANDLER_RTX gen_frame_mem (Pmode,											\
								plus_constant (Pmode, frame_pointer_rtx, UNITS_PER_WORD))

//Assembler syntax
#include "asm.h"
