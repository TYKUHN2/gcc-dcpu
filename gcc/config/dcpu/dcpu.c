/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define several functions used to create the DCPU definition.
*/

#define IN_TARGET_CODE 1

#include "target.h"
#include "target-def.h"

struct gcc_target targetm = TARGET_INITIALIZER;
