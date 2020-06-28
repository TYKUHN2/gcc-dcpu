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

#define ASM_GENERATE_INTERNAL_LABEL(str, prefix, num) sprintf(str, "LGCC%s%d:\n", prefix, num)

#define ASM_OUTPUT_ALIGN(str, power) fprintf(str, "; GCC ALIGN %d", power);
#define ASM_OUTPUT_SKIP(str, size) fprintf(str, ".fill 0x0, %d\n", size);
#define ASM_OUTPUT_COMMON(str, name, size, aligned) assemble_name(str, name); ASM_OUTPUT_SKIP(str, aligned)
#define ASM_OUTPUT_LOCAL(str, name, size, aligned) ASM_OUTPUT_COMMON(str, name, size, aligned)

#define TARGET_HAVE_NAMED_SECTIONS false

#define GLOBAL_ASM_OP "; .global\n"
