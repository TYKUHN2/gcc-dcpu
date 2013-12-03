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

#include "rust.h"

static std::vector<std::map<std::string, tree> *> context;

static tree dot_pass_genFndecl_Basic (location_t, const char *, tree);
static tree dot_pass_lookupCTU (const char *);
static std::vector<tree> * dot_pass_popContext (void);
static void dot_pass_pushContext (void);
static void dot_pass_genMethodProto (rdot);
static void dot_pass_compileSuite (rdot, tree *);

static tree global_retDecl;
static bool global_retDecl_;
static tree __impl_type_decl = error_mark_node;

static
char * dot_pass_demangleImpl (const char * val)
{
  // has form of type.method_name
  size_t i;
  size_t last_dot = 0;
  for (i = 0; i < strlen (val); ++i)
    {
      if (val [i] == '.')
	last_dot = i;
    }
  size_t bsize = (strlen (val) - last_dot) * sizeof (char);
  char * buffer = (char *) xmalloc (bsize);
  memset (buffer, 0, bsize);
  strncpy (buffer, val + last_dot + 1, strlen (val) - last_dot);

  return buffer;
}

static
char * dot_pass_mangle (const char * val)
{
  // just for now pre-append __rust_[id] will do ok for now
  const char * stuff = "__rust_";
  size_t blen = (strlen (stuff) + strlen (val) + 1) * sizeof (char);
  char * retval = (char *) xmalloc (blen);
  memset (retval, 0, blen);
  snprintf (retval, blen, "%s%s", stuff, val);
  return retval;
}

static
tree dot_pass_rustToGccType (rdot type, bool consty)
{
  tree retval = error_mark_node;
  switch (RDOT_TYPE (type))
    {
    case RTYPE_INT:
      retval = integer_type_node;
      break;

    case RTYPE_USER_STRUCT:
      {
	rdot stype = RDOT_lhs_TT (type);
	const char * id = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (stype));
	retval = dot_pass_lookupCTU (id);

	if (retval == error_mark_node)
	  error ("Unable to find struct [%s]\n", id);
      }
      break;

    case D_STRUCT_TYPE:
      {
	const char * id = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (type));
	retval = dot_pass_lookupCTU (id);

	if (retval == error_mark_node)
	  error ("Unable to find struct type [%s]\n", id);
      }
      break;

    default:
      error ("Unable to figure out gcc type for [%s]\n",
             RDOT_OPCODE_STR (type));
      break;
    }
  if (consty)
    retval = build_qualified_type (retval, TYPE_QUAL_CONST);

  return retval;
}

static
tree dot_pass_genFndecl_Basic (location_t loc, const char * ident, tree fntype)
{
  tree fndecl = build_decl (loc, FUNCTION_DECL,
                            get_identifier (ident), fntype);
  TREE_STATIC (fndecl) = 0;
  TREE_USED (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  tree argslist = NULL_TREE;
  DECL_ARGUMENTS (fndecl) = argslist;

  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL,
                             NULL_TREE, TREE_TYPE (fntype));
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_ARTIFICIAL (resdecl) = true;
  DECL_IGNORED_P (resdecl) = true;
  DECL_RESULT (fndecl) = resdecl;

  if (DECL_STRUCT_FUNCTION (fndecl) == NULL)
    push_struct_function (fndecl);
  else
    push_cfun (DECL_STRUCT_FUNCTION (fndecl));
  return fndecl;
}

static
tree dot_pass_generateCString (const char * str)
{
  tree index_type = build_index_type (size_int (strlen (str)));
  tree const_char_type = build_qualified_type (char_type_node, TYPE_QUAL_CONST);
  tree string_type = build_array_type (const_char_type, index_type);
  string_type = build_variant_type_copy (string_type);

  TYPE_STRING_FLAG (string_type) = 1;
  tree string_val = build_string (strlen (str), str);
  TREE_TYPE (string_val) = string_type;

  return string_val;
}

static
tree dot_pass_lookupCTU (const char * id)
{
  tree retval = error_mark_node;

  std::vector<std::map<std::string, tree> *>::reverse_iterator it;
  for (it = context.rbegin (); it != context.rend (); ++it)
    {
      std::map<std::string, tree> * ctx = *it;
      if (ctx->count (std::string (id)) > 0)
	{
          retval = ctx->at (std::string (id));
          break;
        }
    }

  return retval;
}

static
void dot_pass_pushDecl (const char * id, tree decl)
{
  tree test = dot_pass_lookupCTU (id);
  if (test == error_mark_node)
    {
      std::map<std::string, tree> * ctx = context.back ();
      (*ctx) [std::string (id)] = decl;
    }
  else
    error ("duplicate declaration of [%s]\n", id);
}

static
tree dot_pass_genScalar (rdot decl)
{
  tree retval = error_mark_node;
  gcc_assert (RDOT_TYPE (decl) == D_PRIMITIVE);
  gcc_assert (RDOT_lhs_T (decl) == D_TD_COM);

  switch (RDOT_lhs_TC (decl)->T)
    {
    case D_T_INTEGER:
      retval = build_int_cst (integer_type_node, RDOT_lhs_TC (decl)->o.integer);
      break;
        
    case D_T_STRING:
      {
        tree stringVal = dot_pass_generateCString (RDOT_lhs_TC (decl)->o.string);
        retval = build_fold_addr_expr (stringVal);
      }
      break;

    default:
      error ("invalid scalar type %i!\n", RDOT_lhs_TC (decl)->T);
      break;
    }
  return retval;
}

static
tree dot_pass_genifyCall (tree mfndecl, vec<tree,va_gc> * arguments)
{
  tree retval = error_mark_node;
  if (TREE_CODE (mfndecl) == FUNCTION_DECL)
    {
      // size_t len = arguments->length ();
      // size_t lparms = 0;
      // tree types = TYPE_ARG_TYPES (mfndecl);

      /* really need to check the calling types and number of arguments */

      retval = build_call_expr_loc_vec (UNKNOWN_LOCATION, mfndecl, arguments);
    }
  else
    error ("trying to call a function which isn't callable [%s]",
	   IDENTIFIER_POINTER (mfndecl));
  return retval;
}

static
tree dot_pass_lowerExpr (rdot dot, tree * block)
{
  tree retval = error_mark_node;
  switch (RDOT_TYPE (dot))
    {
    case D_PRIMITIVE:
      retval = dot_pass_genScalar (dot);
      break;

    case D_IDENTIFIER:
      {
	const char * id = RDOT_IDENTIFIER_POINTER (dot);
	retval = dot_pass_lookupCTU (id);
	if (retval == error_mark_node)
	  error ("no such id [%s] in scope", id);
      }
      break;

    case D_STRUCT_INIT:
      {
	// need to go fetch the type and build the constructor...
	size_t count = 0;
	tree root_type = dot_pass_rustToGccType (RDOT_FIELD (dot), false);

	vec<constructor_elt, va_gc> *init;
	vec_alloc (init, count + 1);
	tree fields = TYPE_FIELDS (root_type);

	tree fnext;
	for (fnext = fields; fnext != NULL_TREE; fnext = DECL_CHAIN (fnext))
	  count++;
	fnext = error_mark_node;

	/*
	  FIXME this is all very buggy:
	  eg:
	  struct test {
	    x : int
            y : int
	  }

	  initilize with test { x: 1, x: 1} will pass but it should fail
	  needs more validation at dataflow level and here
	 */

	rdot next;
	size_t valid = 0;
	for (next = RDOT_rhs_TT (dot); next != NULL_DOT; next = RDOT_CHAIN (next))
	  {
	    constructor_elt empty = { NULL, NULL };
	    constructor_elt * elt = init->quick_push (empty);

	    gcc_assert (RDOT_TYPE (next) == D_STRUCT_PARAM);

	    bool found = false;
	    for (fnext = fields; fnext != NULL_TREE; fnext = DECL_CHAIN (fnext))
	      {
		const char * pid = IDENTIFIER_POINTER (DECL_NAME (fnext));
		const char * sid = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next));
		if (strcmp (pid, sid) == 0)
		  {
		    found = true;
		    break;
		  }
	      }
	    if (!found)
	      {
		error ("Unable to find field [%s] in struct [%s]",
		       IDENTIFIER_POINTER (TYPE_NAME (root_type)),
		       RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next)));
		break;
	      }
	    
	    elt->index = fnext;
	    elt->value = dot_pass_lowerExpr (RDOT_rhs_TT (next), block);
	    valid++;
	  }

	if (valid != count)
	  {
	    error ("Cannot initilize struct required [%lu] fields got [%lu]",
		   valid, count);
	    // TODO better diagnostic make a map of the initilized so
	    // we can display the un initilized to the user
	  }
	else
	  retval = build_constructor (root_type, init);
      }
      break;

    case D_CALL_EXPR:
      {
        const char * fnid = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (dot));

        rdot ptr;
        vec<tree,va_gc> * arguments;
        vec_alloc (arguments, 0);
        for (ptr = RDOT_rhs_TT (dot); ptr != NULL_DOT;
             ptr = RDOT_CHAIN (ptr))
          vec_safe_push (arguments, dot_pass_lowerExpr (ptr, block));

        /* lookup the function prototype */
        tree lookup = dot_pass_lookupCTU (fnid);
        if (lookup != error_mark_node)
          retval = dot_pass_genifyCall (lookup, arguments);
        else
          {
            error ("Unable to find callable %s\n", fnid);
            retval = error_mark_node;
          }
      }
      break;

    case D_ATTRIB_REF:
      {
	rdot lhs = RDOT_lhs_TT (dot);
	rdot rhs = RDOT_rhs_TT (dot);

	tree lookup = dot_pass_lowerExpr (lhs, block);

	switch (RDOT_TYPE (rhs))
	  {
	  case D_CALL_EXPR:
	    {
	      rdot crid = RDOT_lhs_TT (rhs);
	      const char * rlookup = RDOT_IDENTIFIER_POINTER (crid);
	      tree tid = TYPE_NAME (TREE_TYPE (lookup));
	      const char * ctid = IDENTIFIER_POINTER (tid);

	      tree type_decl = dot_pass_lookupCTU (ctid);
	      // just to be sure but we will have already error'd at this point..
	      gcc_assert (type_decl != error_mark_node);

	      tree mths = TYPE_METHODS (type_decl);
	      tree next;
	      for (next = mths; next != NULL_TREE; next = DECL_CHAIN (next))
		{
		  const char * mid = IDENTIFIER_POINTER (DECL_NAME (next));
		  char * demangle = dot_pass_demangleImpl (mid);
		  if (strcmp (rlookup, demangle) == 0)
		    {
		      vec<tree,va_gc> * cargs;
		      vec_alloc (cargs, 0);
		      vec_safe_push (cargs, lookup);

		      rdot pnext;
		      for (pnext = RDOT_rhs_TT (rhs); pnext != NULL_DOT;
			   pnext = RDOT_CHAIN (pnext))
			vec_safe_push (cargs, dot_pass_lowerExpr (pnext, block));

		      retval = dot_pass_genifyCall (next, cargs);
		      break;
		    }
		}
	    }
	    break;

	  case D_IDENTIFIER:
	    {
	      const char * rlookup = RDOT_IDENTIFIER_POINTER (rhs);
	      tree fields = TYPE_FIELDS (TREE_TYPE (lookup));
	      tree next;
	      for (next = fields; next != NULL_TREE; next = DECL_CHAIN (next))
		{
		  const char * fid = IDENTIFIER_POINTER (DECL_NAME (next));
		  if (strcmp (rlookup, fid) == 0)
		    {
		      /* no idea why we need build3 here but build2 fails... */
		      retval = build3 (COMPONENT_REF, TREE_TYPE (next),
				       lookup, next, NULL_TREE);
		      break;
		    }
		}
	    }
	    break;

	  default:
	    fatal_error ("Really don't know what happened here!\n");
	    break;
	  }
      }
      break;

    case D_MODIFY_EXPR:
      {
	tree assignment = dot_pass_lowerExpr (RDOT_rhs_TT (dot), block);
	// the compution compiled first so we can correct the types if nessecary
	tree decl = dot_pass_lowerExpr (RDOT_lhs_TT (dot), block);
        tree assign = build2 (MODIFY_EXPR, TREE_TYPE (decl),
			      decl, assignment);
        retval = assign;
      }
      break;

    case D_ADD_EXPR:
      {
	rdot lhs = RDOT_lhs_TT (dot);
        rdot rhs = RDOT_rhs_TT (dot);

        tree xlhs = dot_pass_lowerExpr (lhs, block);
        tree xrhs = dot_pass_lowerExpr (rhs, block);

	retval = build2 (PLUS_EXPR, TREE_TYPE (xlhs),
			 xlhs, xrhs);
      }
      break;

    case D_MINUS_EXPR:
      {
	rdot lhs = RDOT_lhs_TT (dot);
        rdot rhs = RDOT_rhs_TT (dot);

        tree xlhs = dot_pass_lowerExpr (lhs, block);
        tree xrhs = dot_pass_lowerExpr (rhs, block);

	retval = build2 (MINUS_EXPR, TREE_TYPE (xlhs),
			 xlhs, xrhs);
      }
      break;

      case D_MULT_EXPR:
      {
	rdot lhs = RDOT_lhs_TT (dot);
        rdot rhs = RDOT_rhs_TT (dot);

        tree xlhs = dot_pass_lowerExpr (lhs, block);
        tree xrhs = dot_pass_lowerExpr (rhs, block);

	retval = build2 (MULT_EXPR, TREE_TYPE (xlhs),
			 xlhs, xrhs);
      }
      break;

      case D_LESS_EQ_EXPR:
      {
	rdot lhs = RDOT_lhs_TT (dot);
        rdot rhs = RDOT_rhs_TT (dot);

        tree xlhs = dot_pass_lowerExpr (lhs, block);
        tree xrhs = dot_pass_lowerExpr (rhs, block);

	retval = build2 (LE_EXPR, TREE_TYPE (xlhs),
			 xlhs, xrhs);
      }
      break;

    case D_VAR_DECL:
        {
          const char * varID = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (dot));
          bool consty = false;
          if (RDOT_qual (dot) == FINAL)
            consty = true;
          tree gcc_type = dot_pass_rustToGccType (RDOT_rhs_TT (dot), consty);
          tree decl = build_decl (RDOT_LOCATION (dot),
                                  VAR_DECL, get_identifier (varID),
                                  gcc_type);
          if (dot_pass_lookupCTU (varID) == error_mark_node)
            dot_pass_pushDecl (varID, decl);
          retval = decl;
        }
      break;

    default:
      error ("unhandled binary operation type [%s]!\n", RDOT_OPCODE_STR (dot));
      break;
    }

  if (DOT_RETVAL (dot))
    {
      if (global_retDecl != error_mark_node)
        {
          tree retass = fold_build2_loc (RDOT_LOCATION (dot),
                                         MODIFY_EXPR, TREE_TYPE (global_retDecl),
                                         global_retDecl, retval);
          append_to_statement_list (retass, block);
          global_retDecl_ = true;
        }
    }

  return retval;
}

static
void dot_pass_compileCond (rdot node, tree * block)
{
  rdot ifblock = RDOT_lhs_TT (node);
  rdot elseblock = RDOT_rhs_TT (node);

  tree endif_label_decl = build_decl (BUILTINS_LOCATION, LABEL_DECL,
				      create_tmp_var_name ("ENDIF"),
				      void_type_node);
  tree endif_label_expr = fold_build1_loc (BUILTINS_LOCATION, LABEL_EXPR,
					   void_type_node, endif_label_decl);
  DECL_CONTEXT (endif_label_decl) = current_function_decl;

  tree else_label_expr = error_mark_node;
  tree else_label_decl = error_mark_node;
  if (elseblock != NULL_DOT)
    {
      else_label_decl = build_decl (BUILTINS_LOCATION, LABEL_DECL,
				    create_tmp_var_name ("ELSE"),
				    void_type_node);
      else_label_expr = fold_build1_loc (BUILTINS_LOCATION, LABEL_EXPR,
					 void_type_node, else_label_decl);
      DECL_CONTEXT (else_label_decl) = current_function_decl;
    }
  else
    {
      else_label_expr = endif_label_expr;
      else_label_decl = endif_label_decl;
    }

  tree cond = dot_pass_lowerExpr (RDOT_lhs_TT (ifblock), block);
  tree conditional = build3_loc (RDOT_LOCATION (node), COND_EXPR, void_type_node,
				 cond,
				 NULL_TREE,
				 build1 (GOTO_EXPR, void_type_node, else_label_decl));

  append_to_statement_list (conditional, block);
  dot_pass_compileSuite (RDOT_rhs_TT (ifblock), block);
  append_to_statement_list (build1 (GOTO_EXPR, void_type_node, endif_label_decl),
			    block);
  if (elseblock)
    {
      append_to_statement_list (else_label_expr, block);
      dot_pass_compileSuite (RDOT_lhs_TT (elseblock), block);
      append_to_statement_list (endif_label_expr, block);
    }
  else
    append_to_statement_list (endif_label_expr, block);
}

static
void dot_pass_compileWhile (rdot node, tree * block)
{
  rdot condition = RDOT_lhs_TT (node);
  rdot suite = RDOT_rhs_TT (node);

  tree start_label_decl = build_decl (BUILTINS_LOCATION, LABEL_DECL,
				      create_tmp_var_name ("START"),
				      void_type_node);
  tree start_label_expr = fold_build1_loc (BUILTINS_LOCATION, LABEL_EXPR,
					   void_type_node, start_label_decl);
  DECL_CONTEXT (start_label_decl) = current_function_decl;

  tree end_label_decl = build_decl (BUILTINS_LOCATION, LABEL_DECL,
				    create_tmp_var_name ("END"),
				    void_type_node);
  tree end_label_expr = fold_build1_loc (BUILTINS_LOCATION, LABEL_EXPR,
					 void_type_node, end_label_decl);
  DECL_CONTEXT (end_label_decl) = current_function_decl;

  /* -- -- -- */
  append_to_statement_list (start_label_expr, block);

  tree cond = dot_pass_lowerExpr (condition, block);
  tree conditional = build3_loc (RDOT_LOCATION (node), COND_EXPR, void_type_node,
				 cond, NULL_TREE,
				 build1 (GOTO_EXPR, void_type_node, end_label_decl));
  append_to_statement_list (conditional, block);
  dot_pass_compileSuite (suite, block);
  append_to_statement_list (build1 (GOTO_EXPR, void_type_node, start_label_decl), block);
  append_to_statement_list (end_label_expr, block);
}

static
void dot_pass_compileSuite (rdot suite, tree * block)
{
  rdot node;
  for (node = suite; node != NULL_DOT; node = RDOT_CHAIN (node))
    {
      if (RDOT_T_FIELD (node) ==  D_D_EXPR)
	append_to_statement_list (dot_pass_lowerExpr (node, block), block);
      else
        {
          switch (RDOT_TYPE (node))
            {
	    case D_STRUCT_IF:
	      dot_pass_compileCond (node, block);
	      break;

	    case D_STRUCT_WHILE:
	      dot_pass_compileWhile (node, block);
	      break;

            default:
              error ("Unhandled statement [%s]\n", RDOT_OPCODE_STR (node));
              break;
            }
        }
    }
}

static
void dot_pass_genMethodProto (rdot node)
{
  const char * method_id = RDOT_IDENTIFIER_POINTER (RDOT_FIELD (node));
  if (dot_pass_lookupCTU (method_id) != error_mark_node)
    {
      error ("Duplicate declaration of function [%s]\n", method_id);
      return;
    }
  tree rtype = void_type_node;
  if (RDOT_FIELD2 (node))
    rtype = dot_pass_rustToGccType (RDOT_FIELD2 (node), false);

  rdot parameters = RDOT_lhs_TT (node);
  tree fntype = error_mark_node;
  if (parameters != NULL_DOT)
    {
      size_t nparams = 0;
      rdot prm;
      for (prm = parameters; prm != NULL_DOT; prm = RDOT_CHAIN (prm))
	nparams++;

      tree * gccparams = XALLOCAVEC (tree, nparams);
      size_t i = 0;
      for (prm = parameters; prm != NULL_DOT; prm = RDOT_CHAIN (prm))
	{
	  bool mut = false;
	  if (RDOT_qual (prm))
	    mut = true;
	  gccparams [i] = dot_pass_rustToGccType (RDOT_rhs_TT (prm), mut);
	  i++;
	}
      fntype = build_function_type_array (rtype, nparams, gccparams);
    }
  else
    fntype = build_function_type_list (rtype, NULL_TREE);

  tree fndecl = dot_pass_genFndecl_Basic (RDOT_LOCATION (node), method_id, fntype);
  SET_DECL_ASSEMBLER_NAME (fndecl, get_identifier (dot_pass_mangle (method_id)));
  dot_pass_pushDecl (method_id, fndecl);
}

static
tree dot_pass_genifyTopFndecl (rdot node)
{
  const char * method_id;
  if (__impl_type_decl != error_mark_node)
    {
      char * mid = RDOT_IDENTIFIER_POINTER (RDOT_FIELD (node));
      tree spfx = TYPE_NAME (__impl_type_decl);
      const char *pfx = IDENTIFIER_POINTER (spfx);

      size_t len = strlen (mid) + strlen (pfx) + 2;
      size_t bsize = len * sizeof (char);
      char * buffer = (char *) alloca (bsize);
      gcc_assert (buffer);
      memset (buffer, 0, bsize);

      snprintf (buffer, bsize, "%s.%s", pfx, mid);
      method_id = buffer;
    }
  else
    method_id = RDOT_IDENTIFIER_POINTER (RDOT_FIELD (node));

  tree rtype = void_type_node;
  if (RDOT_FIELD2 (node))
    rtype = dot_pass_rustToGccType (RDOT_FIELD2 (node), false);

  rdot parameters = RDOT_lhs_TT (node);
  tree fntype = error_mark_node;
  if (parameters != NULL_DOT)
    {
      size_t nparams = 0;
      rdot prm;
      for (prm = parameters; prm != NULL_DOT; prm = RDOT_CHAIN (prm))
	nparams++;

      tree * gccparams = XALLOCAVEC (tree, nparams);
      size_t i = 0;
      for (prm = parameters; prm != NULL_DOT; prm = RDOT_CHAIN (prm))
	{
	  bool mut = false;
	  if (RDOT_qual (prm))
	    mut = true;

	  const char * pid = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (prm));
	  if (strcmp (pid, "self") == 0)
	    {
	      tree self_type = __impl_type_decl;
	      if (RDOT_REFERENCE (prm) == true)
		self_type = build_pointer_type (self_type);
	      if (RDOT_qual (prm) == FINAL)
		self_type = build_qualified_type (self_type, TYPE_QUAL_CONST);
	      gccparams [i] = self_type;
	    }
	  else
	    gccparams [i] = dot_pass_rustToGccType (RDOT_rhs_TT (prm), mut);
	  i++;
	}
      fntype = build_function_type_array (rtype, nparams, gccparams);
    }
  else
    fntype = build_function_type_list (rtype, NULL_TREE);

  tree fndecl = dot_pass_genFndecl_Basic (RDOT_LOCATION (node), method_id, fntype);
  SET_DECL_ASSEMBLER_NAME (fndecl, get_identifier (dot_pass_mangle (method_id)));
  dot_pass_pushContext ();
  
  rdot rdot_params = RDOT_lhs_TT (node);
  if (rdot_params != NULL_DOT)
    {
      tree argslist = NULL_TREE;
      rdot next;
      for (next = rdot_params; next != NULL_DOT; next = RDOT_CHAIN (next))
	{
	  const char * pid = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next));
	  if (dot_pass_lookupCTU (pid) != error_mark_node)
	    error ("paramater [%s] is already declared", pid);

	  tree ptype = error_mark_node;
	  if (strcmp (pid, "self") == 0)
	    {
	      tree self_type = __impl_type_decl;
	      if (RDOT_REFERENCE (next) == true)
		self_type = build_pointer_type (self_type);
	      if (RDOT_qual (next) == FINAL)
		self_type = build_qualified_type (self_type, TYPE_QUAL_CONST);
	      ptype = self_type;
	    }
	  else
	    ptype = dot_pass_rustToGccType (RDOT_rhs_TT (next), false);

	  tree param = build_decl (RDOT_LOCATION (node), PARM_DECL,
				   get_identifier (pid), ptype);
	  DECL_CONTEXT (param) = fndecl;
	  DECL_ARG_TYPE (param) = ptype;
	  TREE_READONLY (param) = true;
	  TREE_USED (param) = true;
	  argslist = chainon (argslist, param);

	  dot_pass_pushDecl (pid, param);
	}
      DECL_ARGUMENTS (fndecl) = argslist;
    }

  current_function_decl = fndecl;
  tree block = alloc_stmt_list ();

  if (rtype != void_type_node)
    {
      global_retDecl = DECL_RESULT (fndecl);
      global_retDecl_ = false;
    }

  // compile the block...
  dot_pass_compileSuite (RDOT_rhs_TT (node), &block);

  // make sure it returns something!!!
  if (rtype != void_type_node)
    {
      if (global_retDecl_ == false)
        {
          error ("Function [%s] doesn't seem to return anything!!\n", method_id);
          return error_mark_node;
        }
      tree returnVal = build1 (RETURN_EXPR, rtype,
                               global_retDecl);
      append_to_statement_list (returnVal, &block);
    }

  tree bind = NULL_TREE;
  tree declare_vars = DECL_RESULT (fndecl);

  tree head = declare_vars;
  std::vector<tree> * decl_vars = dot_pass_popContext ();
  std::vector<tree>::iterator it;
  for (it = decl_vars->begin (); it != decl_vars->end (); ++it)
    {
      if (TREE_CODE (*it) != PARM_DECL)
	{
	  DECL_CHAIN (head) = *it;
	  head = *it;
	}
    }
  delete decl_vars;

  tree bl = make_node (BLOCK);
  BLOCK_SUPERCONTEXT (bl) = fndecl;
  DECL_INITIAL (fndecl) = bl;
  BLOCK_VARS(bl) = declare_vars;
  TREE_USED (bl) = true;

  bind = build3 (BIND_EXPR, void_type_node, BLOCK_VARS (bl),
                 NULL_TREE, bl);
  TREE_SIDE_EFFECTS (bind) = 1;
  /* Finalize the main function */
  BIND_EXPR_BODY (bind) = block;
  block = bind;
  DECL_SAVED_TREE (fndecl) = block;

  gimplify_function_tree (fndecl);
  cgraph_finalize_function (fndecl, false);

  pop_cfun ();

  // reset them
  global_retDecl = error_mark_node;
  global_retDecl_ = false;

  return fndecl;
}

static
tree dot_pass_genifyStruct (rdot node)
{
  rdot layout = RDOT_rhs_TT (node);
  tree userStruct = make_node (RECORD_TYPE);

  bool first = true;
  tree head_chain = NULL_TREE;
  tree curr = head_chain;

  rdot next;
  for (next = layout; next != NULL_DOT; next = RDOT_CHAIN (next))
    {
      gcc_assert (RDOT_TYPE (next) == D_PARAMETER);

      tree name = get_identifier (RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next)));
      tree type = dot_pass_rustToGccType (RDOT_rhs_TT (next), false);
      tree field = build_decl (RDOT_LOCATION (node),
			       FIELD_DECL, name, type);
      DECL_CONTEXT (field) = userStruct;

      if (first == true)
	{
	  head_chain = curr = field;
	  first = false;
	  continue;
	}
      DECL_CHAIN (curr) = field;
      curr = field;
    }

  TYPE_FIELDS (userStruct) = head_chain;
  layout_type (userStruct);

  const char * struct_id = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (node));
  tree type_decl = build_decl (RDOT_LOCATION (node), TYPE_DECL,
			       get_identifier (struct_id), userStruct);
  TYPE_NAME (userStruct) = get_identifier (struct_id);
  grs_preserve_from_gc (type_decl);
  rest_of_decl_compilation (type_decl, 1, 0);

  dot_pass_pushDecl (struct_id, userStruct);
  return type_decl;
}

static
std::vector<tree> * dot_pass_genifyImplBlock (rdot node)
{
  std::vector<tree> * retval = new std::vector<tree>;
  // look up the struct type to set TYPE_METHODS on it...
  const char * implid = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (node));
  tree type_decl = dot_pass_lookupCTU (implid);
  if (type_decl == error_mark_node)
    error ("type [%s] does not exist for impl block", implid);
  else
    {
      __impl_type_decl = type_decl;
      rdot decl;
      tree fndecl_chain = error_mark_node, curr = error_mark_node;
      bool first = true;
      for (decl = RDOT_rhs_TT (node); decl != NULL_DOT; decl = RDOT_CHAIN (decl))
	{
	  tree fndecl = dot_pass_genifyTopFndecl (decl);
	  retval->push_back (fndecl);
	  if (first == true)
	    {
	      fndecl_chain = fndecl;
	      curr = fndecl_chain;
	      first = false;
	    }
	  else
	    {
	      DECL_CHAIN (curr) = fndecl;
	      curr = fndecl;
	    }
	}
      TYPE_METHODS (__impl_type_decl) = fndecl_chain;
      __impl_type_decl = error_mark_node;
    }
  return retval;
}

static
std::vector<tree> * dot_pass_genifyTopNode (rdot node)
{
  std::vector<tree> * retval = NULL;
  switch (RDOT_TYPE (node))
    {
    case D_STRUCT_METHOD:
      {
	retval = new std::vector<tree>;
	retval->push_back (dot_pass_genifyTopFndecl (node));
      }
      break;

      // nothing to do here...
    case D_STRUCT_TYPE:
    case D_STRUCT_IMPL:
      break;

    default:
      error ("Unhandled Toplevel declaration [%s]\n", RDOT_OPCODE_STR (node));
      break;
    }
  return retval;
}

static
void dot_pass_setupContext (void)
{
  std::map<std::string, tree> * lgrs = new std::map<std::string, tree>();
  rs_fill_runtime_decls (lgrs);
  context.push_back (lgrs);
}

static
void dot_pass_pushContext (void)
{
  std::map<std::string, tree> * nctx = new std::map<std::string, tree>;
  context.push_back (nctx);
}

static
std::vector<tree> * dot_pass_popContext (void)
{
  std::vector<tree> * retval = new std::vector<tree>;
  if (context.size () > 0)
    {
      std::map<std::string, tree> * popd = context.back ();
      context.pop_back ();

      std::map<std::string, tree>::iterator it;
      for (it = popd->begin (); it != popd->end (); ++it)
        retval->push_back (it->second);

      delete popd;
    }
  return retval;
}

vec<tree,va_gc> * dot_pass_Genericify (vec<rdot,va_gc> * decls)
{
  vec<tree,va_gc> * retval;
  vec_alloc (retval, 0);

  dot_pass_setupContext ();
  dot_pass_pushContext ();

  size_t i;
  rdot idtx = NULL_DOT;

  /* fill up the prototypes now ... */
  for (i = 0; decls->iterate (i, &idtx); ++i)
    {
      rdot node = idtx;
      switch (RDOT_TYPE (node))
	{
	case D_STRUCT_METHOD:
	  dot_pass_genMethodProto (node);
	  break;

	case D_STRUCT_TYPE:
	  {
	    tree gen = dot_pass_genifyStruct (node);
	    vec_safe_push (retval, gen);
	  }
	  break;

	default:
	  break;
	}
    }

  if (seen_error ())
    goto exit;

  for (i = 0; decls->iterate (i, &idtx); ++i)
    {
      rdot node = idtx;
      switch (RDOT_TYPE (node))
	{
	case D_STRUCT_IMPL:
	  {
	    std::vector<tree> * gdecls = dot_pass_genifyImplBlock (node);
	    std::vector<tree>::iterator it;
	    for (it = gdecls->begin (); it != gdecls->end (); ++it)
	      vec_safe_push (retval, *it);
	    delete gdecls;
	  }
	  break;

	default:
	  break;
	}
    }

  if (seen_error ())
    goto exit;

  __impl_type_decl = error_mark_node;
  for (i = 0; decls->iterate (i, &idtx); ++i)
    {
      std::vector<tree> * gdecls = dot_pass_genifyTopNode (idtx);
      if (gdecls != NULL)
	{
	  std::vector<tree>::iterator it;
	  for (it = gdecls->begin (); it != gdecls->end (); ++it)
	    vec_safe_push (retval, *it);
	  delete gdecls;
	}
    }

 exit:
  dot_pass_popContext ();
  return retval;
}
