/* Gimple Represented as Polyhedra.
   Copyright (C) 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <sebastian.pop@inria.fr>.

This file is part of GCC.

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
<http://www.gnu.org/licenses/>.  */

/* This pass converts GIMPLE to GRAPHITE, performs some loop
   transformations and then converts the resulting representation back
   to GIMPLE.  

   An early description of this pass can be found in the GCC Summit'06
   paper "GRAPHITE: Polyhedral Analyses and Optimizations for GCC".
   The wiki page http://gcc.gnu.org/wiki/Graphite contains pointers to
   the related work.  

   One important document to read is CLooG's internal manual:
   http://repo.or.cz/w/cloog-ppl.git?a=blob_plain;f=doc/cloog.texi;hb=HEAD
   that describes the data structure of loops used in this file, and
   the functions that are used for transforming the code.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "toplev.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "domwalk.h"
#include "value-prof.h"
#include "pointer-set.h"
#include "gimple.h"
#include "sese.h"

#ifdef HAVE_cloog
#include "cloog/cloog.h"
#include "ppl_c.h"
#include "graphite-ppl.h"
#include "graphite.h"
#include "graphite-poly.h"
#include "graphite-scop-detection.h"
#include "graphite-data-ref.h"
#include "graphite-clast-to-gimple.h"

/* Debug the list of old induction variables for this SCOP.  */

void
debug_oldivs (sese region)
{
  int i;
  name_tree oldiv;

  fprintf (stderr, "Old IVs:");

  for (i = 0; VEC_iterate (name_tree, SESE_OLDIVS (region), i, oldiv); i++)
    {
      fprintf (stderr, "(");
      print_generic_expr (stderr, oldiv->t, 0);
      fprintf (stderr, ", %s, %d)\n", oldiv->name, oldiv->loop->num);
    }
  fprintf (stderr, "\n");
}

/* Debug the loops around basic block GB.  */

void
debug_loop_vec (poly_bb_p pbb)
{
  int i;
  loop_p loop;

  fprintf (stderr, "Loop Vec:");

  for (i = 0; VEC_iterate (loop_p, PBB_LOOPS (pbb), i, loop); i++)
    fprintf (stderr, "%d: %d, ", i, loop ? loop->num : -1);

  fprintf (stderr, "\n");
}

/* Pretty print all SCoPs in DOT format and mark them with different colors.
   If there are not enough colors, paint later SCoPs gray.
   Special nodes:
   - "*" after the node number: entry of a SCoP,
   - "#" after the node number: exit of a SCoP,
   - "()" entry or exit not part of SCoP.  */

static void
dot_all_scops_1 (FILE *file, VEC (scop_p, heap) *scops)
{
  basic_block bb;
  edge e;
  edge_iterator ei;
  scop_p scop;
  const char* color;
  int i;

  /* Disable debugging while printing graph.  */
  int tmp_dump_flags = dump_flags;
  dump_flags = 0;

  fprintf (file, "digraph all {\n");

  FOR_ALL_BB (bb)
    {
      int part_of_scop = false;

      /* Use HTML for every bb label.  So we are able to print bbs
         which are part of two different SCoPs, with two different
         background colors.  */
      fprintf (file, "%d [label=<\n  <TABLE BORDER=\"0\" CELLBORDER=\"1\" ",
                     bb->index);
      fprintf (file, "CELLSPACING=\"0\">\n");

      /* Select color for SCoP.  */
      for (i = 0; VEC_iterate (scop_p, scops, i, scop); i++)
	if (bb_in_sese_p (bb, SCOP_REGION (scop))
	    || (SCOP_EXIT (scop) == bb)
	    || (SCOP_ENTRY (scop) == bb))
	  {
	    switch (i % 17)
	      {
	      case 0: /* red */
		color = "#e41a1c";
		break;
	      case 1: /* blue */
		color = "#377eb8";
		break;
	      case 2: /* green */
		color = "#4daf4a";
		break;
	      case 3: /* purple */
		color = "#984ea3";
		break;
	      case 4: /* orange */
		color = "#ff7f00";
		break;
	      case 5: /* yellow */
		color = "#ffff33";
		break;
	      case 6: /* brown */
		color = "#a65628";
		break;
	      case 7: /* rose */
		color = "#f781bf";
		break;
	      case 8:
		color = "#8dd3c7";
		break;
	      case 9:
		color = "#ffffb3";
		break;
	      case 10:
		color = "#bebada";
		break;
	      case 11:
		color = "#fb8072";
		break;
	      case 12:
		color = "#80b1d3";
		break;
	      case 13:
		color = "#fdb462";
		break;
	      case 14:
		color = "#b3de69";
		break;
	      case 15:
		color = "#fccde5";
		break;
	      case 16:
		color = "#bc80bd";
		break;
	      default: /* gray */
		color = "#999999";
	      }

	    fprintf (file, "    <TR><TD WIDTH=\"50\" BGCOLOR=\"%s\">", color);
        
	    if (!bb_in_sese_p (bb, SCOP_REGION (scop)))
	      fprintf (file, " ("); 

	    if (bb == SCOP_ENTRY (scop)
		&& bb == SCOP_EXIT (scop))
	      fprintf (file, " %d*# ", bb->index);
	    else if (bb == SCOP_ENTRY (scop))
	      fprintf (file, " %d* ", bb->index);
	    else if (bb == SCOP_EXIT (scop))
	      fprintf (file, " %d# ", bb->index);
	    else
	      fprintf (file, " %d ", bb->index);

	    if (!bb_in_sese_p (bb, SCOP_REGION (scop)))
	      fprintf (file, ")");

	    fprintf (file, "</TD></TR>\n");
	    part_of_scop  = true;
	  }

      if (!part_of_scop)
        {
          fprintf (file, "    <TR><TD WIDTH=\"50\" BGCOLOR=\"#ffffff\">");
          fprintf (file, " %d </TD></TR>\n", bb->index);
        }

      fprintf (file, "  </TABLE>>, shape=box, style=\"setlinewidth(0)\"]\n");
    }

  FOR_ALL_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	      fprintf (file, "%d -> %d;\n", bb->index, e->dest->index);
    }

  fputs ("}\n\n", file);

  /* Enable debugging again.  */
  dump_flags = tmp_dump_flags;
}

/* Display all SCoPs using dotty.  */

void
dot_all_scops (VEC (scop_p, heap) *scops)
{
  /* When debugging, enable the following code.  This cannot be used
     in production compilers because it calls "system".  */
#if 1
  FILE *stream = fopen ("/tmp/allscops.dot", "w");
  gcc_assert (stream);

  dot_all_scops_1 (stream, scops);
  fclose (stream);

  system ("dotty /tmp/allscops.dot");
#else
  dot_all_scops_1 (stderr, scops);
#endif
}

/* Returns true when BB will be represented in graphite.  Return false
   for the basic blocks that contain code eliminated in the code
   generation pass: i.e. induction variables and exit conditions.  */

static bool
graphite_stmt_p (sese region, basic_block bb,
		 VEC (data_reference_p, heap) *drs)
{
  gimple_stmt_iterator gsi;
  loop_p loop = bb->loop_father;

  if (VEC_length (data_reference_p, drs) > 0)
    return true;

  for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
    {
      gimple stmt = gsi_stmt (gsi);

      switch (gimple_code (stmt))
        {
          /* Control flow expressions can be ignored, as they are
             represented in the iteration domains and will be
             regenerated by graphite.  */
	case GIMPLE_COND:
	case GIMPLE_GOTO:
	case GIMPLE_SWITCH:
	  break;

	case GIMPLE_ASSIGN:
	  {
	    tree var = gimple_assign_lhs (stmt);
	    var = analyze_scalar_evolution (loop, var);
	    var = instantiate_scev (block_before_sese (region), loop, var);

	    if (chrec_contains_undetermined (var))
	      return true;

	    break;
	  }

	default:
	  return true;
        }
    }

  return false;
}

/* Store the GRAPHITE representation of BB.  */

static gimple_bb_p
new_gimple_bb (basic_block bb, VEC (data_reference_p, heap) *drs)
{
  struct gimple_bb *gbb;

  gbb = XNEW (struct gimple_bb);
  bb->aux = gbb;
  GBB_BB (gbb) = bb;
  GBB_DATA_REFS (gbb) = drs;
  GBB_CONDITIONS (gbb) = NULL;
  GBB_CONDITION_CASES (gbb) = NULL;
  GBB_CLOOG_IV_TYPES (gbb) = NULL;
 
  return gbb;
}

/* Frees GBB.  */

static void
free_gimple_bb (struct gimple_bb *gbb)
{
  if (GBB_CLOOG_IV_TYPES (gbb))
    htab_delete (GBB_CLOOG_IV_TYPES (gbb));

  /* FIXME: free_data_refs is disabled for the moment, but should be
     enabled.

     free_data_refs (GBB_DATA_REFS (gbb)); */

  VEC_free (gimple, heap, GBB_CONDITIONS (gbb));
  VEC_free (gimple, heap, GBB_CONDITION_CASES (gbb));
  GBB_BB (gbb)->aux = 0;
  XDELETE (gbb);
}

/* Deletes all gimple bbs in SCOP.  */

static void
remove_gbbs_in_scop (scop_p scop)
{
  int i;
  poly_bb_p pbb;

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    free_gimple_bb (PBB_BLACK_BOX (pbb));
}

/* Deletes all scops in SCOPS.  */

void
free_scops (VEC (scop_p, heap) *scops)
{
  int i;
  scop_p scop;

  for (i = 0; VEC_iterate (scop_p, scops, i, scop); i++)
    {
      remove_gbbs_in_scop (scop);
      free_sese (SCOP_REGION (scop));
      free_scop (scop);
    }

  VEC_free (scop_p, heap, scops);
}

/* Generates a polyhedral black box only if the bb contains interesting
   information.  */

static void
try_generate_gimple_bb (scop_p scop, basic_block bb)
{
  sese region = SCOP_REGION (scop); 
  VEC (data_reference_p, heap) *drs = VEC_alloc (data_reference_p, heap, 5);
  struct loop *nest = outermost_loop_in_sese (region, bb);
  gimple_stmt_iterator gsi;

  for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
    find_data_references_in_stmt (nest, gsi_stmt (gsi), &drs);

  if (!graphite_stmt_p (region, bb, drs))
    {
      free_data_refs (drs);
      return;
    }

  new_poly_bb (scop, new_gimple_bb (bb, drs));
}

/* Gather the basic blocks belonging to the SCOP.  */

void
build_scop_bbs (scop_p scop)
{
  basic_block *stack = XNEWVEC (basic_block, n_basic_blocks + 1);
  sbitmap visited = sbitmap_alloc (last_basic_block);
  int sp = 0;

  sbitmap_zero (visited);
  stack[sp++] = SCOP_ENTRY (scop);

  while (sp)
    {
      basic_block bb = stack[--sp];
      int depth = loop_depth (bb->loop_father);
      int num = bb->loop_father->num;
      edge_iterator ei;
      edge e;

      /* Scop's exit is not in the scop.  Exclude also bbs, which are
	 dominated by the SCoP exit.  These are e.g. loop latches.  */
      if (TEST_BIT (visited, bb->index)
	  || dominated_by_p (CDI_DOMINATORS, bb, SCOP_EXIT (scop))
	  /* Every block in the scop is dominated by scop's entry.  */
	  || !dominated_by_p (CDI_DOMINATORS, bb, SCOP_ENTRY (scop)))
	continue;

      try_generate_gimple_bb (scop, bb);

      SET_BIT (visited, bb->index);

      /* First push the blocks that have to be processed last.  Note
	 that this means that the order in which the code is organized
	 below is important: do not reorder the following code.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (! TEST_BIT (visited, e->dest->index)
	    && (int) loop_depth (e->dest->loop_father) < depth)
	  stack[sp++] = e->dest;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (! TEST_BIT (visited, e->dest->index)
	    && (int) loop_depth (e->dest->loop_father) == depth
	    && e->dest->loop_father->num != num)
	  stack[sp++] = e->dest;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (! TEST_BIT (visited, e->dest->index)
	    && (int) loop_depth (e->dest->loop_father) == depth
	    && e->dest->loop_father->num == num
	    && EDGE_COUNT (e->dest->preds) > 1)
	  stack[sp++] = e->dest;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (! TEST_BIT (visited, e->dest->index)
	    && (int) loop_depth (e->dest->loop_father) == depth
	    && e->dest->loop_father->num == num
	    && EDGE_COUNT (e->dest->preds) == 1)
	  stack[sp++] = e->dest;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (! TEST_BIT (visited, e->dest->index)
	    && (int) loop_depth (e->dest->loop_father) > depth)
	  stack[sp++] = e->dest;
    }

  free (stack);
  sbitmap_free (visited);
}

/* Returns the dimensionality of an enclosing loop iteration domain
   with respect to enclosing SCoP for a given data reference REF.  The
   returned dimensionality is homogeneous (depth of loop nest + number
   of SCoP parameters + const).  */

int
ref_nb_loops (data_reference_p ref, sese region)
{
  loop_p loop = loop_containing_stmt (DR_STMT (ref));

  return nb_loops_around_loop_in_sese (loop, region)
    + sese_nb_params (region) + 2;
}

/* Returns the number of loops that are identical at the beginning of
   the vectors A and B.  */

static int
compare_prefix_loops (VEC (loop_p, heap) *a, VEC (loop_p, heap) *b)
{
  int i;
  loop_p ea;
  int lb;

  if (!a || !b)
    return 0;

  lb = VEC_length (loop_p, b);

  for (i = 0; VEC_iterate (loop_p, a, i, ea); i++)
    if (i >= lb
	|| ea != VEC_index (loop_p, b, i))
      return i;

  return 0;
}

/* Build for BB the static schedule.

   The STATIC_SCHEDULE is defined like this:

   A
   for (i: ...)
     {
       for (j: ...)
         {
           B
           C 
         }

       for (k: ...)
         {
           D
           E 
         }
     }
   F

   Static schedules for A to F:

     DEPTH
     0 1 2 
   A 0
   B 1 0 0
   C 1 0 1
   D 1 1 0
   E 1 1 1 
   F 2
*/

static void
build_scop_canonical_schedules (scop_p scop)
{
  int i;
  poly_bb_p pbb;
  ppl_Linear_Expression_t static_schedule;
  VEC (loop_p, heap) *loops_previous = NULL;
  ppl_Coefficient_t c;
  Value v;

  value_init (v);
  ppl_new_Coefficient (&c);
  ppl_new_Linear_Expression (&static_schedule);

  /* We have to start schedules at 0 on the first component and
     because we cannot compare_prefix_loops against a previous loop,
     prefix will be equal to zero, and that index will be
     incremented before copying.  */
  value_set_si (v, -1);
  ppl_assign_Coefficient_from_mpz_t (c, v);
  ppl_Linear_Expression_add_to_coefficient (static_schedule, 0, c);

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    {
      ppl_Linear_Expression_t common;
      int prefix = compare_prefix_loops (loops_previous, PBB_LOOPS (pbb));
      int nb = pbb_nb_loops (pbb);

      loops_previous = PBB_LOOPS (pbb);
      ppl_new_Linear_Expression_with_dimension (&common, prefix + 1);
      ppl_assign_Linear_Expression_from_Linear_Expression (common, static_schedule);

      value_set_si (v, 1);
      ppl_assign_Coefficient_from_mpz_t (c, v);
      ppl_Linear_Expression_add_to_coefficient (common, prefix, c);
      ppl_assign_Linear_Expression_from_Linear_Expression (static_schedule, common);

      ppl_new_Linear_Expression_with_dimension (&PBB_STATIC_SCHEDULE (pbb),
						nb + 1);
      ppl_assign_Linear_Expression_from_Linear_Expression
	(PBB_STATIC_SCHEDULE (pbb), common);
      ppl_delete_Linear_Expression (common);
    }

  value_clear (v);
  ppl_delete_Coefficient (c);
  ppl_delete_Linear_Expression (static_schedule);
}

/* Build the LOOPS vector for all bbs in SCOP.  */

static void
build_bb_loops (scop_p scop)
{
  poly_bb_p pbb;
  int i;

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    {
      loop_p loop;
      int depth; 

      depth = nb_loops_around_pbb (pbb) - 1; 

      PBB_LOOPS (pbb) = VEC_alloc (loop_p, heap, 3);
      VEC_safe_grow_cleared (loop_p, heap, PBB_LOOPS (pbb), depth + 1);

      loop = GBB_BB (PBB_BLACK_BOX (pbb))->loop_father;  

      while (sese_contains_loop (SCOP_REGION(scop), loop))
        {
          VEC_replace (loop_p, PBB_LOOPS (pbb), depth, loop);
          loop = loop_outer (loop);
          depth--;
        }
    }
}

/* When SUBTRACT is true subtract or when SUBTRACT is false add the
   value K to the dimension D of the linear expression EXPR.  */

static void
add_value_to_dim (ppl_dimension_type d, ppl_Linear_Expression_t expr, 
		  Value k, bool subtract)
{
  Value val;
  ppl_Coefficient_t coef;

  ppl_new_Coefficient (&coef);
  ppl_Linear_Expression_coefficient (expr, d, coef);
  value_init (val);
  ppl_Coefficient_to_mpz_t (coef, val);

  if (subtract)
    value_subtract (val, val, k);
  else
    value_addto (val, val, k);

  ppl_assign_Coefficient_from_mpz_t (coef, val);
  ppl_Linear_Expression_add_to_coefficient (expr, d, coef);
  value_clear (val);
  ppl_delete_Coefficient (coef);
}

/* In the context of scop S, scan E, the right hand side of a scalar
   evolution function in loop VAR, and translate it to a linear
   expression EXPR.  When SUBTRACT is true the linear expression
   corresponding to E is subtracted from the linear expression EXPR.  */

static void
scan_tree_for_params_right_scev (sese s, tree e, int var,
				 ppl_Linear_Expression_t expr,
				 bool subtract)
{
  if (expr)
    {
      loop_p loop = get_loop (var);
      ppl_dimension_type l = sese_loop_depth (s, loop);
      Value val;

      gcc_assert (TREE_CODE (e) == INTEGER_CST);

      value_init (val);
      value_set_si (val, int_cst_value (e));
      add_value_to_dim (l, expr, val, subtract);
      value_clear (val);
    }
}

/* Scan the integer constant CST, and if SUBTRACT is false add it or
   if SUBTRACT is true subtract it from the inhomogeneous part of the
   linear expression EXPR.  */

static void
scan_tree_for_params_int (tree cst, ppl_Linear_Expression_t expr, bool subtract)
{
  Value val;
  ppl_Coefficient_t coef;
  int v = int_cst_value (cst);

  if (v < 0)
    {
      v = -v;
      subtract = subtract ? false : true;
    }

  value_init (val);
  value_set_si (val, 0);
  if (subtract)
    value_sub_int (val, val, v);
  else
    value_add_int (val, val, v);

  ppl_new_Coefficient (&coef);
  ppl_assign_Coefficient_from_mpz_t (coef, val);
  ppl_Linear_Expression_add_to_inhomogeneous (expr, coef);
  value_clear (val);
  ppl_delete_Coefficient (coef);
}

/* In the context of sese S, scan the expression E and translate it to
   a linear expression C.  When parsing a symbolic multiplication, K
   represents the constant multiplier of an expression containing
   parameters.  When SUBTRACT is true the linear expression
   corresponding to E is subtracted from the linear expression C.  */

static void
scan_tree_for_params (sese s, tree e, ppl_Linear_Expression_t c,
		      Value k, bool subtract)
{
  if (e == chrec_dont_know)
    return;

  switch (TREE_CODE (e))
    {
    case POLYNOMIAL_CHREC:
      scan_tree_for_params_right_scev (s, CHREC_RIGHT (e), CHREC_VARIABLE (e),
				       c, subtract);
      scan_tree_for_params (s, CHREC_LEFT (e), c, k, subtract);
      break;

    case MULT_EXPR:
      if (chrec_contains_symbols (TREE_OPERAND (e, 0)))
	{
	  if (c)
	    {
	      Value val;
	      gcc_assert (host_integerp (TREE_OPERAND (e, 1), 0));
	      value_init (val);
	      value_set_si (val, int_cst_value (TREE_OPERAND (e, 1)));
	      value_multiply (k, k, val);
	      value_clear (val);
	    }
	  scan_tree_for_params (s, TREE_OPERAND (e, 0), c, k, subtract);
	}
      else
	{
	  if (c)
	    {
	      Value val;
	      gcc_assert (host_integerp (TREE_OPERAND (e, 0), 0));
	      value_init (val);
	      value_set_si (val, int_cst_value (TREE_OPERAND (e, 0)));
	      value_multiply (k, k, val);
	      value_clear (val);
	    }
	  scan_tree_for_params (s, TREE_OPERAND (e, 1), c, k, subtract);
	}
      break;

    case PLUS_EXPR:
    case POINTER_PLUS_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, k, subtract);
      scan_tree_for_params (s, TREE_OPERAND (e, 1), c, k, subtract);
      break;

    case MINUS_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, k, subtract);
      scan_tree_for_params (s, TREE_OPERAND (e, 1), c, k, !subtract);
      break;

    case NEGATE_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, k, !subtract);
      break;

    case SSA_NAME:
      {
	ppl_dimension_type p = parameter_index_in_region (e, s);
	if (c)
	  {
	    ppl_dimension_type dim;
	    ppl_Linear_Expression_space_dimension (c, &dim);
	    p += dim - sese_nb_params (s);
	    add_value_to_dim (p, c, k, subtract);
	  }
	break;
      }

    case INTEGER_CST:
      if (c)
	scan_tree_for_params_int (e, c, subtract);
      break;

    CASE_CONVERT:
    case NON_LVALUE_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, k, subtract);
      break;

    default:
      gcc_unreachable ();
      break;
    }
}

/* Data structure for idx_record_params.  */

struct irp_data
{
  struct loop *loop;
  sese sese;
};

/* For a data reference with an ARRAY_REF as its BASE, record the
   parameters occurring in IDX.  DTA is passed in as complementary
   information, and is used by the automatic walker function.  This
   function is a callback for for_each_index.  */

static bool
idx_record_params (tree base, tree *idx, void *dta)
{
  struct irp_data *data = (struct irp_data *) dta;

  if (TREE_CODE (base) != ARRAY_REF)
    return true;

  if (TREE_CODE (*idx) == SSA_NAME)
    {
      tree scev;
      sese sese = data->sese;
      struct loop *loop = data->loop;
      Value one;

      scev = analyze_scalar_evolution (loop, *idx);
      scev = instantiate_scev (block_before_sese (sese), loop, scev);

      value_init (one);
      value_set_si (one, 1);
      scan_tree_for_params (sese, scev, NULL, one, false);
      value_clear (one);
    }

  return true;
}

/* Find parameters with respect to SESE in BB. We are looking in memory
   access functions, conditions and loop bounds.  */

static void
find_params_in_bb (sese sese, gimple_bb_p gbb)
{
  int i;
  data_reference_p dr;
  gimple stmt;
  loop_p father = GBB_BB (gbb)->loop_father;

  for (i = 0; VEC_iterate (data_reference_p, GBB_DATA_REFS (gbb), i, dr); i++)
    {
      struct irp_data irp;

      irp.loop = father;
      irp.sese = sese;
      for_each_index (&dr->ref, idx_record_params, &irp);
    }

  /* Find parameters in conditional statements.  */ 
  for (i = 0; VEC_iterate (gimple, GBB_CONDITIONS (gbb), i, stmt); i++)
    {
      Value one;
      loop_p loop = father;

      tree lhs, rhs;

      lhs = gimple_cond_lhs (stmt);
      lhs = analyze_scalar_evolution (loop, lhs);
      lhs = instantiate_scev (block_before_sese (sese), loop, lhs);

      rhs = gimple_cond_rhs (stmt);
      rhs = analyze_scalar_evolution (loop, rhs);
      rhs = instantiate_scev (block_before_sese (sese), loop, rhs);

      value_init (one);
      value_set_si (one, 1);
      scan_tree_for_params (sese, lhs, NULL, one, false);
      scan_tree_for_params (sese, rhs, NULL, one, false);
      value_clear (one);
    }
}

/* Record the parameters used in the SCOP.  A variable is a parameter
   in a scop if it does not vary during the execution of that scop.  */

static void
find_scop_parameters (scop_p scop)
{
  poly_bb_p pbb;
  unsigned i;
  sese region = SCOP_REGION (scop);
  struct loop *loop;
  Value one;

  value_init (one);
  value_set_si (one, 1);

  /* Find the parameters used in the loop bounds.  */
  for (i = 0; VEC_iterate (loop_p, SCOP_LOOP_NEST (scop), i, loop); i++)
    {
      tree nb_iters = number_of_latch_executions (loop);

      if (!chrec_contains_symbols (nb_iters))
	continue;

      nb_iters = analyze_scalar_evolution (loop, nb_iters);
      nb_iters = instantiate_scev (block_before_sese (SCOP_REGION (scop)), loop,
						      nb_iters);
      scan_tree_for_params (region, nb_iters, NULL, one, false);
    }

  value_clear (one);

  /* Find the parameters used in data accesses.  */
  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    find_params_in_bb (region, PBB_BLACK_BOX (pbb));

  SESE_ADD_PARAMS (region) = false;
}

/* Returns a gimple_bb from BB.  */

static inline gimple_bb_p
gbb_from_bb (basic_block bb)
{
  return (gimple_bb_p) bb->aux;
}

/* Builds the constraint polyhedra for LOOP in SCOP.  OUTER_PH gives
   the constraints for the surrounding loops.  */

static void
build_loop_iteration_domains (scop_p scop, struct loop *loop,
                              ppl_Polyhedron_t outer_ph, int nb)

{
  int i;
  poly_bb_p pbb;
  Value one, minus_one, val;
  ppl_Polyhedron_t ph;
  ppl_Linear_Expression_t lb_expr, ub_expr;
  ppl_Constraint_t lb, ub;
  ppl_Coefficient_t coef;
  ppl_const_Constraint_System_t pcs;
  tree nb_iters = number_of_latch_executions (loop);
  ppl_dimension_type dim = nb + 1 + scop_nb_params (scop);
  ppl_dimension_type *map;

  value_init (one);
  value_init (minus_one);
  value_init (val);
  value_set_si (one, 1);
  value_set_si (minus_one, -1);

  ppl_new_Linear_Expression_with_dimension (&lb_expr, dim);
  ppl_new_Linear_Expression_with_dimension (&ub_expr, dim);
  ppl_new_NNC_Polyhedron_from_space_dimension (&ph, dim, 0);

  /* 0 <= loop_i */
  ppl_new_Coefficient_from_mpz_t (&coef, one);
  ppl_Linear_Expression_add_to_coefficient (lb_expr, nb, coef);
  ppl_new_Constraint (&lb, lb_expr, PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL);
  ppl_delete_Linear_Expression (lb_expr);

  /* loop_i <= nb_iters */
  ppl_assign_Coefficient_from_mpz_t (coef, minus_one);
  ppl_Linear_Expression_add_to_coefficient (ub_expr, nb, coef);

  if (TREE_CODE (nb_iters) == INTEGER_CST)
    {
      value_set_si (val, int_cst_value (nb_iters));
      ppl_assign_Coefficient_from_mpz_t (coef, val);
      ppl_Linear_Expression_add_to_inhomogeneous (ub_expr, coef);
      ppl_new_Constraint (&ub, ub_expr, PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL);
    }
  else if (!chrec_contains_undetermined (nb_iters))
    {
      nb_iters = analyze_scalar_evolution (loop, nb_iters);
      nb_iters = instantiate_scev (block_before_sese (SCOP_REGION (scop)), loop,
						      nb_iters);
      scan_tree_for_params (SCOP_REGION (scop), nb_iters, ub_expr, one, false);
      ppl_new_Constraint (&ub, ub_expr, PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL);
    }
  else
    gcc_unreachable ();

  ppl_delete_Linear_Expression (ub_expr);
  ppl_Polyhedron_get_constraints (outer_ph, &pcs);
  ppl_Polyhedron_add_constraints (ph, pcs);

  map = (ppl_dimension_type *) XNEWVEC (ppl_dimension_type, dim);
  for (i = 0; i < (int) nb; i++)
    map[i] = i;
  for (i = (int) nb; i < (int) dim - 1; i++)
    map[i] = i + 1;
  map[dim - 1] = nb;

  ppl_Polyhedron_map_space_dimensions (ph, map, dim);
  free (map);
  ppl_Polyhedron_add_constraint (ph, lb);
  ppl_Polyhedron_add_constraint (ph, ub);
  ppl_delete_Constraint (lb);
  ppl_delete_Constraint (ub);

  if (loop->inner && loop_in_sese_p (loop->inner, SCOP_REGION (scop)))
    build_loop_iteration_domains (scop, loop->inner, ph, nb + 1);

  if (nb != 0
      && loop->next
      && loop_in_sese_p (loop->next, SCOP_REGION (scop)))
    build_loop_iteration_domains (scop, loop->next, outer_ph, nb);

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    if (gbb_loop (PBB_BLACK_BOX (pbb)) == loop)
      {
	ppl_delete_Polyhedron (PBB_DOMAIN (pbb));
	ppl_new_NNC_Polyhedron_from_NNC_Polyhedron (&PBB_DOMAIN (pbb), ph);
      }

  ppl_delete_Coefficient (coef);
  ppl_delete_Polyhedron (ph);
  value_clear (one);
  value_clear (minus_one);
  value_clear (val);
}

/* Add conditions to the domain of GB.  */

static void
add_conditions_to_domain (poly_bb_p pbb)
{
  unsigned int i;
  gimple stmt;
  gimple_bb_p gbb = PBB_BLACK_BOX (pbb);
  VEC (gimple, heap) *conditions = GBB_CONDITIONS (gbb);
  scop_p scop = PBB_SCOP (pbb);
  basic_block before_scop = block_before_sese (SCOP_REGION (scop));

  if (VEC_empty (gimple, conditions))
    return;

  /* Add the conditions to the new enlarged domain.  */
  for (i = 0; VEC_iterate (gimple, conditions, i, stmt); i++)
    {
      switch (gimple_code (stmt))
        {
        case GIMPLE_COND:
          {
            Value one;
            tree left, right;
            loop_p loop = GBB_BB (gbb)->loop_father;
	    ppl_Linear_Expression_t expr;
	    ppl_Constraint_t cstr;
	    enum ppl_enum_Constraint_Type type = 0;
            enum tree_code code = gimple_cond_code (stmt);
	    ppl_dimension_type dim;

            /* The conditions for ELSE-branches are inverted.  */
            if (VEC_index (gimple, gbb->condition_cases, i) == NULL)
              code = invert_tree_comparison (code, false);

            switch (code)
              {
              case LT_EXPR:
		type = PPL_CONSTRAINT_TYPE_LESS_THAN;
                break;

              case GT_EXPR:
		type = PPL_CONSTRAINT_TYPE_GREATER_THAN;
                break;

              case LE_EXPR:
		type = PPL_CONSTRAINT_TYPE_LESS_OR_EQUAL;
                break;

              case GE_EXPR:
		type = PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL;
                break;

              default:
                /* NE and EQ statements are not supported right now. */
                gcc_unreachable ();
                break;
              }

	    value_init (one);
	    value_set_si (one, 1);
	    dim = pbb_nb_loops (pbb) + scop_nb_params (scop);
	    ppl_new_Linear_Expression_with_dimension (&expr, dim);

	    left = gimple_cond_lhs (stmt);
	    left = analyze_scalar_evolution (loop, left);
	    left = instantiate_scev (before_scop, loop, left);

	    right = gimple_cond_rhs (stmt);
	    right = analyze_scalar_evolution (loop, right);
	    right = instantiate_scev (before_scop, loop, right);

	    scan_tree_for_params (SCOP_REGION (scop), left, expr, one, true);
	    value_set_si (one, 1);
	    scan_tree_for_params (SCOP_REGION (scop), right, expr, one, false);

	    value_clear (one);
	    ppl_new_Constraint (&cstr, expr, type);
	    ppl_Polyhedron_add_constraint (PBB_DOMAIN (pbb), cstr);
	    ppl_delete_Constraint (cstr);
	    ppl_delete_Linear_Expression (expr);
            break;
          }
        case GIMPLE_SWITCH:
          /* Switch statements are not supported right know.  */
        default:
          gcc_unreachable ();
          break;
        }
    }
}

/* Returns true when PHI defines an induction variable in the loop
   containing the PHI node.  */

static bool
phi_node_is_iv (gimple phi)
{
  loop_p loop = gimple_bb (phi)->loop_father;
  tree scev = analyze_scalar_evolution (loop, gimple_phi_result (phi));

  return tree_contains_chrecs (scev, NULL);
}

/* Returns true when BB contains scalar phi nodes that are not an
   induction variable of a loop.  */

static bool
bb_contains_non_iv_scalar_phi_nodes (basic_block bb)
{
  gimple phi = NULL;
  gimple_stmt_iterator si;

  for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
    if (is_gimple_reg (gimple_phi_result (gsi_stmt (si))))
      {
	/* Store the unique scalar PHI node: at this point, loops
	   should be in cannonical form, so we expect to see at most
	   one scalar phi node in the loop header.  */
	if (phi || bb != bb->loop_father->header)
	  return true;

	phi = gsi_stmt (si);
      }

  if (!phi || phi_node_is_iv (phi))
    return false;

  return true;
}


/* Check if SCOP contains non scalar phi nodes.  */

static bool
scop_contains_non_iv_scalar_phi_nodes (scop_p scop)
{
  int i;
  poly_bb_p pbb;

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    if (bb_contains_non_iv_scalar_phi_nodes (GBB_BB (PBB_BLACK_BOX (pbb))))
      return true;

  return false;
}

/* Helper recursive function.  Record in CONDITIONS and CASES all
   conditions from 'if's and 'switch'es occurring in BB from REGION.  */

static void
build_sese_conditions_1 (VEC (gimple, heap) **conditions,
			 VEC (gimple, heap) **cases, basic_block bb,
			 sese region)
{
  int i, j;
  gimple_bb_p gbb;
  basic_block bb_child, bb_iter;
  VEC (basic_block, heap) *dom;
  gimple stmt;
  
  /* Make sure we are in the SCoP.  */
  if (!bb_in_sese_p (bb, region))
    return;

  gbb = gbb_from_bb (bb);
  if (gbb)
    {
      GBB_CONDITIONS (gbb) = VEC_copy (gimple, heap, *conditions);
      GBB_CONDITION_CASES (gbb) = VEC_copy (gimple, heap, *cases);
    }

  dom = get_dominated_by (CDI_DOMINATORS, bb);

  stmt = last_stmt (bb);
  if (stmt)
    {
      VEC (edge, gc) *edges;
      edge e;

      switch (gimple_code (stmt))
	{
	case GIMPLE_COND:
	  edges = bb->succs;
	  for (i = 0; VEC_iterate (edge, edges, i, e); i++)
	    if ((dominated_by_p (CDI_DOMINATORS, e->dest, bb))
		&& VEC_length (edge, e->dest->preds) == 1)
	      {
		/* Remove the scanned block from the dominator successors.  */
		for (j = 0; VEC_iterate (basic_block, dom, j, bb_iter); j++)
		  if (bb_iter == e->dest)
		    {
		      VEC_unordered_remove (basic_block, dom, j);
		      break;
		    }

		/* Recursively scan the then or else part.  */
		if (e->flags & EDGE_TRUE_VALUE)
		  VEC_safe_push (gimple, heap, *cases, stmt);
		else 
		  {
		    gcc_assert (e->flags & EDGE_FALSE_VALUE);
		    VEC_safe_push (gimple, heap, *cases, NULL);
		  }

		VEC_safe_push (gimple, heap, *conditions, stmt);
		build_sese_conditions_1 (conditions, cases, e->dest, region);
		VEC_pop (gimple, *conditions);
		VEC_pop (gimple, *cases);
	      }
	  break;

	case GIMPLE_SWITCH:
	  {
	    unsigned i;
	    gimple_stmt_iterator gsi_search_gimple_label;

	    for (i = 0; i < gimple_switch_num_labels (stmt); ++i)
	      {
		basic_block bb_iter;
		size_t k;
		size_t n_cases = VEC_length (gimple, *conditions);
		unsigned n = gimple_switch_num_labels (stmt);

		bb_child = label_to_block
		  (CASE_LABEL (gimple_switch_label (stmt, i)));

		for (k = 0; k < n; k++)
		  if (i != k
		      && label_to_block 
		      (CASE_LABEL (gimple_switch_label (stmt, k))) == bb_child)
		    break;

		/* Switches with multiple case values for the same
		   block are not handled.  */
		if (k != n
		    /* Switch cases with more than one predecessor are
		       not handled.  */
		    || VEC_length (edge, bb_child->preds) != 1)
		  gcc_unreachable ();

		/* Recursively scan the corresponding 'case' block.  */
		for (gsi_search_gimple_label = gsi_start_bb (bb_child);
		     !gsi_end_p (gsi_search_gimple_label);
		     gsi_next (&gsi_search_gimple_label))
		  {
		    gimple label = gsi_stmt (gsi_search_gimple_label);

		    if (gimple_code (label) == GIMPLE_LABEL)
		      {
			tree t = gimple_label_label (label);

			gcc_assert (t == gimple_switch_label (stmt, i));
			VEC_replace (gimple, *cases, n_cases, label);
			break;
		      }
		  }

		build_sese_conditions_1 (conditions, cases, bb_child, region);

		/* Remove the scanned block from the dominator successors.  */
		for (j = 0; VEC_iterate (basic_block, dom, j, bb_iter); j++)
		  if (bb_iter == bb_child)
		    {
		      VEC_unordered_remove (basic_block, dom, j);
		      break;
		    }
	      }

	    VEC_pop (gimple, *conditions);
	    VEC_pop (gimple, *cases);
	    break;
	  }

	default:
	  break;
      }
  }

  /* Scan all immediate dominated successors.  */
  for (i = 0; VEC_iterate (basic_block, dom, i, bb_child); i++)

  VEC_free (basic_block, heap, dom);
}

/* Record all conditions in REGION.  */

static void 
build_sese_conditions (sese region)
{
  VEC (gimple, heap) *conditions = NULL;
  VEC (gimple, heap) *cases = NULL;

  build_sese_conditions_1 (&conditions, &cases, SESE_ENTRY (region)->dest,
			     region);

  VEC_free (gimple, heap, conditions);
  VEC_free (gimple, heap, cases);
}

/* Traverses all the GBBs of the SCOP and add their constraints to the
   iteration domains.  */

static void
add_conditions_to_constraints (scop_p scop)
{
  int i;
  poly_bb_p pbb;

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    add_conditions_to_domain (pbb);
}

/* Build the iteration domains: the loops belonging to the current
   SCOP, and that vary for the execution of the current basic block.
   Returns false if there is no loop in SCOP.  */

static void 
build_scop_iteration_domain (scop_p scop)
{
  struct loop *loop;
  sese region = SCOP_REGION (scop);
  int i;

  /* Build cloog loop for all loops, that are in the uppermost loop layer of
     this SCoP.  */
  for (i = 0; VEC_iterate (loop_p, SESE_LOOP_NEST (region), i, loop); i++)
    if (!loop_in_sese_p (loop_outer (loop), region)) 
      {
	ppl_Polyhedron_t ph;
	ppl_new_NNC_Polyhedron_from_space_dimension (&ph, 0, 0);
	build_loop_iteration_domains (scop, loop, ph, 0);
	ppl_delete_Polyhedron (ph);
      }
}

/* Initializes an equation CY of the access matrix using the
   information for a subscript from AF, relatively to the loop
   indexes from LOOP_NEST and parameter indexes from PARAMS.  NDIM is
   the dimension of the array access, i.e. the number of
   subscripts.  Returns true when the operation succeeds.  */

static bool
build_access_matrix_with_af (tree af, lambda_vector cy,
			     sese region, int ndim)
{
  int param_col;

  switch (TREE_CODE (af))
    {
    case POLYNOMIAL_CHREC:
      {
        struct loop *outer_loop;
	tree left = CHREC_LEFT (af);
	tree right = CHREC_RIGHT (af);
	int var;

	if (TREE_CODE (right) != INTEGER_CST)
	  return false;

        outer_loop = get_loop (CHREC_VARIABLE (af));
        var = nb_loops_around_loop_in_sese (outer_loop, region);
	cy[var] = int_cst_value (right);

	switch (TREE_CODE (left))
	  {
	  case POLYNOMIAL_CHREC:
	    return build_access_matrix_with_af (left, cy, region, ndim);

	  case INTEGER_CST:
	    cy[ndim - 1] = int_cst_value (left);
	    return true;

	  default:
	    return build_access_matrix_with_af (left, cy, region, ndim);
	  }
      }

    case PLUS_EXPR:
      build_access_matrix_with_af (TREE_OPERAND (af, 0), cy, region, ndim);
      build_access_matrix_with_af (TREE_OPERAND (af, 1), cy, region, ndim);
      return true;
      
    case MINUS_EXPR:
      build_access_matrix_with_af (TREE_OPERAND (af, 0), cy, region, ndim);
      build_access_matrix_with_af (TREE_OPERAND (af, 1), cy, region, ndim);
      return true;

    case INTEGER_CST:
      cy[ndim - 1] = int_cst_value (af);
      return true;

    case SSA_NAME:
      param_col = parameter_index_in_region (af, region);
      cy [ndim - sese_nb_params (region) + param_col - 1] = 1; 
      return true;

    default:
      /* FIXME: access_fn can have parameters.  */
      return false;
    }
}

/* Initialize the access matrix in the data reference REF with respect
   to the loop nesting LOOP_NEST.  Return true when the operation
   succeeded.  */

static bool
build_access_matrix (data_reference_p ref, poly_bb_p pbb)
{
  int i, ndim = DR_NUM_DIMENSIONS (ref);
  struct access_matrix *am = GGC_NEW (struct access_matrix);
  sese region = SCOP_REGION (PBB_SCOP (pbb));

  AM_MATRIX (am) = VEC_alloc (lambda_vector, gc, ndim);

  for (i = 0; i < ndim; i++)
    {
      lambda_vector v = lambda_vector_new (ref_nb_loops (ref, region));
      tree af = DR_ACCESS_FN (ref, i);

      if (!build_access_matrix_with_af (af, v, region,
					ref_nb_loops (ref, region)))
	return false;

      VEC_quick_push (lambda_vector, AM_MATRIX (am), v);
    }

  DR_ACCESS_MATRIX (ref) = am;
  return true;
}

/* Build the access matrices for the data references in the SCOP.  */

static void
build_scop_data_accesses (scop_p scop)
{
  int i;
  poly_bb_p pbb;

  /* FIXME: Construction of access matrix is disabled until some
     pass, like the data dependence analysis, is using it.  */
  return;

  for (i = 0; VEC_iterate (poly_bb_p, SCOP_BBS (scop), i, pbb); i++)
    {
      int j;
      data_reference_p dr;

      /* Construct the access matrix for each data ref, with respect to
	 the loop nest of the current BB in the considered SCOP.  */
      for (j = 0;
	   VEC_iterate (data_reference_p,
			GBB_DATA_REFS (PBB_BLACK_BOX (pbb)), j, dr); j++)
	{
	  bool res = build_access_matrix (dr, pbb);

	  /* FIXME: At this point the DRs should always have an affine
	     form.  For the moment this fails as build_access_matrix
	     does not build matrices with parameters.  */
	  gcc_assert (res);
	}
    }
}

/* Perform a set of linear transforms on the loops of the current
   function.  */

void
graphite_transform_loops (void)
{
  int i;
  scop_p scop;
  bool transform_done = false;
  VEC (scop_p, heap) *scops = NULL;

  if (number_of_loops () <= 1)
    return;

  recompute_all_dominators ();

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Graphite loop transformations \n");

  initialize_original_copy_tables ();
  cloog_initialize ();
  build_scops (&scops);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\nnumber of SCoPs: %d\n",
	     VEC_length (scop_p, scops));

  for (i = 0; VEC_iterate (scop_p, scops, i, scop); i++)
    {
      build_scop_bbs (scop);
      if (!build_sese_loop_nests (SCOP_REGION (scop)))
	continue;

      if (scop_contains_non_iv_scalar_phi_nodes (scop))
	continue;

      build_bb_loops (scop);
      build_sese_conditions (SCOP_REGION (scop));
      find_scop_parameters (scop);
      build_scop_iteration_domain (scop);
      add_conditions_to_constraints (scop);
      build_scop_canonical_schedules (scop);
      build_scop_data_accesses (scop);
      
      if (graphite_apply_transformations (scop))
	transform_done = gloog (scop);
#ifdef ENABLE_CHECKING
      else
	{
	  cloog_prog_clast pc = scop_to_clast (scop);
	  cloog_clast_free (pc.stmt);
	  cloog_program_free (pc.prog);
	}
#endif
    }

  /* Cleanup.  */
  if (transform_done)
    cleanup_tree_cfg ();

  free_scops (scops);
  cloog_finalize ();
  free_original_copy_tables ();
}

#else /* If Cloog is not available: #ifndef HAVE_cloog.  */

void
graphite_transform_loops (void)
{
  sorry ("Graphite loop optimizations cannot be used");
}

#endif
