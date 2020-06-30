/*
	Target definition for the DCPU-16 specifically the TechCompliant variety
	Copyright (C) 2019 Tyler Kuhn
	
	This file is an addition to GCC, not part of GCC.
	
	The purpose of this file is to define the sizes for common data types.
*/

//Define basic C types
#define CHAR_TYPE_SIZE 16
#define SHORT_TYPE_SIZE 16
#define INT_TYPE_SIZE 16
#define LONG_TYPE_SIZE 32
#define LONG_LONG_TYPE_SIZE 64

#define WCHAR_TYPE_SIZE 16
#define WCHAR_TYPE "unsigned int"

#define SIZE_TYPE "unsigned int"

#define PTR_TYPE "unsigned int"
#define PTRDIFF_TYPE "int"

//Define extra type information
#define DEFAULT_SIGNED_CHAR 0

//Define floating point sizes despite the fact we lack that capability
#define FLOAT_TYPE_SIZE 32
#define DOUBLE_TYPE_SIZE 64
#define LONG_DOUBLE_TYPE_SIZE 64
