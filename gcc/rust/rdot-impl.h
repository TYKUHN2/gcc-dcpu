/* This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>. */

#ifndef __GCC_RDOT_IMPL_H__
#define __GCC_RDOT_IMPL_H__

typedef enum {
    D_IDENTIFIER = 0,
    D_T_INTEGER,
    D_T_FLOAT,
    D_T_STRING,
    D_T_LIST,

    D_VAR_DECL,
    D_MODIFY_EXPR,
    D_MULT_EXPR,
    D_DIVD_EXPR,
    D_ADD_EXPR,
    D_MINUS_EXPR,

    D_EQ_EQ_EXPR,
    D_LESS_EXPR,
    D_LESS_EQ_EXPR,
    D_GREATER_EXPR,
    D_GREATER_EQ_EXPR,
    D_NOT_EQ_EXPR,

    D_CALL_EXPR,
    D_ATTRIB_REF,
    D_ACC_EXPR,
    
    D_STRUCT_METHOD,
    D_STRUCT_WHILE,

    D_D_EXPR,
    D_TD_COM,
    D_TD_DOT,
    D_TD_NULL,

    D_PRIMITIVE,

    D_STRUCT_IF,
    D_STRUCT_ELIF,
    D_STRUCT_ELSE,
    D_STRUCT_CONDITIONAL,

    RTYPE_BOOL,
    RTYPE_INT,
    RTYPE_FLOAT,
    RTYPE_UINT,
    // infer the type please...
    RTYPE_INFER,

    D_PARAMETER,
    D_STRUCT_TYPE,
    D_STRUCT_PARAM,
    D_STRUCT_INIT,

    RTYPE_USER_STRUCT,

    D_STRUCT_ENUM,
    D_STRUCT_IMPL,

    D_BOOLEAN,
    D_T_BOOL
} opcode_t ;

typedef enum {
    FINAL = 0,
    MUTABLE
} qualified;

typedef enum {
  ALLOC_AUTO,
  ALLOC_HEAP,
  ALLOC_STACK,
  ALLOC_BOX
} ALLOCA_;

typedef struct GTY(()) grs_rdot_tree_common {
  opcode_t T;
  union {
    int integer;
    unsigned char c;
    char * string;
    bool boolean;
  } o;
} rdot_tree_common ;

typedef struct GTY(()) grs_tree_dot {
    opcode_t T, FT, opaT, opbT;
    qualified qual;
    bool retval;
    bool reference;
    location_t loc;
    ALLOCA_ alloca_modifier;
    struct grs_tree_dot * field1;
    struct grs_tree_dot * field2;
    union {
	struct grs_tree_dot * t;
	rdot_tree_common * tc;
    } opa;
    union {
	struct grs_tree_dot * t;
	rdot_tree_common * tc;
    } opb;
    struct grs_tree_dot * next;
} * rdot;

#define RDOT_TYPE(x_)                x_->T
#define RDOT_CHAIN(x_)               x_->next
#define RDOT_T_FIELD(x_)             x_->FT
#define RDOT_FIELD(x_)               x_->field1
#define RDOT_FIELD2(x_)              x_->field2
#define RDOT_lhs_T(x_)               x_->opaT
#define RDOT_rhs_T(x_)               x_->opbT
#define RDOT_lhs_TT(x_)              x_->opa.t
#define RDOT_rhs_TT(x_)              x_->opb.t
#define RDOT_lhs_TC(x_)              x_->opa.tc
#define RDOT_rhs_TC(x_)              x_->opb.tc
#define RDOT_qual(x_)                x_->qual
#define NULL_DOT                     (rdot) 0
#define RDOT_alloc                   rdot_alloc ()
#define RDOT_CM_alloc                rdot_cm_alloc ()
#define RDOT_IDENTIFIER_POINTER(x_)  RDOT_lhs_TC (x_)->o.string
#define DOT_RETVAL(x_)               x_->retval
#define MAYBE_RETVAL(x_)	     DOT_RETVAL(x_) = true
#define RDOT_OPCODE_STR(x_)          rdot_getOpString (x_)
#define RDOT_CODE_STR(x_)            rdot_getOpString_enum (x_)
#define RDOT_LOCATION(x_)            x_->loc
#define RDOT_BOOLEAN_VAL(x_)         RDOT_lhs_TC (x_)->o.boolean
#define RDOT_MEM_MODIFIER(x_)        x_->alloca_modifier
#define RDOT_REFERENCE(x_)           x_->reference

extern rdot rdot_alloc (void);
extern void rdot_init (void);
extern rdot rdot_build_decl1 (opcode_t, rdot);
extern rdot rdot_build_decl2 (opcode_t, rdot, rdot);
/**
 * 1 = identifier; 2 = parameters
 * 3 = return type; 4 = suite
 **/
extern rdot rdot_build_fndecl (rdot, rdot, rdot, rdot);
extern rdot rdot_build_integer (const int);
extern rdot rdot_build_string (const char *);
extern rdot rdot_build_identifier (const char *);
extern rdot rdot_build_bool (bool);

/* type / final or mutable / ident */
extern rdot rdot_build_varDecl (rdot, qualified, rdot);

extern const char * rdot_getOpString (rdot);
extern const char * rdot_getOpString_enum (opcode_t);

#endif //__GCC_RDOT_IMPL_H__
