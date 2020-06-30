/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define the sizes for common data types.
*/

//Define basic C types
#define CHAR_TYPE_SIZE 16
#define BOOL_TYPE_SIZE 16
#define SHORT_TYPE_SIZE 16
#define INT_TYPE_SIZE 16
#define LONG_TYPE_SIZE 16
#define LONG_LONG_TYPE_SIZE 16

#define WCHAR_TYPE "unsigned int"
#define WCHAR_TYPE_SIZE 16

#define WINT_TYPE "unsigned int"

#define INTMAX_TYPE "int"
#define UINTMAX_TYPE "int"

#define SIZE_TYPE "unsigned int"

#define Pmode HImode
#define POINTER_SIZE 16
#define PTR_TYPE "unsigned int"
#define PTRDIFF_TYPE "int"

//Define extra type information
#define DEFAULT_SIGNED_CHAR 0

//Define floating point sizes despite no support
#define FLOAT_TYPE_SIZE 16
#define DOUBLE_TYPE_SIZE 16
#define LONG_DOUBLE_TYPE_SIZE 16
#define WIDEST_HARDWARE_FP_SIZE 16

#define BIGGEST_ALIGNMENT 16
#define FASTEST_ALIGNMENT 16
