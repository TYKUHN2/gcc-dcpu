#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "%{!mno-crt0:crt0%O%s} crti.o%s crtbegin.o%s"

   /* Provide an ENDFILE_SPEC appropriate for svr4.  Here we tack on our own
      magical crtend.o file (see crtstuff.c) which provides part of the
      support for getting C++ file-scope static object constructed before
      entering `main', followed by the normal svr3/svr4 "finalizer" file,
      which is either `gcrtn.o' or `crtn.o'.  */

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

      /* Provide a LIB_SPEC appropriate for svr4.  Here we tack on the default
         standard C library (unless we are building a shared library) and
         the simulator BSP code.  */

#undef LIB_SPEC
#define LIB_SPEC "%{!shared:%{!symbolic:-lc}}"

#undef  LINK_SPEC
#define LINK_SPEC "%{h*} %{v:-V} %{!mel:-EB} %{mel:-EL}\
		   %{static:-Bstatic} %{shared:-shared} %{symbolic:-Bsymbolic}"

#ifndef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS { "meb" }
#endif

/* We can't copy to or from our CC register. */
#define AVOID_CCMODE_COPIES 1

   /* The Overall Framework of an Assembler File */

#undef  ASM_SPEC
#define ASM_SPEC "%{!mel:-EB} %{mel:-EL}"

#define FILE_ASM_OP     "\t.file\n"

/* Switch to the text or data segment.  */
#define TEXT_SECTION_ASM_OP  "\t.text"
#define DATA_SECTION_ASM_OP  "\t.data"

   /* If defined, the maximum amount of space required for outgoing arguments
      will be computed and placed into the variable
      `current_function_outgoing_args_size'.  No space will be pushed
      onto the stack for each call; instead, the function prologue should
      increase the stack frame size by this amount.  */
#define ACCUMULATE_OUTGOING_ARGS 1

      /* A C statement (sans semicolon) for initializing the variable CUM
         for the state at the beginning of the argument list.
         For moxie, the first arg is passed in register 2 (aka $r0).  */
#define INIT_CUMULATIVE_ARGS(CUM,FNTYPE,LIBNAME,FNDECL,N_NAMED_ARGS) \
  (CUM = MOXIE_R0)

         /* How Scalar Function Values Are Returned */

         /* STACK AND CALLING */

            /* Define this if the above stack space is to be considered part of the
               space allocated by the caller.  */
#define OUTGOING_REG_PARM_STACK_SPACE(FNTYPE) 1
#define STACK_PARMS_IN_REG_PARM_AREA

               /* Define this if it is the responsibility of the caller to allocate
                  the area reserved for arguments passed in registers.  */
#define REG_PARM_STACK_SPACE(FNDECL) (6 * UNITS_PER_WORD)

                  /* Offset from the argument pointer register to the first argument's
                     address.  On some machines it may depend on the data type of the
                     function.  */
#define FIRST_PARM_OFFSET(F) 12

                        /* Define this macro as a C expression that is nonzero for registers that are
                           used by the epilogue or the return pattern.  The stack and frame
                           pointer registers are already assumed to be used as needed.  */
#define EPILOGUE_USES(R) (R == MOXIE_R5)

                              /* Describe how we implement __builtin_eh_return.  */
#define EH_RETURN_DATA_REGNO(N)	((N) < 4 ? (N+2) : INVALID_REGNUM)

/* Storage Layout */

#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN ( ! TARGET_LITTLE_ENDIAN )
#define WORDS_BIG_ENDIAN ( ! TARGET_LITTLE_ENDIAN )

/* Alignment required for a function entry point, in bits.  */
#define FUNCTION_BOUNDARY 16

/* Define this macro as a C expression which is nonzero if accessing
   less than a word of memory (i.e. a `char' or a `short') is no
   faster than accessing a word of memory.  */
#define SLOW_BYTE_ACCESS 1

   /* Number of storage units in a word; normally the size of a
      general-purpose register, a power of two from 1 or 8.  */
#define UNITS_PER_WORD 4

      /* Define this macro to the minimum alignment enforced by hardware
         for the stack pointer on this machine.  The definition is a C
         expression for the desired alignment (measured in bits).  */
#define STACK_BOUNDARY 32

         /* Normal alignment required for function parameters on the stack, in
            bits.  All stack parameters receive at least this much alignment
            regardless of data type.  */
#define PARM_BOUNDARY 32

            /* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY  32

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT 32

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT 32

/* Every structures size must be a multiple of 8 bits.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* Look at the fundamental type that is used for a bit-field and use
   that to impose alignment on the enclosing structure.
   struct s {int a:8}; should have same alignment as "int", not "char".  */
#define	PCC_BITFIELD_TYPE_MATTERS	1

   /* Largest integer machine mode for structures.  If undefined, the default
      is GET_MODE_SIZE(DImode).  */
#define MAX_FIXED_MODE_SIZE 32

      /* Make arrays of chars word-aligned for the same reasons.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < FASTEST_ALIGNMENT ? FASTEST_ALIGNMENT : (ALIGN))

/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

   /* Generating Code for Profiling */
#define FUNCTION_PROFILER(FILE,LABELNO) (abort (), 0)

/* Trampolines for Nested Functions.  */
#define TRAMPOLINE_SIZE (2 + 6 + 4 + 2 + 6)

/* Alignment required for trampolines, in bits.  */
#define TRAMPOLINE_ALIGNMENT 32

/* An alias for the machine mode for pointers.  */
#define Pmode         SImode

/* An alias for the machine mode used for memory references to
   functions being called, in `call' RTL expressions.  */
#define FUNCTION_MODE QImode

   /* The register number of the stack pointer register, which must also
      be a fixed register according to `FIXED_REGISTERS'.  */
#define STACK_POINTER_REGNUM MOXIE_SP

      /* The register number of the frame pointer register, which is used to
         access automatic variables in the stack frame.  */
#define FRAME_POINTER_REGNUM MOXIE_QFP

         /* The register number of the arg pointer register, which is used to
            access the function's argument list.  */
#define ARG_POINTER_REGNUM MOXIE_QAP

#define HARD_FRAME_POINTER_REGNUM MOXIE_FP

#define ELIMINABLE_REGS							\
{{ FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM },			\
 { ARG_POINTER_REGNUM,   HARD_FRAME_POINTER_REGNUM }}			

            /* This macro returns the initial difference between the specified pair
               of registers.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
  do {									\
    (OFFSET) = moxie_initial_elimination_offset ((FROM), (TO));		\
  } while (0)

               /* A C expression that is nonzero if REGNO is the number of a hard
                  register in which function arguments are sometimes passed.  */
#define FUNCTION_ARG_REGNO_P(r) (r >= MOXIE_R0 && r <= MOXIE_R5)

#define HARD_REGNO_OK_FOR_BASE_P(NUM) \
  ((unsigned) (NUM) < FIRST_PSEUDO_REGISTER \
   && (REGNO_REG_CLASS(NUM) == GENERAL_REGS \
       || (NUM) == HARD_FRAME_POINTER_REGNUM))

                     /* A C expression which is nonzero if register number NUM is suitable
                        for use as a base register in operand addresses.  */
#ifdef REG_OK_STRICT
#define REGNO_OK_FOR_BASE_P(NUM)		 \
  (HARD_REGNO_OK_FOR_BASE_P(NUM) 		 \
   || HARD_REGNO_OK_FOR_BASE_P(reg_renumber[(NUM)]))
#else
#define REGNO_OK_FOR_BASE_P(NUM)		 \
  ((NUM) >= FIRST_PSEUDO_REGISTER || HARD_REGNO_OK_FOR_BASE_P(NUM))
#endif

                        /* A C expression which is nonzero if register number NUM is suitable
                           for use as an index register in operand addresses.  */
#define REGNO_OK_FOR_INDEX_P(NUM) MOXIE_FP

                              /* All load operations zero extend.  */
#define LOAD_EXTEND_OP(MEM) ZERO_EXTEND

   /* An alias for a machine mode name.  This is the machine mode that
      elements of a jump-table should have.  */
#define CASE_VECTOR_MODE SImode
