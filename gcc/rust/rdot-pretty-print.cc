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

static bool _no_infer = false;
static bool first = true;

#define RDOT_PREFIX_PRE      "pre-rdot"
#define RDOT_PREFIX_POST     "post-rdot"

static const char * typeStrings [] = {
  "bool",
  "int",
  "float",
  "unsigned_int",
  "__infer_me",
  "__user_struct",
  "void"
};

static
const char * typeStringNode (rdot node)
{
  bool _error = false;
  const char * retval = typeStrings [6];
  if (node != NULL_DOT)
    {
      switch (RDOT_TYPE (node))
        {
        case RTYPE_BOOL:
          retval = typeStrings [0];
          break;

        case RTYPE_INT:
          retval = typeStrings [1];
          break;

        case RTYPE_FLOAT:
          retval = typeStrings [2];
          break;

        case RTYPE_UINT:
          retval = typeStrings [3];
          break;

        case RTYPE_INFER:
          if (_no_infer) {
            retval = xstrdup ("__FAIL__");
            error ("gcc-rust has failed to infer a type and cannot continue");
          }
          else
            retval = typeStrings [4];
          break;

	case RTYPE_USER_STRUCT:
	  {
	    if (RDOT_lhs_TT (node) != NULL_DOT)
	      {
		rdot sdef = RDOT_lhs_TT (node);
		if (!_no_infer)
		  retval = RDOT_IDENTIFIER_POINTER (sdef);
		else
		  {
		    // ... 
		    retval = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (sdef));
		  }
	      }
	    else
	      retval = typeStrings [5];
	  }
	  break;

        default:
	  {
	    _error = false;
	    error ("unhandled type [%s]", RDOT_OPCODE_STR (node));
	  }
          break;
        }
    }

  if (node)
    if (!_error)
      {
	char modifier_char;
	switch (RDOT_MEM_MODIFIER (node))
	  {
	  case ALLOC_AUTO:
	    modifier_char = '_';
	    break;

	  case ALLOC_HEAP:
	    modifier_char = '~';
	    break;

	  case ALLOC_STACK:
	    modifier_char = '&';
	    break;

	  case ALLOC_BOX:
	    modifier_char = '@';
	    break;
	  }
	size_t blen = strlen (retval) + 5;
	char * buffer = (char *) alloca (blen);
	gcc_assert (buffer);

	snprintf (buffer, blen, "%c::%s", modifier_char, retval);
	retval = xstrdup (buffer);
      }
  return retval;
}

static void dot_pass_dump_node (FILE *, rdot, size_t);
static void dot_pass_dump_method (FILE *, rdot, size_t);
static void dot_pass_dump_struct (FILE *, rdot, size_t);

static void dot_pass_dumpPrimitive (FILE *, rdot);
static void dot_pass_dumpExprNode (FILE *, rdot);
static void dot_pass_dump_expr (FILE *, rdot);

static
void dot_pass_dump_struct (FILE * fd, rdot node, size_t indents)
{
  size_t i;
  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");

  rdot ident = RDOT_lhs_TT (node);
  rdot layout = RDOT_rhs_TT (node);

  fprintf (fd, "struct %s {\n", RDOT_IDENTIFIER_POINTER (ident));
  rdot next;
  for (next = layout; next != NULL_DOT; next = RDOT_CHAIN (next))
    {
      gcc_assert (RDOT_TYPE (next) = D_PARAMETER);
      const char * id = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next));
      const char * typestr = typeStringNode (RDOT_rhs_TT (next));

      for (i = 0; i < (indents + 1); ++i)
	fprintf (fd, "  ");
      fprintf (fd, "%s %s;\n", typestr, id);
    }
  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");
  fprintf (fd, "}\n");
}

static
void dot_pass_dump_method (FILE * fd, rdot node, size_t indents)
{
  size_t i;
  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");

  const char * method_id = RDOT_IDENTIFIER_POINTER (RDOT_FIELD (node));
  const char * rtype = typeStringNode (RDOT_FIELD2 (node));
  rdot parameters = RDOT_lhs_TT (node);

  fprintf (fd, "fn %s ( ", method_id);
  if (parameters == NULL_DOT)
    fprintf (fd, "void");
  else
    {
      rdot next;
      for (next = parameters; next != NULL_DOT; next = RDOT_CHAIN (next))
        {
          gcc_assert (RDOT_TYPE (next) = D_PARAMETER);
	  bool iself = false;
	  bool reference = RDOT_REFERENCE (next);
	  bool muta = false;
	  if (RDOT_qual (next) == MUTABLE)
	    muta = true;
          const char * id = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next));

	  if (strcmp (id, "self") == 0)
	    iself = true;

	  const char * sref;
	  if (reference) {
	    sref = "&";
	  }
	  else {
	    sref = "_";
	  }

	  const char *smuta;
	  if (muta) {
	    smuta = "mut";
	  }
	  else {
	    smuta = "final";
	  }

	  if (iself)
	    fprintf (fd, "[%s %s] _self_", sref, smuta);
	  else
	    {
	      const char * typestr = typeStringNode (RDOT_rhs_TT (next));
	      fprintf (fd, "[%s %s] %s:%s", sref, smuta, typestr, id);
	    }
          if (RDOT_CHAIN (next) != NULL_DOT)
            fprintf (fd, ", ");
        }
    }
  fprintf (fd, " ) -> %s {\n", rtype);

  rdot suite;
  for (suite = RDOT_rhs_TT (node); suite != NULL_DOT; suite = RDOT_CHAIN (suite))
    {
      dot_pass_dump_node (fd, suite, indents + 1);
      fprintf (fd, "\n");
    }

  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");
  fprintf (fd, "}\n");
}

static
void dot_pass_dumpPrimitive (FILE * fd, rdot node)
{
  /* Handle other primitive literal types here ... */
  switch (RDOT_lhs_TC (node)->T)
    {
    case D_T_INTEGER:
      fprintf (fd, "%i", RDOT_lhs_TC (node)->o.integer);
      break;

    case D_T_STRING:
      fprintf (fd, "\"%s\"", RDOT_lhs_TC (node)->o.string);
      break;

    default:
      fatal_error ("Something very wrong!\n");
      break;
    }
}

static
void dot_pass_dumpExprNode (FILE * fd, rdot node)
{
  /* print expr tree ... */
  switch (RDOT_TYPE (node))
    {
    case D_PRIMITIVE:
      dot_pass_dumpPrimitive (fd, node);
      break;

    case D_IDENTIFIER:
      fprintf (fd, "%s", RDOT_IDENTIFIER_POINTER (node));
      break;

    case D_BOOLEAN:
      {
	bool val = RDOT_BOOLEAN_VAL (node);
	if (val)
	  fprintf (fd, "true");
	else
	  fprintf (fd, "false");
      }
      break;

    case D_CALL_EXPR:
      {
        rdot id = RDOT_lhs_TT (node);
        dot_pass_dump_expr (fd, id);
        fprintf (fd, " (");

        rdot p;
        for (p = RDOT_rhs_TT (node); p != NULL_DOT; p = RDOT_CHAIN (p))
	  {
	    dot_pass_dump_expr (fd, p);
	    if (RDOT_CHAIN (p) != NULL_DOT)
	      fprintf (fd, ", ");
	  }
        fprintf (fd, ")");
      }
      break;

    case D_VAR_DECL:
      {
        const char * mut;
        if (RDOT_qual (node) == FINAL)
          mut = "_final_";
        else
          mut = "_mut_";

        fprintf (fd, "let [%s] ", mut);
        dot_pass_dumpExprNode (fd, RDOT_lhs_TT (node));
        fprintf (fd, " -> [%s]", typeStringNode (RDOT_rhs_TT (node)));
      }
      break;

    case D_STRUCT_INIT:
      {
	rdot ident = RDOT_lhs_TT (node);
	rdot init = RDOT_rhs_TT (node);

	fprintf (fd, "%s { ", RDOT_IDENTIFIER_POINTER (ident));
	rdot next;
	for (next = init; next != NULL_DOT; next = RDOT_CHAIN (next))
	  {
	    gcc_assert (RDOT_TYPE (next) == D_STRUCT_PARAM);
	    const char * name = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (next));
	    
	    fprintf (fd, "|%s := (", name);
	    dot_pass_dump_expr (fd, RDOT_rhs_TT (next));
	    fprintf (fd, ")|");
	  }
	fprintf (fd, " }");
      }
      break;

    default:
      error ("unhandled dumpExprNode [%s]\n", RDOT_OPCODE_STR (node));
      break;
    }
}

static
void dot_pass_dump_expr (FILE * fd, rdot node)
{
  if (DOT_RETVAL (node)) {
    fprintf (fd, "[_rust_retval]: ");
  }

  switch (RDOT_TYPE (node))
    {
    case D_PRIMITIVE:
    case D_IDENTIFIER:
    case D_CALL_EXPR:
    case D_BOOLEAN:
    case D_VAR_DECL:
    case D_STRUCT_INIT:
      dot_pass_dumpExprNode (fd, node);
      break;
        
    default:
      {
        /* print expr tree ... */
        rdot lhs = RDOT_lhs_TT (node);
        rdot rhs = RDOT_rhs_TT (node);
        
        dot_pass_dump_expr (fd, lhs);
        switch (RDOT_TYPE (node))
          {
          case D_MODIFY_EXPR:
            fprintf (fd, " = ");
            break;

          case D_ADD_EXPR:
            fprintf (fd, " + ");
            break;

          case D_MINUS_EXPR:
            fprintf (fd, " - ");
            break;

          case D_MULT_EXPR:
            fprintf (fd, " * ");
            break;

          case D_LESS_EXPR:
            fprintf (fd, " < ");
            break;

	  case D_LESS_EQ_EXPR:
	    fprintf (fd, " <= ");
	    break;
            
          case D_GREATER_EXPR:
            fprintf (fd, " > ");
            break;

	  case D_GREATER_EQ_EXPR:
	    fprintf (fd, " >= ");
	    break;

          case D_EQ_EQ_EXPR:
            fprintf (fd, " == ");
            break;

          case D_NOT_EQ_EXPR:
            fprintf (fd, " != ");
            break;
	    
	  case D_ATTRIB_REF:
	    fprintf (fd, ".");
	    break;

	  case D_ACC_EXPR:
	    fprintf (fd, "::");
	    break;

          default:
            fatal_error ("unhandled dump [%s]!\n", RDOT_OPCODE_STR (node));
            break;
          }
        dot_pass_dump_expr (fd, rhs);
      }
      break;
    }
}

static
void dot_pass_dump_enum (FILE * fd, rdot node, size_t indents)
{
  rdot enum_id = RDOT_lhs_TT (node);
  rdot enum_layout = RDOT_rhs_TT (node);

  size_t i;
  for (i = 0; i < indents; ++i)
    fprintf (fd, "    ");
  const char * id = RDOT_IDENTIFIER_POINTER (enum_id);
  fprintf (fd, "enum %s {\n", id);

  indents++;
  rdot next;
  for (next = enum_layout; next != NULL_DOT; next = RDOT_CHAIN (next))
    {
      for (i = 0; i < indents; ++i)
	fprintf (fd, "    ");
      const char *enumit = RDOT_IDENTIFIER_POINTER (next);
      fprintf (fd, "[%s],\n", enumit);
    }
  indents--;

  for (i = 0; i < indents; ++i)
    fprintf (fd, "    ");
  fprintf (fd, "}\n");
}

static
void dot_pass_dump_cond (FILE * fd, rdot node, size_t indents)
{
  size_t i;
  rdot ifb = RDOT_lhs_TT (node);
  rdot elb = RDOT_rhs_TT (node);

  gcc_assert (RDOT_TYPE (ifb) == D_STRUCT_IF);

  for (i = 0; i < indents; ++i)
    fprintf (fd, "    ");

  fprintf (fd, "if (");
  dot_pass_dump_expr (fd, RDOT_lhs_TT (ifb));
  fprintf (fd, ") {\n");
  
  rdot next;
  for (next = RDOT_rhs_TT (ifb) ; next != NULL_DOT; next = RDOT_CHAIN (next))
    {
      dot_pass_dump_node (fd, next, indents + 1);
      fprintf (fd, "\n");
    }

  for (i = 0; i < indents; ++i)
    fprintf (fd, "    ");
  fprintf (fd, "}");

  if (elb != NULL_DOT)
    {
      fprintf (fd, " else {\n");
      for (next = RDOT_lhs_TT (elb); next != NULL_DOT; next = RDOT_CHAIN (next))
	{
	  dot_pass_dump_node (fd, next, indents + 1);
	  fprintf (fd, "\n");
	} 
      for (i = 0; i < indents; ++i)
	fprintf (fd, "    ");
      fprintf (fd, "}\n");
    }
}

static
void dot_pass_dump_while (FILE * fd, rdot node, size_t indents)
{
  size_t i;
  rdot expr = RDOT_lhs_TT (node);
  rdot suite = RDOT_rhs_TT (node);

  for (i = 0; i < indents; ++i)
    fprintf (fd, "    ");
  fprintf (fd, "while (");
  dot_pass_dump_expr (fd, expr);
  fprintf (fd, ") {\n");

  rdot next;
  for (next = suite; next != NULL_DOT; next = RDOT_CHAIN (next))
    {
      dot_pass_dump_node (fd, next, indents + 1);
      fprintf (fd, "\n");
    }

  for (i = 0; i < indents; ++i)
    fprintf (fd, "    ");
  fprintf (fd, "}");
}

static
void dot_pass_dump_impl (FILE * fd, rdot node, size_t indents)
{
  const char * implid = RDOT_IDENTIFIER_POINTER (RDOT_lhs_TT (node));
  fprintf (fd, "impl %s {\n", implid);

  rdot next;
  for (next = RDOT_rhs_TT (node); next != NULL_DOT; next = RDOT_CHAIN (next))
    {
      dot_pass_dump_node (fd, next, indents + 1);
      fprintf (fd, "\n");
    }

  fprintf (fd, "}\n");
}

static
void dot_pass_dump_node (FILE * fd, rdot node, size_t indents)
{
  if (RDOT_T_FIELD (node) ==  D_D_EXPR)
    {
      size_t i;
      for (i = 0; i < indents; ++i)
        fprintf (fd, "    ");
      dot_pass_dump_expr (fd, node);
      fprintf (fd, ";");
    }
  else
    {
      switch (RDOT_TYPE (node))
        {
        case D_PRIMITIVE:
          dot_pass_dump_expr (fd, node);
          break;

	case D_STRUCT_IMPL:
	  dot_pass_dump_impl (fd, node, indents);
	  break;

        case D_STRUCT_METHOD:
          dot_pass_dump_method (fd, node, indents);
          break;
        
	case D_STRUCT_TYPE:
	  dot_pass_dump_struct (fd, node, indents);
	  break;
     
	case D_STRUCT_ENUM:
	  dot_pass_dump_enum (fd, node, indents);
	  break;

	case D_STRUCT_IF:
	  dot_pass_dump_cond (fd, node, indents);
	  break;

	case D_STRUCT_WHILE:
	  dot_pass_dump_while (fd, node, indents);
	  break;

        default:
	  error ("unhandled node [%s]\n", RDOT_OPCODE_STR (node));
          break;
        }
    }
}

vec<rdot,va_gc> * dot_pass_PrettyPrint (vec<rdot,va_gc> * decls)
{
  if (GRS_OPT_dump_dot)
    {
      size_t bsize = 128;
      char * outfile =  (char *) alloca (bsize);
      gcc_assert (outfile);
      memset (outfile, 0, bsize);

      strncpy (outfile, GRS_current_infile, strlen (GRS_current_infile) - 2);
      if (first == true)
        {
          strncat (outfile, RDOT_PREFIX_PRE, sizeof (RDOT_PREFIX_PRE));
          first = false;
        }
      else
        {
          strncat (outfile, RDOT_PREFIX_POST, sizeof (RDOT_PREFIX_POST));
          _no_infer = true;
        }

      FILE * fd = fopen (outfile, "w");
      if (!fd)
        {
          error ("Unable to open %s for write\n", outfile);
          goto exit;
        }

      rdot idtx = NULL_DOT;
      size_t i;
      for (i = 0; decls->iterate (i, &idtx); ++i)
        {
          dot_pass_dump_node (fd, idtx, 0);
          fprintf (fd, "\n");
        }

      fclose (fd);
    }
 exit:
  return decls;
}
