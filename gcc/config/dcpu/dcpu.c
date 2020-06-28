/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define several functions used to create the DCPU definition.
*/

#define IN_TARGET_CODE 1
#define NULL nullptr

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "stringpool.h"
#include "attribs.h"
#include "df.h"
#include "regs.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "output.h"
#include "stor-layout.h"
#include "varasm.h"
#include "calls.h"
#include "expr.h"
#include "builtins.h"

bool dcpu_legitimate_address_p(machine_mode mode, rtx x, bool strict, addr_space_t space) {
	return true;
}

#define TARGET_ADDR_SPACE_LEGITIMATE_ADDRESS_P dcpu_legitimate_address_p

rtx dcpu_function_value(const_tree ret_type, const_tree fn_decl, bool outgoing) {
	return gen_rtx_REG(TYPE_MODE(ret_type), REG_A);
}

#define TARGET_FUNCTION_VALUE dcpu_function_value

#include "dcpu.h"
#include "target.h"
#include "target-def.h"

struct gcc_target targetm = TARGET_INITIALIZER;
