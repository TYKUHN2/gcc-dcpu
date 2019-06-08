/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define how DASM, the accepted standard assembler for the DCPU architecture, reads input.
	
	NOTE: This will be replaced by the GNU Assembler at some point, but this initial port is quick and dirty as few C compilers exist for the DCPU.
*/

#define ASM_COMMENT_START ";"

//Disable GNU Assembler features not found in DASM
#define ASM_APP_ON ""
#define ASM_APP_OFF ""
