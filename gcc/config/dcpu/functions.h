/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define how functions and their related data is processed
*/

//Define the processing of arguments
#define CUMULATIVE_ARGS unsigned int

//Define exeception handling for C exceptions
#define EH_RETURN_DATA_REGNO(N) N < 4 ? N - 1 : INVALID_REGNUM

//More exeception handling directly ripped from Moxie
#define EH_RETURN_HANDLER_RTX gen_frame_mem (Pmode,											\
								plus_constant (Pmode, frame_pointer_rtx, UNITS_PER_WORD))

#define INCOMING_RETURN_ADDR_RTX gen_frame_mem (Pmode,											\
								plus_constant (Pmode, frame_pointer_rtx, UNITS_PER_WORD))
								
//Define trampoline handling
#define TRAMPOLINE_ALIGNMENT 16
#define TRAMPOLINE_SIZE 2

//Define jump table handling
#define CASE_VECTOR_MODE HImode

#define INIT_CUMULATIVE_ARGS(cum, fntype, libname, fndecl, n_args) cum = REG_A

#define FUNCTION_PROFILER(file, labelno) (abort(), 0)

#define FIRST_PARM_OFFSET(fndecl) 0
