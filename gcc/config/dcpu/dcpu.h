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

//Type information
#include "types.h"

//Functional information
#include "functions.h"

//Assembler syntax
#include "asm.h"

#define TARGET_CPU_CPP_BUILTINS() {		\
	builtin_define_std("dcpu");			\
	builtin_define("__BIG_ENDIAN__");	\
}

#define TARGET_HAVE_CTORS_DTORS false
#define USE_COLLECT2 true

bool dcpu_void_false();

#define TARGET_DECIMAL_FLOAT_SUPPORTED_P dcpu_void_false
#define TARGET_FIXED_POINT_SUPPORTED_P dcpu_void_false
