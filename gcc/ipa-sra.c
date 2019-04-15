/* Interprocedural scalar replacement of aggregates
   Copyright (C) 2008-2019 Free Software Foundation, Inc.

   Contributed by Martin Jambor <mjambor@suse.cz>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "predict.h"
#include "alloc-pool.h"
#include "tree-pass.h"
#include "ssa.h"
#include "cgraph.h"
#include "print-tree.h"
#include "gimple-pretty-print.h"
#include "alias.h"
#include "fold-const.h"
#include "tree-eh.h"
#include "stor-layout.h"
#include "gimplify.h"
#include "gimple-iterator.h"
#include "gimplify-me.h"
#include "gimple-walk.h"
#include "tree-cfg.h"
#include "tree-dfa.h"
#include "tree-ssa.h"
#include "tree-sra.h"
#include "symbol-summary.h"
#include "ipa-prop.h"
#include "params.h"
#include "dbgcnt.h"
#include "ipa-fnsummary.h"
#include "tree-inline.h"
#include "ipa-inline.h"
#include "ipa-utils.h"
#include "builtins.h"
#include "cfganal.h"
#include "errors.h"
#include "tree-streamer.h"


/* Bits used to track size of an aggregate in bytes interprocedurally.  */
#define ISRA_ARG_SIZE_LIMIT 16

/* Structure describing accesses to a specific portion of an aggregate
   parameter, as given by the offset and size.  Any smaller accesses that occur
   within a function that fall within another access form a tree.  The pass
   cannot analyze parameters with only partially overlapping accesses.  */

struct GTY(()) param_access
{
  /* Type that a potential replacement should have.  This field only has
     meaning in the summary building and transformation phases, when it is
     reconstructoed from the body.  Must not be touched in IPA analysys
     stage.  */
  tree type;

  /* Alias reference type to be used in MEM_REFs when adjusting caller
     arguments.  */
  tree alias_ptr_type;

  /* Values returned by get_ref_base_and_extent but converted to bytes and
     stored as unsigned ints.  */
  unsigned unit_offset;
  unsigned unit_size : ISRA_ARG_SIZE_LIMIT;

  /* Set once we are sure that the access will really end up in a potentially
     transformed function - initially not set for portions of formal parameters
     that are only used as actual function arguments passed to callees.  */
  unsigned definitive : 1;
  /* Set when initially we have allowed overlaps for this access and so it
     eventually needs checking for overlaps.  */
  /* !!! Use for testing only, otherwise not worth having, let's simply check
         all final splits.  */
  unsigned check_overlaps : 1;
};

/* This structure has the same purpose as the one above and additoonally it
   contains some fields that are only necessary in the summary generation
   phase.  */

struct gensum_param_access
{
  /* Values returned by get_ref_base_and_extent.  */
  HOST_WIDE_INT offset;
  HOST_WIDE_INT size;

  /* if this access has any children (in terms of the definition above), this
     points to the first one.  */
  struct gensum_param_access *first_child;

  /* In intraprocedural SRA, pointer to the next sibling in the access tree as
     described above.  */
  struct gensum_param_access *next_sibling;

  /* Type that a potential replacement should have.  This field only has
     meaning in the summary building and transformation phases, when it is
     reconstructoed from the body.  Must not be touched in IPA analysys
     stage.  */
  tree type;

  /* Alias refrerence type to be used in MEM_REFs when adjusting caller
     arguments.  */
  tree alias_ptr_type;

  /* Have there been writes to or reads from this exact location except for as
     arguments to a function call that can be tracked.  */
  bool nonarg;
};

/* Summary describing a parameter in the IPA stages.  */

/* !!! TODO: Probably remove the m_prefixes here.  */
struct GTY(()) isra_param_desc
{
  /* List of access representatives to the parameters, sorted according to
     their offset.  */
  vec <param_access *, va_gc> *m_accesses;

  /* If the below is non-zero, this is the nuber of uses as actual arguents.  */
  int m_call_uses;   		/* !!! Unnecessary? */

  /* How many of the call uses are passes to nodes in other SCC components.  */
  int m_scc_uses;

  /* Unit size limit of total size of all replacements.  */
  unsigned m_param_size_limit : ISRA_ARG_SIZE_LIMIT;
  /* Sum of unit sizes of all definitive replacements.  */
  unsigned m_size_reached : ISRA_ARG_SIZE_LIMIT;

  /* A parameter that is used only in call arguments and can be removed if all
     concerned actual arguments are removed.  */
  unsigned m_locally_unused : 1;
  /* An aggregate that is a candidate for breaking up or complete removal.  */
  unsigned m_split_candidate : 1;
  /* Is this a parameter passing stuff by reference?  */
  unsigned m_by_ref : 1;
};

/* Structure used when generating summaries that describes a parameter.  */

struct gensum_param_desc
{
  /* Roots of param_accesses.  */
  gensum_param_access *x_accesses; /* !!! x_ */

  /* Number of  */
  unsigned m_access_count;

  /* If the below is non-zero, this is the nuber of uses as actual arguents.  */
  int m_call_uses;

  /* Number of times this parameter has been directly passed to  */
  unsigned ptr_pt_count;

  /* Size limit of total size of all replacements.  */
  unsigned param_size_limit;	/* !!? m_ ? */
  /* Sum of sizes of nonarg accesses.  */
  unsigned nonarg_acc_size;      /* !!? m_ ? */

  /* A parameter that is used only in call arguments and can be removed if all
     concerned actual arguments are removed.  */
  bool m_locally_unused;
  /* An aggregate that is a candidate for breaking up or complete removal.  */
  bool m_split_candidate;
  /* Is this a parameter passing stuff by reference?  */
  bool m_by_ref;

  /* The number of this parameter as they are ordered in function decl.  */
  int m_param_number;

  /* For parameters passing data by reference, this is parameter index to
     compute indices to bb_dereferences.  */
  int m_deref_index;
};

/* Properly deallocate m_accesses of DESC.  TODO: Since this data strucutre is
   not in GC memory, this is not necessary and we can consider removing the
   function.  */

static void
free_param_decl_accesses (isra_param_desc *desc)
{
  unsigned len = vec_safe_length (desc->m_accesses);
  for (unsigned i = 0; i < len; ++i)
    ggc_free ((*desc->m_accesses)[i]);
  vec_free (desc->m_accesses);
}

/* Class used to convey information about functions from the
   intra-procedurwl analysis stage to inter-procedural one.  */

class GTY((for_user)) isra_func_summary
{
public:
  /* initialize the object.  */

  isra_func_summary ()
    : m_parameters (NULL), m_candidate (false), m_returns_value (false),
    m_return_ignored (false), m_queued (false)
  {}

  /* Destroy m_parameters.  */

  ~isra_func_summary ();

  /* Mark the function as not a candidate for any IPA-SRA transofrmation.
     Return true if it was a candidate until now.  */

  bool zap ();

  /* Vector of parameter descriptors corresponding to the function being
     analyzed.  */
  vec<isra_param_desc, va_gc> *m_parameters;

  /* Whether the node is even a candidate for any IPA-SRA transformation at
     all.  */
  unsigned m_candidate : 1;

  /* Whether the original function returns any value.  */
  unsigned m_returns_value : 1;

  /* Set to true if all call statements do not actually use the returned
     value.  */

  unsigned m_return_ignored : 1;

  /* Whether the node is already queued in IPA SRA stack during processing of
     call graphs SCCs.  */

  unsigned m_queued : 1;
};

/* Claen up and deallocate isra_func_summary points to.  TODO: Since this data
   strucutre is not in GC memory, this is not necessary and we can consider
   removing the destructor.  */

isra_func_summary::~isra_func_summary ()
{
  unsigned len = vec_safe_length (m_parameters);
  for (unsigned i = 0; i < len; ++i)
    free_param_decl_accesses (&(*m_parameters)[i]);
  vec_free (m_parameters);
}


/* Mark the function as not a candidate for any IPA-SRA transofrmation.  Return
   true if it was a candidate until now.  */

bool isra_func_summary::zap ()
{
  bool ret = m_candidate;
  m_candidate = false;
  vec_free (m_parameters);
  return ret;
}

#define IPA_SRA_MAX_PARAM_FLOW_LEN 7

/* Structure to describe which formal parameters feed into a particular actual
   arguments.  */

/* !!! this can easily be turned into a more compact representation.  */
struct isra_param_flow
{
  /* Number of elements in array inputs that contain valid data.  */
  char length;
  /* Indices of formal parameters that feed into the described actual
     argument.  */
  unsigned char inputs[IPA_SRA_MAX_PARAM_FLOW_LEN];

  /* True when the value of this actual copy is a portion of a formal
     parameter.  */
  unsigned aggregate_pass_through : 1; /* !!? bad name?  Also, active! */
  /* True when the value of this actual copy is a verbatim pass through of an
     obtained pointer.  */
  unsigned pointer_pass_through : 1; /* !!? bad name?  Also, active! */
  /* True when it is safe to copy access candidates here from the callee, which
     would mean introducing dereferences into callers of the caller.  */
  unsigned safe_to_import_accesses : 1;

  /* The number of the formal parameter that is passed through */
  unsigned param_number;
  /* Offset within the formal parameter.  */
  unsigned unit_offset;
  /* Size of the portion of the formal parameter.  */
  unsigned unit_size;
};

/* Strucutre used to convey information about calls from the intra-procedurwl
   analysis stage to inter-procedural one.  */

class isra_call_summary
{
public:
  isra_call_summary ()
    : m_inputs (), m_return_ignored (false), m_return_returned (false),
      m_bit_aligned_arg (false)
  {}

  void init_inputs (unsigned arg_count);
  void dump (FILE *f);

  /* Information about what formal parameters of the caller are used to compute
     indivisual actual arguments of this call.  */

  auto_vec <isra_param_flow> m_inputs; /* !!! Rename to arg_flow or sth similar? */

  /* Set to true if the call statement does not have a LHS.  */
  unsigned m_return_ignored : 1;

  /* Set to true if the LHS of call statement is only used to construct the
     return value of the caller.  */
  unsigned m_return_returned : 1;

  /* Set when any of the call arguments are not byte-aligned.  */
  unsigned m_bit_aligned_arg : 1;
};

/* Class to manage function summaries.  */

class GTY((user)) ipa_sra_function_summaries
  : public function_summary <isra_func_summary *>
{
public:
  ipa_sra_function_summaries (symbol_table *table, bool ggc):
    function_summary<isra_func_summary *> (table, ggc) { }

  virtual void duplicate (cgraph_node *, cgraph_node *,
			  isra_func_summary *old_sum,
			  isra_func_summary *new_sum);
};

/* Hook that is called by summary when a node is duplicated.  */

void
ipa_sra_function_summaries::duplicate (cgraph_node *, cgraph_node *,
				       isra_func_summary *old_sum,
				       isra_func_summary *new_sum)
{
  /* TODO: Somehow stop copying when ISRA is doing the cloning, it is
     useless.  */
  new_sum->m_candidate  = old_sum->m_candidate;
  new_sum->m_returns_value = old_sum->m_returns_value;
  new_sum->m_return_ignored = old_sum->m_return_ignored;
  gcc_assert (!old_sum->m_queued);
  new_sum->m_queued = false;

  unsigned param_count = vec_safe_length (old_sum->m_parameters);
  if (!param_count)
    return;
  vec_safe_reserve_exact (new_sum->m_parameters, param_count);
  new_sum->m_parameters->quick_grow_cleared (param_count);
  for (unsigned i = 0; i < param_count; i++)
    {
      isra_param_desc *s = &(*old_sum->m_parameters)[i];
      isra_param_desc *d = &(*new_sum->m_parameters)[i];

      d->m_call_uses = s->m_call_uses;
      d->m_scc_uses = s->m_scc_uses;
      d->m_param_size_limit = s->m_param_size_limit;
      d->m_size_reached = s->m_size_reached;
      d->m_locally_unused = s->m_locally_unused;
      d->m_split_candidate = s->m_split_candidate;
      d->m_by_ref = s->m_by_ref;

      unsigned acc_count = vec_safe_length (s->m_accesses);
      vec_safe_reserve_exact (d->m_accesses, acc_count);
      for (unsigned j = 0; j < acc_count; j++)
	{
	  param_access *from = (*s->m_accesses)[j];
	  param_access *to = ggc_cleared_alloc<param_access> ();
	  to->type = from->type;
	  to->alias_ptr_type = from->alias_ptr_type;
	  to->unit_offset = from->unit_offset;
	  to->unit_size = from->unit_size;
	  to->definitive = from->definitive;
	  to->check_overlaps = from->check_overlaps;
	  d->m_accesses->quick_push (to);
	}
    }
}

/* Pointer to the pass function summary holder.  */

static GTY(()) ipa_sra_function_summaries *func_sums;

/* Class to manage call summaries.  */

class ipa_sra_call_summaries: public call_summary <isra_call_summary *>
{
public:
  ipa_sra_call_summaries (symbol_table *table):
    call_summary<isra_call_summary *> (table) { }
};

static ipa_sra_call_summaries *call_sums;


/* Initialize m_inputs of a particular instance of isra_call_summary.
   ARG_COUNT is the number of actual arguments passed.  */

void
isra_call_summary::init_inputs (unsigned arg_count)
{
  if (arg_count == 0)
    {
      gcc_checking_assert (m_inputs.length () == 0);
      return;
    }
  if (m_inputs.length () == 0)
    {
      m_inputs.reserve_exact (arg_count);
      m_inputs.quick_grow_cleared (arg_count);
    }
  else
    gcc_checking_assert (arg_count == m_inputs.length ());
}

/* Dump all information in call summary to F.  */

void
isra_call_summary::dump (FILE *f)
{
  if (m_return_ignored)
    fprintf (f, "    return value ignored\n");
  if (m_return_returned)
    fprintf (f, "    return value used only to compute caller return value\n");
  for (unsigned i = 0; i < m_inputs.length (); i++)
    {
      fprintf (f, "    Parameter %u:\n", i);
      isra_param_flow *ipf = &m_inputs[i];

      if (ipf->length)
	{
	  bool first = true;
	  fprintf (f, "      Scalar param sources: ");
	  for (int j = 0; j < ipf->length; j++)
	    {
	      if (!first)
		fprintf (f, ", ");
	      else
		first = false;
	      fprintf (f, "%i", (int) ipf->inputs[j]);
	    }
	  fprintf (f, "\n");
	}
      if (ipf->aggregate_pass_through)
	fprintf (f, "      Aggregate pass through from param %u, "
		 "unit offset: %u , unit size: %u\n",
		 ipf->param_number, ipf->unit_offset, ipf->unit_size);
      if (ipf->pointer_pass_through)
	fprintf (f, "      Pointer pass through from param %u, "
		 "safe_to_import_accesses: %u\n",
		 ipf->param_number, ipf->safe_to_import_accesses);
    }
}

/* With all GTY stuff done, we can move to anonymous namespace.  */
namespace {

/* Return false the function is apparently unsuitable for IPA-SRA based on it's
   attributes, return true otherwise.  NODE is the cgraph node of the current
   function.  */

static bool
ipa_sra_preliminary_function_checks (cgraph_node *node)
{
  if (!node->local.can_change_signature)
    {
      if (dump_file)
	fprintf (dump_file, "Function cannot change signature.\n");
      return false;
    }

  if (!tree_versionable_function_p (node->decl))
    {
      if (dump_file)
	fprintf (dump_file, "Function is not versionable.\n");
      return false;
    }

  if (!opt_for_fn (node->decl, optimize)
      || !opt_for_fn (node->decl, flag_ipa_sra))
    {
      if (dump_file)
	fprintf (dump_file, "Not optimizing or IPA-SRA turned off for this "
		 "function.\n");
      return false;
    }

  if (DECL_VIRTUAL_P (node->decl))
    {
      if (dump_file)
	fprintf (dump_file, "Function is a virtual method.\n");
      return false;
    }

  struct function *fun = DECL_STRUCT_FUNCTION (node->decl);
  if (fun->stdarg)
    {
      if (dump_file)
	fprintf (dump_file, "Function uses stdarg. \n");
      return false;
    }

  if (TYPE_ATTRIBUTES (TREE_TYPE (node->decl)))
    {
      if (dump_file)
	fprintf (dump_file, "Function type has attributes. \n");
      return false;
    }

  if (DECL_DISREGARD_INLINE_LIMITS (node->decl))
    {
      if (dump_file)
	fprintf (dump_file, "Always inline function will be inlined "
		 "anyway. \n");
      return false;
    }

  return true;
}

/* Quick mapping from a decl to its param descriptor.  */
/* TODO: Make local? */

static hash_map<tree, gensum_param_desc *> *decl2desc;

/* Countdown of allowe Alias analysis steps during summary building.  */

static int aa_walking_limit;

/* This is a table in which for each basic block and parameter there is a
   distance (offset + size) in that parameter which is dereferenced and
   accessed in that BB.  */
static HOST_WIDE_INT *bb_dereferences = NULL;
/* How many by-reference parameters there are in the current function.  */
static int by_ref_count;

/* Bitmap of BBs that can cause the function to "stop" progressing by
   returning, throwing externally, looping infinitely or calling a function
   which might abort etc.. */
static bitmap final_bbs;

/* Print access tree starting at ACCESS to F.  */

static void
dump_gensum_access (FILE *f, gensum_param_access *access, unsigned indent)
{
  fprintf (f, "  ");
  for (unsigned i = 0; i < indent; i++)
    fprintf (f, " ");
  fprintf (f, "    * Access to offset: " HOST_WIDE_INT_PRINT_DEC,
	   access->offset);
  fprintf (f, ", size: " HOST_WIDE_INT_PRINT_DEC, access->size);
  fprintf (f, ", type: ");
  print_generic_expr (f, access->type);
  fprintf (f, ", alias_ptr_type: ");
  print_generic_expr (f, access->alias_ptr_type);
  fprintf (f, ", nonarg: %u\n", access->nonarg);
  for (gensum_param_access *ch = access->first_child;
       ch;
       ch = ch->next_sibling)
    dump_gensum_access (f, ch, indent + 2);
}


/* Print access tree starting at ACCESS to F.  */

static void
dump_isra_access (FILE *f, param_access *access)
{
  fprintf (f, "    * Access to unit offset: %u", access->unit_offset);
  fprintf (f, ", unit size: %u", access->unit_size);
  fprintf (f, ", type: ");
  print_generic_expr (f, access->type);
  fprintf (f, ", alias_ptr_type: ");
  print_generic_expr (f, access->alias_ptr_type);
  fprintf (f, ", definitive: %u, check_overlaps: %u\n", access->definitive,
	   access->check_overlaps);
}

/* Dump access tree starting at ACCESS to stderr.  */

DEBUG_FUNCTION void
debug_isra_access (param_access *access)
{
  dump_isra_access (stderr, access);
}

/* Dump DESC to F.  */

static void
dump_gensum_param_descriptor (FILE *f, gensum_param_desc *desc)
{
  if (desc->m_locally_unused)
    {
      fprintf (f, "    unused with %i call_uses\n", desc->m_call_uses);
    }
  if (!desc->m_split_candidate)
    {
      fprintf (f, "    not a candidate\n");
      return;
    }
  if (desc->m_by_ref)
    fprintf (f, "    by_ref with %u pass throughs\n", desc->ptr_pt_count);

  for (gensum_param_access *acc = desc->x_accesses;
       acc;
       acc = acc->next_sibling)
    dump_gensum_access (f, acc, 2);
}

/* Dump all parameter descriptors in IFS, assuming it describes FNDECl, to
   F.  */

static void
dump_gensum_param_descriptors (FILE *f, tree fndecl,
			       vec<gensum_param_desc> *param_descriptions)
{
  tree parm = DECL_ARGUMENTS (fndecl);
  for (unsigned i = 0;
       i < param_descriptions->length ();
       ++i, parm = DECL_CHAIN (parm))
    {
      fprintf (f, "  Descriptor for parameter %i ", i);
      print_generic_expr (f, parm, TDF_UID);
      fprintf (f, "\n");
      dump_gensum_param_descriptor (f, &(*param_descriptions)[i]);
    }
}


/* Dump DESC to F.   */

static void
dump_isra_param_descriptor (FILE *f, isra_param_desc *desc)
{
  if (desc->m_locally_unused)
    {
      fprintf (f, "    unused with %i call_uses\n", desc->m_call_uses);
    }
  if (!desc->m_split_candidate)
    {
      fprintf (f, "    not a candidate\n");
      return;
    }
  fprintf (f, "    param_size_limit: %u, size_reached: %u%s\n",
	   desc->m_param_size_limit, desc->m_size_reached,
	   desc->m_by_ref ? ", by_ref" : "");

  for (unsigned i = 0; i < vec_safe_length (desc->m_accesses); ++i)
    {
      param_access *access = (*desc->m_accesses)[i];
      dump_isra_access (f, access);
    }
}

/* Dump all parameter descriptors in IFS, assuming it describes FNDECl, to
   F.  */

static void
dump_isra_param_descriptors (FILE *f, tree fndecl,
			     isra_func_summary *ifs)
{
  tree parm = DECL_ARGUMENTS (fndecl);
  if (!ifs->m_parameters)
    {
      fprintf (f, "  parameter descriptors not available\n");
      return;
    }

  for (unsigned i = 0;
       i < ifs->m_parameters->length ();
       ++i, parm = DECL_CHAIN (parm))
    {
      fprintf (f, "  Descriptor for parameter %i ", i);
      print_generic_expr (f, parm, TDF_UID);
      fprintf (f, "\n");
      dump_isra_param_descriptor (f, &(*ifs->m_parameters)[i]);
    }
}

/* Add SRC to PARAM_FLOW, unless already there or would exceed limit or not fit
   in a char.  If it would exeed limit or would not fit in a char, return
   false, otherwise return true.  */

static bool
add_src_to_param_flow (isra_param_flow *param_flow, int src)
{
  gcc_checking_assert (src >= 0);
  if (src > UCHAR_MAX
      || param_flow->length == IPA_SRA_MAX_PARAM_FLOW_LEN)
    return false;

  param_flow->inputs[(int) param_flow->length] = src;
  param_flow->length++;
  return true;
}

/* Inspect all uses of NAME and simple arithmetic calculations involving NAME
   in NODE and return a negative number if any of them is used for something
   else than either an actual call argument, simple arithemtic operation or
   debug statement.  If there are no such uses, return the number of actual
   arguments that this parameter evetually feeds to (or zero if there is none).
   For any such parameter, mark PAMR_NUM as one of its sources.  ANALYZED is a
   bitmap that tracks which SSA names we have already started
   investigating.  */

static int
isra_track_scalar_value_uses (cgraph_node *node, tree name, int parm_num,
			      bitmap analyzed)
{
  int res = 0;
  imm_use_iterator imm_iter;
  gimple *stmt;

  FOR_EACH_IMM_USE_STMT (stmt, imm_iter, name)
    {
      if (is_gimple_debug (stmt))
	continue;

      /* TODO: I guess we could handle at least const builtin functions like
	 arithmetic operations below.  */
      if (is_gimple_call (stmt))
	{
	  int all_uses = 0;
	  use_operand_p use_p;
	  FOR_EACH_IMM_USE_ON_STMT (use_p, imm_iter)
	    all_uses++;

	  gcall *call = as_a <gcall *> (stmt);
	  unsigned arg_count;
	  if (gimple_call_internal_p (call)
	      || (arg_count = gimple_call_num_args (call)) == 0)
	    {
	      res = -1;
	      BREAK_FROM_IMM_USE_STMT (imm_iter);
	    }

	  cgraph_edge *cs = node->get_edge (stmt);
	  gcc_checking_assert (cs);
	  isra_call_summary *csum = call_sums->get_create (cs);
	  csum->init_inputs (arg_count);

	  int simple_uses = 0;
	  for (unsigned i = 0; i < arg_count; i++)
	    if (gimple_call_arg (call, i) == name)
	      {
		if (!add_src_to_param_flow (&csum->m_inputs[i], parm_num))
		  {
		    simple_uses = -1;
		    break;
		  }
		simple_uses++;
	      }

	  if (simple_uses < 0
	      || all_uses != simple_uses)
	    {
	      res = -1;
	      BREAK_FROM_IMM_USE_STMT (imm_iter);
	    }
	  res += all_uses;
	}
      else if ((is_gimple_assign (stmt) && !gimple_has_volatile_ops (stmt))
	       || gimple_code (stmt) == GIMPLE_PHI)
	{
	  tree lhs;
	  if (gimple_code (stmt) == GIMPLE_PHI)
	    lhs = gimple_phi_result (stmt);
	  else
	    lhs = gimple_assign_lhs (stmt);

	  if (TREE_CODE (lhs) != SSA_NAME)
	    {
	      res = -1;
	      BREAK_FROM_IMM_USE_STMT (imm_iter);
	    }
	  gcc_assert (!gimple_vdef (stmt));
	  if (bitmap_set_bit (analyzed, SSA_NAME_VERSION (lhs)))
	    {
	      int tmp = isra_track_scalar_value_uses (node, lhs, parm_num,
						      analyzed);
	      if (tmp < 0)
		{
		  res = tmp;
		  BREAK_FROM_IMM_USE_STMT (imm_iter);
		}
	      res += tmp;
	    }
	}
      else
	{
	  res = -1;
	  BREAK_FROM_IMM_USE_STMT (imm_iter);
	}
    }
  return res;
}

/* Inspect all uses of PARM, which must be a gimple register, in FUN (which is
   also described by NODE) and simple arithmetic calculations involving PARM
   and return false if any of them is used for something else than either an
   actual call argument, simple arithemtic operation or debug statement.  If
   there are no such uses, return true and store the number of actual arguments
   that this parameter evetually feeds to (or zero if there is none) to
   *CALL_USES_P.  For any such parameter, mark PAMR_NUM as one of its
   sources.  */

static bool
isra_track_scalar_param (function *fun, cgraph_node *node, tree parm,
			 int parm_num, int *call_uses_p)
{
  gcc_checking_assert (is_gimple_reg (parm));

  tree name = ssa_default_def (fun, parm);
  if (!name || has_zero_uses (name))
    {
      *call_uses_p = 0;
      return true;
    }

  bitmap analyzed = BITMAP_ALLOC (NULL);
  int call_uses = isra_track_scalar_value_uses (node, name, parm_num, analyzed);
  BITMAP_FREE (analyzed);
  if (call_uses < 0)
    return false;
  *call_uses_p = call_uses;
  return true;
}

/* Scan immediate uses of a default definition SSA name of a parameter PARM and
   examine whether there are any nonarg uses that are not actual arguments or
   otherwise infeasible uses.  If so, return true, otherwise return false.
   Create pass-through IPA flow records for any direct uses as argument calls
   and if returning false, store their number into *PT_COUNT_P.  NODE and FUN
   must represent the function that is currently analyzed, PARM_NUM must be the
   index of the analyzed parameter.  */

static bool
ptr_parm_has_nonarg_uses (cgraph_node *node, function *fun, tree parm,
			  int parm_num, unsigned *pt_count_p)
{
  imm_use_iterator ui;
  gimple *stmt;
  tree name = ssa_default_def (fun, parm);
  bool ret = false;
  unsigned pt_count = 0;

  if (!name || has_zero_uses (name))
    return false;

  FOR_EACH_IMM_USE_STMT (stmt, ui, name)
    {
      unsigned uses_ok = 0;
      use_operand_p use_p;

      if (is_gimple_debug (stmt))
	continue;

      if (gimple_assign_single_p (stmt))
	{
	  tree rhs = gimple_assign_rhs1 (stmt);
	  while (handled_component_p (rhs))
	    rhs = TREE_OPERAND (rhs, 0);
	  if (TREE_CODE (rhs) == MEM_REF
	      && TREE_OPERAND (rhs, 0) == name
	      && integer_zerop (TREE_OPERAND (rhs, 1))
	      && types_compatible_p (TREE_TYPE (rhs),
				     TREE_TYPE (TREE_TYPE (name)))
	      && !TREE_THIS_VOLATILE (rhs))
	    uses_ok++;
	}
      else if (is_gimple_call (stmt))
	{
	  gcall *call = as_a <gcall *> (stmt);
	  unsigned arg_count;
	  if (gimple_call_internal_p (call)
	      || (arg_count = gimple_call_num_args (call)) == 0)
	    {
	      ret = true;
	      BREAK_FROM_IMM_USE_STMT (ui);
	    }

	  cgraph_edge *cs = node->get_edge (stmt);
	  gcc_checking_assert (cs);
	  isra_call_summary *csum = call_sums->get_create (cs);
	  csum->init_inputs (arg_count);

	  for (unsigned i = 0; i < arg_count; ++i)
	    {
	      tree arg = gimple_call_arg (stmt, i);

	      if (arg == name)
		{
		  /* TODO: Allow &MEM_REF[name + offset] here,
		     ipa_param_body_adjustments::modify_call_stmt has to be
		     adjusted too.  */
		  csum->m_inputs[i].pointer_pass_through = true;
		  csum->m_inputs[i].param_number = parm_num;
		  pt_count++;
		  uses_ok++;
		  continue;
		}
	      /* TODO: Calculate offset here and we can also consider
		 ADDR_EXPR's of MEM_REFs a pass-through.  */

	      while (handled_component_p (arg))
		arg = TREE_OPERAND (arg, 0);
	      if (TREE_CODE (arg) == MEM_REF
		  && TREE_OPERAND (arg, 0) == name
		  && integer_zerop (TREE_OPERAND (arg, 1))
		  && types_compatible_p (TREE_TYPE (arg),
					 TREE_TYPE (TREE_TYPE (name)))
		  && !TREE_THIS_VOLATILE (arg))
		uses_ok++;
	    }
	}

      /* If the number of valid uses does not match the number of
         uses in this stmt there is an unhandled use.  */
      unsigned all_uses = 0;
      FOR_EACH_IMM_USE_ON_STMT (use_p, ui)
	all_uses++;

      gcc_checking_assert (uses_ok <= all_uses);
      if (uses_ok != all_uses)
	{
	  ret = true;
	  BREAK_FROM_IMM_USE_STMT (ui);
	}
    }

  *pt_count_p = pt_count;
  return ret;
}

/* Initialize vector of parameter descriptors of NODE.  Return true if there
   are any candidates for any optimization.  */

static bool
create_parameter_descriptors (cgraph_node *node,
			      vec<gensum_param_desc> *param_descriptions)
{
  function *fun = DECL_STRUCT_FUNCTION (node->decl);
  bool ret = false;

  int num = 0;
  for (tree parm = DECL_ARGUMENTS (node->decl);
       parm;
       parm = DECL_CHAIN (parm), num++)
    {
      const char *msg;
      gensum_param_desc *desc = &(*param_descriptions)[num];
      /* param_descriptions vector is grown cleared in the caller.  */
      desc->m_param_number = num;
      decl2desc->put (parm, desc);

      if (dump_file && (dump_flags & TDF_DETAILS))
	print_generic_expr (dump_file, parm, TDF_UID);

      int scalar_call_uses;
      tree type = TREE_TYPE (parm);
      if (TREE_THIS_VOLATILE (parm))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, is volatile\n");
	  continue;
	}
      if (!is_gimple_reg_type (type) && is_va_list_type (type))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, is a va_list type\n");
	  continue;
	}
      if (TREE_ADDRESSABLE (parm))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, is addressable\n");
	  continue;
	}
      if (TREE_ADDRESSABLE (type))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, type cannot be split\n");
	  continue;
	}

      if (is_gimple_reg (parm)
	  && isra_track_scalar_param (fun, node, parm, num, &scalar_call_uses))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " is a scalar with only %i call uses\n",
		     scalar_call_uses);

	  desc->m_locally_unused = true;
	  desc->m_call_uses = scalar_call_uses;
	  ret = true;
	}

      if (POINTER_TYPE_P (type))
	{
	  type = TREE_TYPE (type);

	  if (TREE_CODE (type) == FUNCTION_TYPE
	      || TREE_CODE (type) == METHOD_TYPE)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, " not a candidate, reference to "
			 "a function\n");
	      continue;
	    }
	  if (TYPE_VOLATILE (type))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, " not a candidate, reference to "
			 "a volatile type\n");
	      continue;
	    }
	  if (TREE_CODE (type) == ARRAY_TYPE
	      && TYPE_NONALIASED_COMPONENT (type))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, " not a candidate, reference to a"
			 "nonaliased component array\n");
	      continue;
	    }
	  if (!is_gimple_reg (parm))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, " not a candidate, a reference which is "
			 "not a gimple register (probably addressable)\n");
	      continue;
	    }
	  if (is_va_list_type (type))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, " not a candidate, reference to "
			 "a va list\n");
	      continue;
	    }
	  if (ptr_parm_has_nonarg_uses (node, fun, parm, num,
					     &desc->ptr_pt_count))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, " not a candidate, reference has "
			 "nonarg uses\n");
	      continue;
	    }
	  desc->m_by_ref = true;
	}
      else if (!AGGREGATE_TYPE_P (type))
	{
	  /* This is in an else branch because scalars passed by reference are
	     still candidates to be passed by value.  */
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, not an aggregate\n");
	  continue;
	}

      if (!COMPLETE_TYPE_P (type))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, not a complete type\n");
	  continue;
	}
      if (!tree_fits_uhwi_p (TYPE_SIZE (type)))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, size not representable\n");
	  continue;
	}
      unsigned HOST_WIDE_INT type_size
	= tree_to_uhwi (TYPE_SIZE (type)) / BITS_PER_UNIT;
      if (type_size == 0
	  || type_size >= 1 << ISRA_ARG_SIZE_LIMIT)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, " not a candidate, has zero or huge size\n");
	  continue;
	}
      if (type_internals_preclude_sra_p (type, &msg))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	      fprintf (dump_file, " not a candidate, %s\n", msg);
	  continue;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, " is a candidate\n");

      ret = true;
      desc->m_split_candidate = true;
      if (desc->m_by_ref)
	desc->m_deref_index = by_ref_count++;
    }
  return ret;
}

/* Return pointer to descriptor of parameter DECL or NULL if we are looking at .  */

static gensum_param_desc *
get_gensum_param_desc (tree decl)
{
  gcc_checking_assert (TREE_CODE (decl) == PARM_DECL);
  gensum_param_desc **slot = decl2desc->get (decl);
  if (!slot)
    /* This can happen for static chains which we cannot handle so far.  */
    return NULL;
  gcc_checking_assert (*slot);
  return *slot;
}


/* Remove parameter described by DESC from candidates for IPA-SRA and write
   REASON to the dump file if there is one.  */

/* !!? Perhaps rename to emphasize this prevents splitting, not removal? */

static void
disqualify_split_candidate (gensum_param_desc *desc, const char *reason)
{
  if (!desc->m_split_candidate)
    return;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "! Disqualifying parameter number %i - %s\n",
	     desc->m_param_number, reason);

  desc->m_split_candidate = false;
}

/* Remove DECL from candidates for IPA-SRA and write REASON to the dump file if
   there is one.  */

static void
disqualify_split_candidate (tree decl, const char *reason)
{
  gensum_param_desc *desc = get_gensum_param_desc (decl);
  if (desc)
    disqualify_split_candidate (desc, reason);
}

/* Allocate a new access to DESC and fill it in with OFFSET and SIZE.  But
   first, check that there are not too many of them already.  If so, do not
   allocate anything and return NULL.  */

static gensum_param_access *
allocate_access (gensum_param_desc *desc,
		 HOST_WIDE_INT offset, HOST_WIDE_INT size)
{
  if (desc->m_access_count
      == (unsigned) PARAM_VALUE (PARAM_IPA_SRA_MAX_REPLACEMENTS))
    {
      disqualify_split_candidate (desc, "Too many replacement candidates");
      return NULL;
    }

  /* !!! TODO: allocate from an obstack */
  gensum_param_access *access = new gensum_param_access ();
  memset (access, 0, sizeof (*access));
  access->offset = offset;
  access->size = size;
  return access;
}

/* In what context scan_expr_access has been called, whether it deals with a
   load, a function argument, or a store.  */

enum isra_scan_context {ISRA_CTX_LOAD, ISRA_CTX_ARG, ISRA_CTX_STORE};

/* Return an access describing memory access to the variable described by DESC
   at OFFSET with SIZE in context CTX, starting at pointer to the linked list
   at a certaint tree level FIRST.  Attempt to create if it does not exist, but
   fail and return NULL if there are already too many accesses, if it would
   create a partially overlapping access or if an access woule end up in a
   non-call access.  */

static gensum_param_access *
get_access_1 (gensum_param_desc *desc, gensum_param_access **first,
	      HOST_WIDE_INT offset, HOST_WIDE_INT size, isra_scan_context ctx)
{
  gensum_param_access *access = *first, **ptr = first;

  if (!access)
    {
      /* No pre-existing access at this level, just create it.  */
      gensum_param_access *a = allocate_access (desc, offset, size);
      if (!a)
	return NULL;
      *first = a;
      return *first;
    }

  if (access->offset >= offset + size)
    {
      /* We want to squeeze in in front of the very first access, just do
	 it.  */
      gensum_param_access *r = allocate_access (desc, offset, size);
      if (!r)
	return NULL;
      r->next_sibling = access;
      *first = r;
      return r;
    }

  /* Skip all accesses that have to come before us until the next sibling is
     already too far.  */
  while (offset >= access->offset + access->size
	 && access->next_sibling
	 && access->next_sibling->offset < offset + size)
    {
      ptr = &access->next_sibling;
      access = access->next_sibling;
    }

  /* At this point we know we do not belong before access.  */
  gcc_assert (access->offset < offset + size);

  if (access->offset == offset && access->size == size)
    /* We found what we were looking for.  */
    return access;

  if (access->offset <= offset
      && access->offset + access->size >= offset + size)
    {
    /* We fit into access which is larger than us.  We need to find/create
       something below access.  But we only allow nesting in call
       arguments.  */
      if (access->nonarg)
	return NULL;

      return get_access_1 (desc, &access->first_child, offset, size, ctx);
    }

  if (offset <= access->offset
      && offset + size  >= access->offset + access->size)
    /* We are actually bigger than access, which fully fits into us, take its
       place and make all accesses fitting into it its children.  */
    {
      if (ctx != ISRA_CTX_ARG)
	return NULL;

      gensum_param_access *r = allocate_access (desc, offset, size);
      if (!r)
	return NULL;
      r->first_child = access;

      while (access->next_sibling
	     && access->next_sibling->offset < offset + size)
	access = access->next_sibling;
      if (access->offset + access->size > offset + size)
	{
	  /* This must be a different access, which are sorted, so the
	     following must be true and this signals a partial overlap.  */
	  gcc_assert (access->offset > offset);
	  return NULL;
	}

      r->next_sibling = access->next_sibling;
      access->next_sibling = NULL;
      *ptr = r;
      return r;
    }

  if (offset >= access->offset + access->size)
    {
      /* We belong after access.  */
      gensum_param_access *r = allocate_access (desc, offset, size);
      if (!r)
	return NULL;
      r->next_sibling = access->next_sibling;
      access->next_sibling = r;
      return r;
    }

  if (offset < access->offset)
    {
      /* We know the following, otherwise we would have created a
	 super-access. */
      gcc_checking_assert (offset + size < access->offset + access->size);
      return NULL;
    }

  if (offset + size > access->offset + access->size)
    {
      /* Likewise.  */
      gcc_checking_assert (offset > access->offset);
      return NULL;
    }

  gcc_unreachable ();
}

/* Return an access describing memory access to the variable described by DESC
   at OFFSET with SIZE in context CTX, mark it as used in context CTX.  Attempt
   to create if it does not exist, but fail and return NULL if there are
   already too many accesses, if it would create a partially overlapping access
   or if an access woule end up in a non-call access.  */

static gensum_param_access *
get_access (gensum_param_desc *desc, HOST_WIDE_INT offset, HOST_WIDE_INT size,
	    isra_scan_context ctx)
{
  gcc_checking_assert (desc->m_split_candidate);

  gensum_param_access *access = get_access_1 (desc, &desc->x_accesses, offset,
					      size, ctx);
  if (!access)
    {
      disqualify_split_candidate (desc, "Bad access overlap");
      return NULL;
    }

  switch (ctx)
    {
    case ISRA_CTX_STORE:
      gcc_assert (!desc->m_by_ref);
      /* Fall-through */
    case ISRA_CTX_LOAD:
      access->nonarg = true;
      break;
    case ISRA_CTX_ARG:
      break;
    }

  return access;
}

/* Verify that parameter access tree starting with ACCESS is in good shape.
   PARENT_OFFSET and PARENT_SIZE are the ciorresponding fields of parent of
   ACCESS or zero if there is none.  */

static bool
verify_access_tree_1 (gensum_param_access *access, HOST_WIDE_INT parent_offset,
		      HOST_WIDE_INT parent_size)
{
  while (access)
    {
      gcc_assert (access->offset >= 0 && access->size > 0);

      if (parent_size != 0)
	{
	  if (access->offset < parent_offset)
	    {
	      error ("Access offset before parent offset");
	      return true;
	    }
	  if (access->size >= parent_size)
	    {
	      error ("Access size greater or equal to its parent size");
	      return true;
	    }
	  if (access->offset + access->size > parent_offset + parent_size)
	    {
	      error ("Access terminates outside of its parent");
	      return true;
	    }
	}

      if (verify_access_tree_1 (access->first_child, access->offset,
				access->size))
	return true;

      if (access->next_sibling
	  && (access->next_sibling->offset < access->offset + access->size))
	{
	  error ("Access overlaps with its sibling");
	  return true;
	}

      access = access->next_sibling;
    }
  return false;
}

/* Verify that parameter access tree starting with ACCESS is in good shape,
   halt compilation and dump the tree to stderr if not.  */

DEBUG_FUNCTION void
isra_verify_access_tree (gensum_param_access *access)
{
  if (verify_access_tree_1 (access, 0, 0))
    {
      for (; access; access = access->next_sibling)
        dump_gensum_access (stderr, access, 2);
      internal_error ("IPA-SRA access verification failed");
    }
}


/* Callback of walk_stmt_load_store_addr_ops visit_addr used to determine
   GIMPLE_ASM operands with memory constrains which cannot be scalarized.  */

static bool
asm_visit_addr (gimple *, tree op, tree, void *)
{
  op = get_base_address (op);
  if (op
      && TREE_CODE (op) == PARM_DECL)
    disqualify_split_candidate (op, "Non-scalarizable GIMPLE_ASM operand.");

  return false;
}

/* Mark a dereference of parameter identified by DESC of distance DIST in a
   basic block BB, unless the BB has already been marked as a potentially
   final.  */

static void
mark_param_dereference (gensum_param_desc *desc, HOST_WIDE_INT dist,
		       basic_block bb)
{
  gcc_assert (desc->m_by_ref);
  gcc_checking_assert (desc->m_split_candidate);

  if (bitmap_bit_p (final_bbs, bb->index))
    return;

  int idx = bb->index * by_ref_count + desc->m_deref_index;
  if (bb_dereferences[idx] < dist)
    bb_dereferences[idx] = dist;
}

/* Return true, if any potential replacements should use NEW_TYPE as opposed to
   previously recorded OLD_TYPE.  */

static bool
type_prevails_p (tree old_type, tree new_type)
{
  if (old_type == new_type)
    return false;

  /* Non-aggregates are always better.  */
  if (!is_gimple_reg_type (old_type)
      && is_gimple_reg_type (new_type))
    return true;
  if (is_gimple_reg_type (old_type)
      && !is_gimple_reg_type (new_type))
    return false;

  /* Prefer any complex or vector type over any other scalar type.  */
  if (TREE_CODE (old_type) != COMPLEX_TYPE
      && TREE_CODE (old_type) != VECTOR_TYPE
      && (TREE_CODE (new_type) == COMPLEX_TYPE
	  || TREE_CODE (new_type) == VECTOR_TYPE))
    return true;
  if ((TREE_CODE (old_type) == COMPLEX_TYPE
       || TREE_CODE (old_type) == VECTOR_TYPE)
      && TREE_CODE (new_type) != COMPLEX_TYPE
      && TREE_CODE (new_type) != VECTOR_TYPE)
    return false;

  /* Use the integral type with the bigger precision first.  */
  if (INTEGRAL_TYPE_P (old_type)
      && INTEGRAL_TYPE_P (new_type))
    return (TYPE_PRECISION (new_type) > TYPE_PRECISION (old_type));

  /* Put any integral type with non-full precision last.  */
  if (INTEGRAL_TYPE_P (old_type)
      && (TREE_INT_CST_LOW (TYPE_SIZE (old_type))
	  != TYPE_PRECISION (old_type)))
    return true;
  if (INTEGRAL_TYPE_P (new_type)
      && (TREE_INT_CST_LOW (TYPE_SIZE (new_type))
	  != TYPE_PRECISION (new_type)))
    return false;
  /* Stabilize the selection.  */
  return TYPE_UID (old_type) < TYPE_UID (new_type);
}

/* When scanning an expression which is a call argument, this structure
   specifies the call and the position of the agrument.  */

struct scan_call_info
{
  /* Call graph edge representing the call. */
  cgraph_edge *cs;
  /* Total number of arguments in the call.  */
  unsigned argument_count;
  /* Number of the actual argument being scanned.  */
  unsigned arg_idx;
};

/* Record use of ACCESS which belongs to a parameter described by DESC in a
   call argument described by CALL_INFO.  */

static void
record_nonregister_call_use (gensum_param_desc *desc,
			     scan_call_info *call_info,
			     HOST_WIDE_INT offset, HOST_WIDE_INT size)
{
  isra_call_summary *csum = call_sums->get_create (call_info->cs);
  csum->init_inputs (call_info->argument_count);

  isra_param_flow *param_flow = &csum->m_inputs[call_info->arg_idx];
  param_flow->aggregate_pass_through = true;
  param_flow->param_number = desc->m_param_number;

  gcc_checking_assert ((offset % BITS_PER_UNIT) == 0);
  gcc_checking_assert ((size % BITS_PER_UNIT) == 0);
  param_flow->unit_offset = offset / BITS_PER_UNIT;
  param_flow->unit_size = size / BITS_PER_UNIT;

  desc->m_call_uses++;
}

/* Callback of walk_aliased_vdefs, just mark that there was a possible
   modification. */

static bool
mark_maybe_modified (ao_ref *, tree, void *data)
{
  bool *maybe_modified = (bool *) data;
  *maybe_modified = true;
  return true;
}

/* Analyze expression EXPR from GIMPLE for accesses to parameters. CTX
   specifies whether EXPR is used in a load, store or as an argument call. BB
   should be the basic block in which expr resides.  If CTX specifies call
   arguemnt context, CALL_INFO must describe tha call and argument position,
   otherwise it is ignored.  */

static void
scan_expr_access (tree expr, gimple *stmt, isra_scan_context ctx,
		  basic_block bb, scan_call_info *call_info = NULL)
{
  poly_int64 poffset, psize, pmax_size;
  HOST_WIDE_INT offset, size, max_size;
  tree base;
  bool deref = false;
  bool reverse;

  if (TREE_CODE (expr) == BIT_FIELD_REF
      || TREE_CODE (expr) == IMAGPART_EXPR
      || TREE_CODE (expr) == REALPART_EXPR)
    expr = TREE_OPERAND (expr, 0);

  base = get_ref_base_and_extent (expr, &poffset, &psize, &pmax_size, &reverse);

  if (TREE_CODE (base) == MEM_REF)
    {
      tree op = TREE_OPERAND (base, 0);
      if (TREE_CODE (op) != SSA_NAME
	  || !SSA_NAME_IS_DEFAULT_DEF (op))
	return;
      base = SSA_NAME_VAR (op);
      if (!base)
	return;
      deref = true;
    }
  if (TREE_CODE (base) != PARM_DECL)
    return;

  /* !!! Move get_gensum_param_desc here and then disqualify using it.  */
  if (!poffset.is_constant (&offset)
      || !psize.is_constant (&size)
      || !pmax_size.is_constant (&max_size))
    {
      disqualify_split_candidate (base, "Encountered a polynomial-sized "
				  "access.");
      return;
    }
  if (size < 0 || size != max_size)
    {
      disqualify_split_candidate (base, "Encountered a variable sized access.");
      return;
    }

  if (TREE_CODE (expr) == COMPONENT_REF
      && DECL_BIT_FIELD (TREE_OPERAND (expr, 1)))
    {
      disqualify_split_candidate (base, "Encountered a bit-field access.");
      return;
    }
  gcc_assert (offset >= 0);
  gcc_assert ((offset % BITS_PER_UNIT) == 0);
  gcc_assert ((size % BITS_PER_UNIT) == 0);
  if ((offset / BITS_PER_UNIT) >= UINT_MAX
      || (size / BITS_PER_UNIT) >= UINT_MAX)
    {
      disqualify_split_candidate (base, "Encountered an access with too big "
				  "offset or size");
      return;
    }

  gensum_param_desc *desc = get_gensum_param_desc (base);
  if (!desc || !desc->m_split_candidate)
    return;

  tree type = TREE_TYPE (expr);
  unsigned int exp_align = get_object_alignment (expr);

  if (exp_align < TYPE_ALIGN (type))
    {
      disqualify_split_candidate (desc, "Underaligned access.");
      return;
    }

  if (deref)
    {
      if (!desc->m_by_ref)
	{
	  disqualify_split_candidate (desc, "Dereferencing a non-reference.");
	  return;
	}
      else if (ctx == ISRA_CTX_STORE)
	{
	  disqualify_split_candidate (desc, "Storing to data passed by "
				      "reference.");
	  return;
	}

      if (!aa_walking_limit)
	{
	  disqualify_split_candidate (desc, "Out of alias analysis step "
				      "limit.");
	  return;
	}

      gcc_checking_assert (gimple_vuse (stmt));
      bool maybe_modified = false;
      ao_ref ar;

      ao_ref_init (&ar, expr);
      bitmap visited = BITMAP_ALLOC (NULL);
      int walked = walk_aliased_vdefs (&ar, gimple_vuse (stmt),
				       mark_maybe_modified, &maybe_modified,
				       &visited, NULL, aa_walking_limit);
      BITMAP_FREE (visited);
      if (walked > 0)
	{
	  gcc_assert (aa_walking_limit > walked);
	  aa_walking_limit = aa_walking_limit - walked;
	}
      if (walked < 0)
	aa_walking_limit = 0;
      if (maybe_modified || walked < 0)
	{
	  disqualify_split_candidate (desc, "Data passed by reference possibly "
				      "modified through an alias.");
	  return;
	}
      else
	mark_param_dereference (desc, offset + size, bb);
    }
  else
    /* Pointer parameters with direct uses should have been ruled out by
       analyzing SSA default def when looking at the paremeters.  */
    gcc_assert (!desc->m_by_ref);

  gensum_param_access *access = get_access (desc, offset, size, ctx);
  if (!access)
    return;

  if (ctx == ISRA_CTX_ARG)
    {
      gcc_checking_assert (call_info);
      if (!deref)
	record_nonregister_call_use (desc, call_info, offset, size);
      else
	/* This is not a pass-through of a pointer, this is a use like any
	   other.  */
	access->nonarg = true;
    }

  if (!access->type)
    {
      access->type = type;
      access->alias_ptr_type = reference_alias_ptr_type (expr);
    }
  else
    {
      if (exp_align < TYPE_ALIGN (access->type))
	{
	  disqualify_split_candidate (desc, "Reference has lower alignment "
				      "than a previous one.");
	  return;
	}
      if (access->alias_ptr_type != reference_alias_ptr_type (expr))
	{
	  disqualify_split_candidate (desc, "Multiple alias pointer types.");
	  return;
	}
      if (!deref
	  && (AGGREGATE_TYPE_P (type) || AGGREGATE_TYPE_P (access->type))
	  && (TYPE_MAIN_VARIANT (access->type) != TYPE_MAIN_VARIANT (type)))
	{
	  /* We need the same aggregate type on all accesses to be able to
	     distinguish transformation spots from pass-through arguments in
	     the transofrmation phase.  */
	  disqualify_split_candidate (desc, "We do not support aggegate "
				      "type punning.");
	  return;
	}

      if (type_prevails_p (access->type, type))
	 access->type = type;
    }
}

/* Scan body function described by NODE and FUN and create access trees for
   parameters.  */

static void
scan_function (cgraph_node *node, struct function *fun)
{
  basic_block bb;

  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi;
      for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);

	  if (stmt_can_throw_external (fun, stmt))
	    bitmap_set_bit (final_bbs, bb->index);
	  switch (gimple_code (stmt))
	    {
	    case GIMPLE_RETURN:
	      {
		tree t = gimple_return_retval (as_a <greturn *> (stmt));
		if (t != NULL_TREE)
		  scan_expr_access (t, stmt, ISRA_CTX_LOAD, bb);
		bitmap_set_bit (final_bbs, bb->index);
	      }
	      break;

	    case GIMPLE_ASSIGN:
	      if (gimple_assign_single_p (stmt)
		  && !gimple_clobber_p (stmt))
		{
		  tree rhs = gimple_assign_rhs1 (stmt);
		  scan_expr_access (rhs, stmt, ISRA_CTX_LOAD, bb);
		  tree lhs = gimple_assign_lhs (stmt);
		  scan_expr_access (lhs, stmt, ISRA_CTX_STORE, bb);
		}
	      break;

	    case GIMPLE_CALL:
	      {
		unsigned argument_count = gimple_call_num_args (stmt);
		scan_call_info call_info;
		call_info.cs = node->get_edge (stmt);
		call_info.argument_count = argument_count;

		for (unsigned i = 0; i < argument_count; i++)
		  {
		    call_info.arg_idx = i;
		    scan_expr_access (gimple_call_arg (stmt, i), stmt,
				      ISRA_CTX_ARG, bb, &call_info);
		  }

		tree lhs = gimple_call_lhs (stmt);
		if (lhs)
		  scan_expr_access (lhs, stmt, ISRA_CTX_STORE, bb);
		int flags = gimple_call_flags (stmt);
		if ((flags & (ECF_CONST | ECF_PURE)) == 0)
		  bitmap_set_bit (final_bbs, bb->index);
	      }
	      break;

	    case GIMPLE_ASM:
	      {
		gasm *asm_stmt = as_a <gasm *> (stmt);
		walk_stmt_load_store_addr_ops (asm_stmt, NULL, NULL, NULL,
					       asm_visit_addr);
		bitmap_set_bit (final_bbs, bb->index);

		for (unsigned i = 0; i < gimple_asm_ninputs (asm_stmt); i++)
		  {
		    tree t = TREE_VALUE (gimple_asm_input_op (asm_stmt, i));
		    scan_expr_access (t, stmt, ISRA_CTX_LOAD, bb);
		  }
		for (unsigned i = 0; i < gimple_asm_noutputs (asm_stmt); i++)
		  {
		    tree t = TREE_VALUE (gimple_asm_output_op (asm_stmt, i));
		    scan_expr_access (t, stmt, ISRA_CTX_STORE, bb);
		  }
	      }
	      break;

	    default:
	      break;
	    }
	}
    }
}

/* Return true if SSA_NAME NAME is only used in return statements, or if
   results of any operations it is involved in are only used in return
   statements.  ANALYZED is a bitmap that tracks which SSA names we have
   already started investigating.  */

static bool
ssa_name_only_returned_p (tree name, bitmap analyzed)
{
  bool res = true;
  imm_use_iterator imm_iter;
  gimple *stmt;

  FOR_EACH_IMM_USE_STMT (stmt, imm_iter, name)
    {
      if (is_gimple_debug (stmt))
	continue;

      if (gimple_code (stmt) == GIMPLE_RETURN)
	{
	  tree t = gimple_return_retval (as_a <greturn *> (stmt));
	  if (t != name)
	    {
	      res = false;
	      BREAK_FROM_IMM_USE_STMT (imm_iter);
	    }
	}
      else if ((is_gimple_assign (stmt) && !gimple_has_volatile_ops (stmt))
	       || gimple_code (stmt) == GIMPLE_PHI)
	{
	  /* TODO: And perhaps for const function calls too?  */
	  tree lhs;
	  if (gimple_code (stmt) == GIMPLE_PHI)
	    lhs = gimple_phi_result (stmt);
	  else
	    lhs = gimple_assign_lhs (stmt);

	  if (TREE_CODE (lhs) != SSA_NAME)
	    {
	      res = false;
	      BREAK_FROM_IMM_USE_STMT (imm_iter);
	    }
	  gcc_assert (!gimple_vdef (stmt));
	  if (bitmap_set_bit (analyzed, SSA_NAME_VERSION (lhs))
	      && !ssa_name_only_returned_p (lhs, analyzed))
	    {
	      res = false;
	      BREAK_FROM_IMM_USE_STMT (imm_iter);
	    }
	}
      else
	{
	  res = false;
	  BREAK_FROM_IMM_USE_STMT (imm_iter);
	}
    }
  return res;
}

/* Inspect the uses of the return value of the call associated with CS, and if
   it is not used or if it is only used to construct the return value of the
   caller, mark it as such in call or caller summary.  Also check for
   misaligned arguments.  */

static void
isra_analyze_call (cgraph_edge *cs)
{
  gcall *call_stmt = cs->call_stmt;
  unsigned count = gimple_call_num_args (call_stmt);
  isra_call_summary *csum = call_sums->get_create (cs);
  csum->init_inputs (count);  	/* !!? Try avoiding calling this. */
  for (unsigned i = 0; i < count; i++)
    {
      tree arg = gimple_call_arg (call_stmt, i);
      if (is_gimple_reg (arg))
	continue;

      tree offset;
      poly_int64 bitsize, bitpos;
      machine_mode mode;
      int unsignedp, reversep, volatilep = 0;
      get_inner_reference (arg, &bitsize, &bitpos, &offset, &mode,
			   &unsignedp, &reversep, &volatilep);
      if (!multiple_p (bitpos, BITS_PER_UNIT))
	{
	  csum->m_bit_aligned_arg = true;
	  break;
	}
    }

  tree lhs = gimple_call_lhs (call_stmt);
  if (lhs)
    {
      /* TODO: Also detect unused and/or only forwarded to
	 return aggregates.  */
      if (TREE_CODE (lhs) == SSA_NAME)
	{
	  bitmap analyzed = BITMAP_ALLOC (NULL);
	  if (ssa_name_only_returned_p (lhs, analyzed))
	    csum->m_return_returned = true;
	  BITMAP_FREE (analyzed);
	}
    }
  else
    csum->m_return_ignored = true;
}

/* Look at all calls going out of NODE, described also by IFS and perform all
   analyses necessary for IPA-SRA that are not done at body scan time or done
   even when body is not scanned because the function is not a candidate.  */

static void
isra_analyze_all_outgoing_calls (cgraph_node *node)
{
  for (cgraph_edge *cs = node->callees; cs; cs = cs->next_callee)
    isra_analyze_call (cs);
  for (cgraph_edge *cs = node->indirect_calls; cs; cs = cs->next_callee)
    isra_analyze_call (cs);
}

/* Dump a dereferences table with heading STR to file F.  */

static void
dump_dereferences_table (FILE *f, struct function *fun, const char *str)
{
  basic_block bb;

  fprintf (dump_file, "%s", str);
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR_FOR_FN (fun),
		  EXIT_BLOCK_PTR_FOR_FN (fun), next_bb)
    {
      fprintf (f, "%4i  %i   ", bb->index, bitmap_bit_p (final_bbs, bb->index));
      if (bb != EXIT_BLOCK_PTR_FOR_FN (fun))
	{
	  int i;
	  for (i = 0; i < by_ref_count; i++)
	    {
	      int idx = bb->index * by_ref_count + i;
	      fprintf (f, " %4" HOST_WIDE_INT_PRINT "d", bb_dereferences[idx]);
	    }
	}
      fprintf (f, "\n");
    }
  fprintf (dump_file, "\n");
}

/* Propagate distances in bb_dereferences in the opposite direction than the
   control flow edges, in each step storing the maximum of the current value
   and the minimum of all successors.  These steps are repeated until the table
   stabilizes.  Note that BBs which might terminate the functions (according to
   final_bbs bitmap) never updated in this way.  */

static void
propagate_dereference_distances (struct function *fun)
{
  basic_block bb;

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_dereferences_table (dump_file, fun,
			     "Dereference table before propagation:\n");

  auto_vec<basic_block> queue (last_basic_block_for_fn (fun));
  queue.quick_push (ENTRY_BLOCK_PTR_FOR_FN (fun));
  FOR_EACH_BB_FN (bb, fun)
    {
      queue.quick_push (bb);
      bb->aux = bb;
    }

  while (!queue.is_empty ())
    {
      edge_iterator ei;
      edge e;
      bool change = false;
      int i;

      bb = queue.pop ();
      bb->aux = NULL;

      if (bitmap_bit_p (final_bbs, bb->index))
	continue;

      for (i = 0; i < by_ref_count; i++)
	{
	  int idx = bb->index * by_ref_count + i;
	  bool first = true;
	  HOST_WIDE_INT inh = 0;

	  FOR_EACH_EDGE (e, ei, bb->succs)
	  {
	    int succ_idx = e->dest->index * by_ref_count + i;

	    if (e->src == EXIT_BLOCK_PTR_FOR_FN (fun))
	      continue;

	    if (first)
	      {
		first = false;
		inh = bb_dereferences [succ_idx];
	      }
	    else if (bb_dereferences [succ_idx] < inh)
	      inh = bb_dereferences [succ_idx];
	  }

	  if (!first && bb_dereferences[idx] < inh)
	    {
	      bb_dereferences[idx] = inh;
	      change = true;
	    }
	}

      if (change && !bitmap_bit_p (final_bbs, bb->index))
	FOR_EACH_EDGE (e, ei, bb->preds)
	  {
	    if (e->src->aux)
	      continue;

	    e->src->aux = e->src;
	    queue.quick_push (e->src);
	  }
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_dereferences_table (dump_file, fun,
			     "Dereference table after propagation:\n");
}

/* Perform basic checks on ACCESS to PARM described by DESC and all its
   children, return true if the parameter cannot be split, otherwise return
   true and update *TOTAL_SIZE and *ONLY_CALLS.  ENTRY_BB_INDEX must be the
   index of the entry BB in the function of PARM.  */

static bool
check_gensum_access (tree parm, gensum_param_desc *desc,
		     gensum_param_access *access,
		     HOST_WIDE_INT *nonarg_acc_size, bool *only_calls,
		      int entry_bb_index)
{
  if (access->nonarg)
    {
      *only_calls = false;
      *nonarg_acc_size += access->size;
    }
  /* Do not decompose a non-BLKmode param in a way that would create
     BLKmode params.  Especially for by-reference passing (thus,
     pointer-type param) this is hardly worthwhile.  */
  if (DECL_MODE (parm) != BLKmode
      && TYPE_MODE (access->type) == BLKmode)
    {
      disqualify_split_candidate (desc, "Would convert a non-BLK to a BLK.");
      return true;
    }

  if (desc->m_by_ref)
    {
      int idx = (entry_bb_index * by_ref_count + desc->m_deref_index);
      if ((access->offset + access->size) > bb_dereferences[idx])
	{
	  disqualify_split_candidate (desc, "Would create a possibly "
				      "illegal dereference in a caller.");
	  return true;
	}
    }

  for (gensum_param_access *ch = access->first_child;
       ch;
       ch = ch->next_sibling)
    if (check_gensum_access (parm, desc, ch, nonarg_acc_size, only_calls,
			     entry_bb_index))
      return true;

  return false;
}

/* Copy data from FROM and all of its children to a vector of accesses in IPA
   descriptor DESC.  */

static void
copy_accesses_to_ipa_desc (gensum_param_access *from, isra_param_desc *desc)
{
  param_access *to = ggc_cleared_alloc<param_access> ();
  gcc_checking_assert ((from->offset % BITS_PER_UNIT) == 0);
  gcc_checking_assert ((from->size % BITS_PER_UNIT) == 0);
  to->unit_offset = from->offset / BITS_PER_UNIT;
  to->unit_size = from->size / BITS_PER_UNIT;
  to->type = from->type;
  to->alias_ptr_type = from->alias_ptr_type;
  to->definitive = from->nonarg;
  to->check_overlaps = !from->nonarg;
  vec_safe_push (desc->m_accesses, to);

  for (gensum_param_access *ch = from->first_child;
       ch;
       ch = ch->next_sibling)
    copy_accesses_to_ipa_desc (ch, desc);
}

/* Analyze function body scan results stored in param_accesses and
   param_accesses, detect possible transformations and store information of
   those in function summary.  NODE, FUN and IFS are all various structures
   describing the currently analyzed function.  */

static void
process_scan_results (cgraph_node *node, struct function *fun,
		      isra_func_summary *ifs,
		      vec<gensum_param_desc> *param_descriptions)
{
  bool check_pass_throughs = false;
  bool dereferences_propagated = false;
  tree parm = DECL_ARGUMENTS (node->decl);
  unsigned param_count = param_descriptions->length();

  for (unsigned desc_index = 0;
       desc_index < param_count;
       desc_index++, parm = DECL_CHAIN (parm))
    {
      gensum_param_desc *desc = &(*param_descriptions)[desc_index];
      if (!desc->m_locally_unused && !desc->m_split_candidate)
	continue;

      if (flag_checking)
	isra_verify_access_tree (desc->x_accesses);

      if (!dereferences_propagated
	  && desc->m_by_ref
	  && desc->x_accesses)
	{
	  propagate_dereference_distances (fun);
	  dereferences_propagated = true;
	}

      HOST_WIDE_INT nonarg_acc_size = 0;
      bool only_calls = true;

      int entry_bb_index = ENTRY_BLOCK_PTR_FOR_FN (fun)->index;
      for (gensum_param_access *acc = desc->x_accesses;
	   acc;
	   acc = acc->next_sibling)
	if (check_gensum_access (parm, desc, acc, &nonarg_acc_size, &only_calls,
				 entry_bb_index))
	  continue;

      if (only_calls)
	desc->m_locally_unused = true;

      HOST_WIDE_INT cur_param_size
	= tree_to_uhwi (TYPE_SIZE (TREE_TYPE (parm)));
      HOST_WIDE_INT param_size_limit;
      if (!desc->m_by_ref || optimize_function_for_size_p (fun))
	param_size_limit = cur_param_size;
      else
	param_size_limit = (PARAM_VALUE (PARAM_IPA_SRA_PTR_GROWTH_FACTOR)
			   * cur_param_size);
      if (nonarg_acc_size > param_size_limit
	  || (!desc->m_by_ref && nonarg_acc_size == param_size_limit))
	{
	  disqualify_split_candidate (desc, "Would result into a too big set of"
				      "replacements.");
	}
      else
	{
	  /* create_parameter_descriptors makes sure unit sizes of all
	     candidate parameters fit unsigned integers restricted to
	     ISRA_ARG_SIZE_LIMIT bits.  */
	  desc->param_size_limit = param_size_limit / BITS_PER_UNIT;
	  desc->nonarg_acc_size = nonarg_acc_size / BITS_PER_UNIT;
	  if (desc->m_split_candidate && desc->ptr_pt_count)
	    {
	      gcc_assert (desc->m_by_ref); /* TODO: Remove after testing.  */
	      check_pass_throughs = true;
	    }
	}
    }

  /* When a pointer parameter is passed-through to a callee, in which it is
     only used to read only one or a few items, we can attempt to transform it
     to obtaining and passing through the items instead of the pointer.  But we
     must take extra care that 1) we do not introduce any segfault by moving
     dereferences above control flow and that 2) the data is not modified
     through an alias in this function.  The IPA analysis must not introduce
     any accesses candidates unless it can prove both.

     The current solution is very crude as it consists of ensuring that the
     call postdominates entry BB and that the definition of VUSE of the call is
     default definition.  TODO: For non-recursive callees in the same
     compilation unit we could do better by doing analysis in topological order
     an looking into access candidates of callees, using their alias_ptr_types
     to attempt real AA.  We could also use the maximum known dereferenced
     offset in this function at IPA level but chances are that it is smaller
     than the one in the callee (if the candidate survives relatively modest
     replacement size limit).

     TODO: Measure the overhead and the effect of just being pessimistic.
     Maybe this is ony -O3 material?
  */
  bool pdoms_calculated = false;
  if (check_pass_throughs)
    for (cgraph_edge *cs = node->callees; cs; cs = cs->next_callee)
      {
	gcall *call_stmt = cs->call_stmt;
	tree vuse = gimple_vuse (call_stmt);

	/* If the callee is a const function, we don't get a VUSE.  In such
	   case there will be no memory accesses in the called function (or the
	   const attribute is wrong) and then we just don't care.  */
	bool uses_memory_as_obtained = vuse && SSA_NAME_IS_DEFAULT_DEF (vuse);

	unsigned count = gimple_call_num_args (call_stmt);
	isra_call_summary *csum = call_sums->get_create (cs);
	csum->init_inputs (count);
	for (unsigned argidx = 0; argidx < count; argidx++)
	  {
	    if (!csum->m_inputs[argidx].pointer_pass_through)
	      continue;
	    unsigned pidx = csum->m_inputs[argidx].param_number;
	    gensum_param_desc *desc = &(*param_descriptions)[pidx];
	    if (!desc->m_split_candidate)
	      {
		csum->m_inputs[argidx].pointer_pass_through = false;
		continue;
	      }
	    if (!uses_memory_as_obtained)
	      continue;
	    /* Post-dominator check placed last, hoping that it usually won't
	       be needed.  */

	    if (!pdoms_calculated)
	      {
		push_cfun (fun);
		add_noreturn_fake_exit_edges ();
		connect_infinite_loops_to_exit ();
		calculate_dominance_info (CDI_POST_DOMINATORS);
		pdoms_calculated = true;
	      }
	    if (dominated_by_p (CDI_POST_DOMINATORS,
				gimple_bb (call_stmt),
				single_succ (ENTRY_BLOCK_PTR_FOR_FN (fun))))
	      csum->m_inputs[argidx].safe_to_import_accesses = true;
	  }

      }
  if (pdoms_calculated)
    {
      free_dominance_info (CDI_POST_DOMINATORS);
      remove_fake_exit_edges ();
      pop_cfun ();
    }

  vec_safe_reserve_exact (ifs->m_parameters, param_count);
  ifs->m_parameters->quick_grow_cleared (param_count);
  for (unsigned desc_index = 0; desc_index < param_count; desc_index++)
    {
      gensum_param_desc *s = &(*param_descriptions)[desc_index];
      isra_param_desc *d = &(*ifs->m_parameters)[desc_index];

      d->m_call_uses = s->m_call_uses;
      d->m_param_size_limit = s->param_size_limit;
      d->m_size_reached = s->nonarg_acc_size;
      d->m_locally_unused = s->m_locally_unused;
      d->m_split_candidate = s->m_split_candidate;
      d->m_by_ref = s->m_by_ref;

      for (gensum_param_access *acc = s->x_accesses;
	   acc;
	   acc = acc->next_sibling)
	copy_accesses_to_ipa_desc (acc, d);
    }
}

/* Intraprocedural part of IPA-SRA analysis.  Scan function body of NODE and
   create a summary structure describing IPA-SRA opportunities and constraints
   in it.  */

static void
ipa_sra_summarize_function (cgraph_node *node)
{
  if (dump_file)
    fprintf (dump_file, "Creating summary for %s/%i:\n", node->name (),
	     node->order);
  if (!ipa_sra_preliminary_function_checks (node))
    return;
  isra_func_summary *ifs = func_sums->get_create (node);
  ifs->m_candidate = true;
  tree ret = TREE_TYPE (TREE_TYPE (node->decl));
  ifs->m_returns_value = (TREE_CODE (ret) != VOID_TYPE);

  decl2desc = new hash_map<tree, gensum_param_desc *>;
  unsigned count = 0;
  for (tree parm = DECL_ARGUMENTS (node->decl); parm; parm = DECL_CHAIN (parm))
    count++;

  if (count > 0)
    {
      auto_vec<gensum_param_desc, 16> param_descriptions (count);
      param_descriptions.reserve_exact (count);
      param_descriptions.quick_grow_cleared (count);

      struct function *fun = DECL_STRUCT_FUNCTION (node->decl);
      if (create_parameter_descriptors (node, &param_descriptions))
	{
	  final_bbs = BITMAP_ALLOC (NULL);
	  bb_dereferences = XCNEWVEC (HOST_WIDE_INT,
				      by_ref_count
				      * last_basic_block_for_fn (fun));
	  aa_walking_limit = PARAM_VALUE (PARAM_IPA_MAX_AA_STEPS);
	  scan_function (node, fun);

	  if (dump_file)
	    {
	      dump_gensum_param_descriptors (dump_file, node->decl,
					     &param_descriptions);
	      fprintf (dump_file, "----------------------------------------\n");
	    }
	}
      process_scan_results (node, fun, ifs, &param_descriptions);

      if (dump_file)
	dump_isra_param_descriptors (dump_file, node->decl, ifs);
      if (bb_dereferences)
	{
	  free (bb_dereferences);
	  bb_dereferences = NULL;
	  BITMAP_FREE (final_bbs);
	  final_bbs = NULL;
	}
    }
  isra_analyze_all_outgoing_calls (node);

  delete decl2desc;
  decl2desc = NULL;
  if (dump_file)
    fprintf (dump_file, "\n\n");
  return;
}

/* Intraprocedural part of IPA-SRA analysis.  Scan bodies of all functions in
   this compilation unit and create summary structures describing IPA-SRA
   opportunities and constraints in them.  */

static void
ipa_sra_generate_summary (void)
{
  struct cgraph_node *node;

  gcc_checking_assert (!func_sums);
  gcc_checking_assert (!call_sums);
  func_sums
    = (new (ggc_cleared_alloc <ipa_sra_function_summaries> ())
       ipa_sra_function_summaries (symtab, true));
  call_sums = new ipa_sra_call_summaries (symtab);

  FOR_EACH_FUNCTION_WITH_GIMPLE_BODY (node)
    ipa_sra_summarize_function (node);
  return;
}

/* Write intraproceural analysis information about E and all of its outgoing
   edges into a stream for LTO WPA.  */

static void
isra_write_edge_summary (output_block *ob, cgraph_edge *e)
{
  isra_call_summary *csum = call_sums->get (e);
  unsigned input_count = csum->m_inputs.length ();
  streamer_write_uhwi (ob, input_count);
  for (unsigned i = 0; i < input_count; i++)
    {
      isra_param_flow *ipf = &csum->m_inputs[i];
      streamer_write_hwi (ob, ipf->length);
      bitpack_d bp = bitpack_create (ob->main_stream);
      for (int j = 0; j < ipf->length; j++)
	bp_pack_value (&bp, ipf->inputs[j], 8);
      bp_pack_value (&bp, ipf->aggregate_pass_through, 1);
      bp_pack_value (&bp, ipf->pointer_pass_through, 1);
      bp_pack_value (&bp, ipf->safe_to_import_accesses, 1);
      streamer_write_bitpack (&bp);
      streamer_write_uhwi (ob, ipf->param_number);
      streamer_write_uhwi (ob, ipf->unit_offset);
      streamer_write_uhwi (ob, ipf->unit_size);
    }
  bitpack_d bp = bitpack_create (ob->main_stream);
  bp_pack_value (&bp, csum->m_return_ignored, 1);
  bp_pack_value (&bp, csum->m_return_returned, 1);
  bp_pack_value (&bp, csum->m_bit_aligned_arg, 1);
  streamer_write_bitpack (&bp);
}

/* Write intraproceural analysis information about NODE and all of its outgoing
   edges into a stream for LTO WPA.  */

static void
isra_write_node_summary (output_block *ob, cgraph_node *node)
{
  isra_func_summary *ifs = func_sums->get (node);
  lto_symtab_encoder_t encoder = ob->decl_state->symtab_node_encoder;
  int node_ref = lto_symtab_encoder_encode (encoder, node);
  streamer_write_uhwi (ob, node_ref);

  unsigned param_desc_count = vec_safe_length (ifs->m_parameters);
  streamer_write_uhwi (ob, param_desc_count);
  for (unsigned i = 0; i < param_desc_count; i++)
    {
      isra_param_desc *desc = &(*ifs->m_parameters)[i];
      unsigned access_count = vec_safe_length (desc->m_accesses);
      streamer_write_uhwi (ob, access_count);
      for (unsigned j = 0; j < access_count; j++)
	{
	  param_access *acc = (*desc->m_accesses)[j];
	  stream_write_tree (ob, acc->type, true);
	  stream_write_tree (ob, acc->alias_ptr_type, true);
	  streamer_write_uhwi (ob, acc->unit_offset);
	  streamer_write_uhwi (ob, acc->unit_size);
	  bitpack_d bp = bitpack_create (ob->main_stream);
	  bp_pack_value (&bp, acc->definitive, 1);
	  bp_pack_value (&bp, acc->check_overlaps, 1);
	  streamer_write_bitpack (&bp);
	}
      streamer_write_hwi (ob, desc->m_call_uses);
      gcc_assert (desc->m_scc_uses == 0);
      streamer_write_uhwi (ob, desc->m_param_size_limit);
      streamer_write_uhwi (ob, desc->m_size_reached);
      bitpack_d bp = bitpack_create (ob->main_stream);
      bp_pack_value (&bp, desc->m_locally_unused, 1);
      bp_pack_value (&bp, desc->m_split_candidate, 1);
      bp_pack_value (&bp, desc->m_by_ref, 1);
      streamer_write_bitpack (&bp);
    }
  bitpack_d bp = bitpack_create (ob->main_stream);
  bp_pack_value (&bp, ifs->m_candidate, 1);
  bp_pack_value (&bp, ifs->m_returns_value, 1);
  bp_pack_value (&bp, ifs->m_return_ignored, 1);
  gcc_assert (!ifs->m_queued);
  streamer_write_bitpack (&bp);

  for (cgraph_edge *e = node->callees; e; e = e->next_callee)
    isra_write_edge_summary (ob, e);
  for (cgraph_edge *e = node->indirect_calls; e; e = e->next_callee)
    isra_write_edge_summary (ob, e);
}

/* Write intraproceural analysis information into a stream for LTO WPA.  */

static void
ipa_sra_write_summary (void)
{
  if (!func_sums || !call_sums)
    return;

  struct output_block *ob = create_output_block (LTO_section_ipa_sra);
  lto_symtab_encoder_t encoder = ob->decl_state->symtab_node_encoder;
  ob->symbol = NULL;

  unsigned int count = 0;
  lto_symtab_encoder_iterator lsei;
  for (lsei = lsei_start_function_in_partition (encoder);
       !lsei_end_p (lsei);
       lsei_next_function_in_partition (&lsei))
    {
      cgraph_node *node = lsei_cgraph_node (lsei);
      if (node->has_gimple_body_p ()
	  && func_sums->get (node) != NULL)
	count++;
    }
  streamer_write_uhwi (ob, count);

  /* Process all of the functions.  */
  for (lsei = lsei_start_function_in_partition (encoder); !lsei_end_p (lsei);
       lsei_next_function_in_partition (&lsei))
    {
      cgraph_node *node = lsei_cgraph_node (lsei);
      if (node->has_gimple_body_p ()
	  && func_sums->get (node) != NULL)
        isra_write_node_summary (ob, node);
    }
  streamer_write_char_stream (ob->main_stream, 0);
  produce_asm (ob, NULL);
  destroy_output_block (ob);
}

/* Read intraproceural analysis information about E and all of its outgoing
   edges into a stream for LTO WPA.  */

static void
isra_read_edge_summary (struct lto_input_block *ib, cgraph_edge *cs)
{
  isra_call_summary *csum = call_sums->get_create (cs);
  unsigned input_count = streamer_read_uhwi (ib);
  csum->init_inputs (input_count);
  for (unsigned i = 0; i < input_count; i++)
    {
      isra_param_flow *ipf = &csum->m_inputs[i];
      ipf->length = streamer_read_hwi (ib);
      bitpack_d bp = streamer_read_bitpack (ib);
      for (int j = 0; j < ipf->length; j++)
	ipf->inputs[j] = bp_unpack_value (&bp, 8);
      ipf->aggregate_pass_through = bp_unpack_value (&bp, 1);
      ipf->pointer_pass_through = bp_unpack_value (&bp, 1);
      ipf->safe_to_import_accesses = bp_unpack_value (&bp, 1);
      ipf->param_number = streamer_read_uhwi (ib);
      ipf->unit_offset = streamer_read_uhwi (ib);
      ipf->unit_size = streamer_read_uhwi (ib);
    }
  bitpack_d bp = streamer_read_bitpack (ib);
  csum->m_return_ignored = bp_unpack_value (&bp, 1);
  csum->m_return_returned = bp_unpack_value (&bp, 1);
  csum->m_bit_aligned_arg = bp_unpack_value (&bp, 1);
}

/* Read intraproceural analysis information about NODE and all of its outgoing
   edges into a stream for LTO WPA.  */

static void
isra_read_node_info (struct lto_input_block *ib, cgraph_node *node,
		     struct data_in *data_in)
{
  isra_func_summary *ifs = func_sums->get_create (node);
  unsigned param_desc_count = streamer_read_uhwi (ib);
  if (param_desc_count > 0)
    {
      vec_safe_reserve_exact (ifs->m_parameters, param_desc_count);
      ifs->m_parameters->quick_grow_cleared (param_desc_count);
    }
  for (unsigned i = 0; i < param_desc_count; i++)
    {
      isra_param_desc *desc = &(*ifs->m_parameters)[i];
      unsigned access_count = streamer_read_uhwi (ib);
      for (unsigned j = 0; j < access_count; j++)
	{
	  param_access *acc = ggc_cleared_alloc<param_access> ();
	  acc->type = stream_read_tree (ib, data_in);
	  acc->alias_ptr_type = stream_read_tree (ib, data_in);
	  acc->unit_offset = streamer_read_uhwi (ib);
	  acc->unit_size = streamer_read_uhwi (ib);
	  bitpack_d bp = streamer_read_bitpack (ib);
	  acc->definitive = bp_unpack_value (&bp, 1);
	  acc->check_overlaps = bp_unpack_value (&bp, 1);
	  vec_safe_push (desc->m_accesses, acc);
	}
      desc->m_call_uses = streamer_read_hwi (ib);
      desc->m_scc_uses = 0;
      desc->m_param_size_limit = streamer_read_uhwi (ib);
      desc->m_size_reached = streamer_read_uhwi (ib);
      bitpack_d bp = streamer_read_bitpack (ib);
      desc->m_locally_unused = bp_unpack_value (&bp, 1);
      desc->m_split_candidate = bp_unpack_value (&bp, 1);
      desc->m_by_ref = bp_unpack_value (&bp, 1);
    }
  bitpack_d bp = streamer_read_bitpack (ib);
  ifs->m_candidate = bp_unpack_value (&bp, 1);
  ifs->m_returns_value = bp_unpack_value (&bp, 1);
  ifs->m_return_ignored = bp_unpack_value (&bp, 1);
  ifs->m_queued = 0;

  for (cgraph_edge *e = node->callees; e; e = e->next_callee)
    isra_read_edge_summary (ib, e);
  for (cgraph_edge *e = node->indirect_calls; e; e = e->next_callee)
    isra_read_edge_summary (ib, e);
}

/* Read IPA-SRA summaries from a section in file FILE_DATA of length LEN with
   data DATA.  TODO: This function was copied almost verbatim from ipa-prop.c,
   that cannot be right.  */

static void
isra_read_summary_section (struct lto_file_decl_data *file_data,
			   const char *data, size_t len)
{
  const struct lto_function_header *header =
    (const struct lto_function_header *) data;
  const int cfg_offset = sizeof (struct lto_function_header);
  const int main_offset = cfg_offset + header->cfg_size;
  const int string_offset = main_offset + header->main_size;
  struct data_in *data_in;
  unsigned int i;
  unsigned int count;

  lto_input_block ib_main ((const char *) data + main_offset,
			   header->main_size, file_data->mode_table);

  data_in =
    lto_data_in_create (file_data, (const char *) data + string_offset,
			header->string_size, vNULL);
  count = streamer_read_uhwi (&ib_main);

  for (i = 0; i < count; i++)
    {
      unsigned int index;
      struct cgraph_node *node;
      lto_symtab_encoder_t encoder;

      index = streamer_read_uhwi (&ib_main);
      encoder = file_data->symtab_node_encoder;
      node = dyn_cast<cgraph_node *> (lto_symtab_encoder_deref (encoder,
								index));
      gcc_assert (node->definition);
      isra_read_node_info (&ib_main, node, data_in);
    }
  lto_free_section_data (file_data, LTO_section_ipa_sra, NULL, data,
			 len);
  lto_data_in_delete (data_in);
}

/* Read intraproceural analysis information into a stream for LTO WPA.  */

static void
ipa_sra_read_summary (void)
{
  struct lto_file_decl_data **file_data_vec = lto_get_file_decl_data ();
  struct lto_file_decl_data *file_data;
  unsigned int j = 0;

  gcc_checking_assert (!func_sums);
  gcc_checking_assert (!call_sums);
  func_sums
    = (new (ggc_cleared_alloc <ipa_sra_function_summaries> ())
       ipa_sra_function_summaries (symtab, true));
  call_sums = new ipa_sra_call_summaries (symtab);

  while ((file_data = file_data_vec[j++]))
    {
      size_t len;
      const char *data = lto_get_section_data (file_data, LTO_section_ipa_sra,
					       NULL, &len);
      if (data)
        isra_read_summary_section (file_data, data, len);
    }
}

/* Dump all IPA-SRA summary data for all cgraph nodes and edges to file F.  */

static void
ipa_sra_dump_all_summaries (FILE *f)
{
  cgraph_node *node;
  FOR_EACH_FUNCTION_WITH_GIMPLE_BODY (node)
    {
      fprintf (f, "\nSummary for node %s:\n", node->dump_name ());

      isra_func_summary *ifs = func_sums->get (node);
      if (!ifs)
	{
	  fprintf (f, "  Function does not have any associated IPA-SRA "
		   "summary\n");
	  continue;
	}
      if (!ifs->m_candidate)
	{
	  fprintf (f, "  Not a candidate function\n");
	  continue;
	}
      if (ifs->m_returns_value)
	  fprintf (f, "  Returns value\n");
      if (vec_safe_is_empty (ifs->m_parameters))
	fprintf (f, "  No parameter information. \n");
      else
	for (unsigned i = 0; i < ifs->m_parameters->length (); ++i)
	  {
	    fprintf (f, "  Descriptor for parameter %i:\n", i);
	    dump_isra_param_descriptor (f, &(*ifs->m_parameters)[i]);
	  }
      fprintf (f, "\n");

      struct cgraph_edge *cs;
      for (cs = node->callees; cs; cs = cs->next_callee)
	{
	  fprintf (f, "  Summary for edge %s->%s:\n", cs->caller->dump_name (),
		   cs->callee->dump_name ());
	  isra_call_summary *csum = call_sums->get (cs);
	  if (csum)
	    csum->dump (f);
	  else
	    fprintf (f, "    Call summary is MISSING!\n");
	}

    }
  fprintf (f, "\n\n");
}

/* Perform function-scope viability tests that can be only made at IPA level
   and return false if the function is deemed unsuitable for IPA-SRA.  */

static bool
ipa_sra_ipa_function_checks (cgraph_node *node)
{
  if (!node->can_be_local_p ())
    {
      if (dump_file)
	fprintf (dump_file, "Function %s disqualified because it cannot be "
		 "made local.\n", node->dump_name ());
      return false;
    }
  if (!node->local.can_change_signature)
    {
      if (dump_file)
	fprintf (dump_file, "Function can not change signature.\n");
      return false;
    }

  return true;
}

/* Issues found out by check_callers_for_issues.  */

struct caller_issues
{
  /* There is a thunk among callers.  */
  bool thunk;
  /* Call site with no available information.  */
  bool unknown_callsite;
  /* There is a bit-aligned load in one of non- */
  bool bit_aligned_argument;
};

/* Worker for call_for_symbol_and_aliases, set any flags of passed caller_issues
   that apply.  */

static bool
check_for_caller_issues (struct cgraph_node *node, void *data)
{
  struct caller_issues *issues = (struct caller_issues *) data;

  for (cgraph_edge *cs = node->callers; cs; cs = cs->next_caller)
    {
      if (cs->caller->thunk.thunk_p)
	{
	  issues->thunk = true;
	  /* TODO: We should be able to process at least some types of
	     thunks.  */
	  return true;
	}

      isra_call_summary *csum = call_sums->get (cs);
      if (!csum)
	{
	  issues->unknown_callsite = true;
	  return true;
	}

      if (csum->m_bit_aligned_arg)
	issues->bit_aligned_argument = true;
    }
  return false;
}

/* Look at all incoming edges to NODE, including aliases and thunks and look
   for problems.  Return true if NODE type should not be modified at all.  */

static bool
check_all_callers_for_issues (cgraph_node *node)
{
  struct caller_issues issues;
  memset (&issues, 0, sizeof (issues));

  node->call_for_symbol_and_aliases (check_for_caller_issues, &issues, true);
  if (issues.unknown_callsite)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "A call of %s has not been analyzed.  Disabling "
		 "all modifications.\n", node->dump_name ());
      return true;
    }
  /* TODO: We should be able to process at least some types of thunks.  */
  if (issues.thunk)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "A call of %s is through thunk, which are not"
		 " handled yet.  Disabling all modifications.\n",
		 node->dump_name ());
      return true;
    }

  if (issues.bit_aligned_argument)
    {
      /* Let's only remove parameters from such functions.  TODO: We could
	 only prevent splitting the problematic parameters if anybody thinks
	 it is worth it.  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "A call of %s has bit-alinged aggregate argument,"
		 " disabling parameter splitting.\n", node->dump_name ());

      isra_func_summary *ifs = func_sums->get (node);
      gcc_checking_assert (ifs);
      unsigned param_count = vec_safe_length (ifs->m_parameters);
      for (unsigned i = 0; i < param_count; i++)
	(*ifs->m_parameters)[i].m_split_candidate = false;
    }
  return false;
}

/* Count the number of times formal parameters feed into an actual argument of
   a call within the same SCC.  */

static void
count_param_scc_uses (cgraph_edge *cs)
{
  isra_func_summary *from_ifs = func_sums->get (cs->caller);
  gcc_checking_assert (from_ifs);
  if (!from_ifs->m_parameters)
    return;
  isra_call_summary *csum = call_sums->get (cs);
  gcc_checking_assert (csum);
  unsigned args_count = csum->m_inputs.length ();

  enum availability availability;
  cgraph_node *callee = cs->callee->function_symbol (&availability);
  isra_func_summary *to_ifs = func_sums->get (callee);
  if (!to_ifs || !to_ifs->m_candidate
      || vec_safe_is_empty (to_ifs->m_parameters))
    return;

  for (unsigned i = 0; i < args_count; i++)
    {
      isra_param_flow *ipf = &csum->m_inputs[i];
      for (int j = 0; j < ipf->length; j++)
	(*from_ifs->m_parameters)[ipf->inputs[j]].m_scc_uses++;

      if (ipf->aggregate_pass_through)
	(*from_ifs->m_parameters)[ipf->param_number].m_scc_uses++;
    }
}

/* Find the access with corresponding OFFSET and SIZE among accesses in
   PARAM_DESC and return it or NULL if such an access is not there.  */

static param_access *
find_param_access (isra_param_desc *param_desc, unsigned offset, unsigned size)
{
  unsigned pclen = vec_safe_length (param_desc->m_accesses);

  /* The search is linear but the number of stored accesses is bound by
     PARAM_IPA_SRA_MAX_REPLACEMENTS, so most probably 8.  */

  for (unsigned i = 0; i < pclen; i++)
    if ((*param_desc->m_accesses)[i]->unit_offset == offset
	&& (*param_desc->m_accesses)[i]->unit_size == size)
      return (*param_desc->m_accesses)[i];

  return NULL;
}

/* Return iff the total size of definite replacement SIZE would violate the
   limit set for it in PARAM.  */

static bool
size_would_violate_limit_p (isra_param_desc *desc, unsigned size)
{
  unsigned limit = desc->m_param_size_limit;
  if (size > limit
      || (!desc->m_by_ref && size == limit))
    return true;
  return false;
}

/* Increase reached size of DESC by SIZE or disqualify it if it would violate
   the set limit.  */

static void
bump_reached_size (isra_param_desc *desc, unsigned size)
{
  unsigned after = desc->m_size_reached + size;
  if (size_would_violate_limit_p (desc, after))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "    ...size limit reached, disqualifying "
		 "candidate\n");
      desc->m_split_candidate = false;
      return;
    }
  desc->m_size_reached = after;
}

/* Take all actions required to deal with indirect call edge CS, for both
   parameter removal and splitting.  */

static void
process_indirect_edge (cgraph_edge *cs)
{
  isra_func_summary *from_ifs = func_sums->get (cs->caller);
  gcc_checking_assert (from_ifs);
  isra_call_summary *csum = call_sums->get (cs);
  gcc_checking_assert (csum);
  unsigned args_count = csum->m_inputs.length ();

  for (unsigned i = 0; i < args_count; i++)
    {
      isra_param_flow *ipf = &csum->m_inputs[i];
      for (int j = 0; j < ipf->length; j++)
	{
	  int input_idx = ipf->inputs[j];
	  (*from_ifs->m_parameters)[input_idx].m_locally_unused = false;
	  (*from_ifs->m_parameters)[input_idx].m_split_candidate = false;
	}

      if (ipf->pointer_pass_through)
        {
          isra_param_desc *param_desc
            = &(*from_ifs->m_parameters)[ipf->param_number];
          param_desc->m_split_candidate = false;
        }
      if (ipf->aggregate_pass_through)
	{
	  isra_param_desc *param_desc
	    = &(*from_ifs->m_parameters)[ipf->param_number];

	  param_desc->m_locally_unused = false;
	  if (!param_desc->m_split_candidate)
	    continue;
	  gcc_assert (!param_desc->m_by_ref);
	  param_access *pacc = find_param_access (param_desc, ipf->unit_offset,
						  ipf->unit_size);
	  gcc_checking_assert (pacc);
	  bump_reached_size (param_desc, pacc->unit_size);
	  pacc->definitive = true;
	  ipf->aggregate_pass_through = false;
	}
    }
}

/* Propagate parameter removal information through cross-SCC edge CS,
   i.e. decrease the use count in the caller parameter descriptor for each use
   in this call.  */

static void
param_removal_cross_scc_edge (cgraph_edge *cs)
{
  enum availability availability;
  cgraph_node *callee = cs->callee->function_symbol (&availability);
  isra_func_summary *to_ifs = func_sums->get (callee);
  if (!to_ifs || !to_ifs->m_candidate
      || vec_safe_is_empty (to_ifs->m_parameters))
    return;
  isra_func_summary *from_ifs = func_sums->get (cs->caller);
  gcc_checking_assert (from_ifs);

  isra_call_summary *csum = call_sums->get (cs);
  gcc_checking_assert (csum);
  unsigned args_count = csum->m_inputs.length ();
  unsigned param_count = vec_safe_length (to_ifs->m_parameters);

  for (unsigned i = 0; (i < args_count) && (i < param_count); i++)
    {
      isra_param_desc *dest_desc = &(*to_ifs->m_parameters)[i];
      if (dest_desc->m_locally_unused
	  && (dest_desc->m_call_uses == dest_desc->m_scc_uses))
	{
	  isra_param_flow *ipf = &csum->m_inputs[i];
	  for (int j = 0; j < ipf->length; j++)
	    {
	      int input_idx = ipf->inputs[j];
	      if ((*from_ifs->m_parameters)[input_idx].m_locally_unused)
		(*from_ifs->m_parameters)[input_idx].m_call_uses--;
	    }

	  if (ipf->aggregate_pass_through
	      && (*from_ifs->m_parameters)[ipf->param_number].m_locally_unused)
	    (*from_ifs->m_parameters)[ipf->param_number].m_call_uses--;
	}
    }
}

/* Unless it is already there, push NODE which is also described by IFS to
   STACK.  */

static void
isra_push_node_to_stack (cgraph_node *node, isra_func_summary *ifs,
		    vec<cgraph_node *> *stack)
{
  if (!ifs->m_queued)
    {
      ifs->m_queued = true;
      stack->safe_push (node);
    }
}

/* If parameter with index INPUT_IDX is marked as locally unused, mark it as
   used and push CALLER on STACK.  */

static void
isra_mark_caller_param_used (isra_func_summary *from_ifs, int input_idx,
			     cgraph_node *caller, vec<cgraph_node *> *stack)
{
  if ((*from_ifs->m_parameters)[input_idx].m_locally_unused)
    {
      (*from_ifs->m_parameters)[input_idx].m_locally_unused = false;
      isra_push_node_to_stack (caller, from_ifs, stack);
    }
}


/* Propagate information that any parameter is not used only locally within a
   SCC accross CS to the caller, which must be in the same SCC as the
   callee. Push any callers that need to be re-processed to STACK.  */

static void
propagate_nonlocal_across_edge (cgraph_edge *cs, vec<cgraph_node *> *stack)
{
  isra_func_summary *from_ifs = func_sums->get (cs->caller);
  if (!from_ifs || vec_safe_is_empty (from_ifs->m_parameters))
    return;

  isra_call_summary *csum = call_sums->get (cs);
  gcc_checking_assert (csum);
  unsigned args_count = csum->m_inputs.length ();
  enum availability availability;
  cgraph_node *callee = cs->callee->function_symbol (&availability);
  isra_func_summary *to_ifs = func_sums->get (callee);

  unsigned param_count = to_ifs ? vec_safe_length (to_ifs->m_parameters) : 0;
  for (unsigned i = 0; i < args_count; i++)
    {
      if (i < param_count)
	{
	  isra_param_desc *dest_desc = &(*to_ifs->m_parameters)[i];

	  if (dest_desc->m_locally_unused)
	    {
	      int d = dest_desc->m_call_uses - dest_desc->m_scc_uses;
	      gcc_assert (d >= 0);
	      if (d == 0)
		/* The number of uses matches exactly the number of times this
		   parameter is passed to a function within SCC, so far so
		   good. */
		continue;
	    }
	}

      /* The argument is passed to a function which needs it (or there is a
	 weird parameter-argument count mismatch), we must mark the parameter
	 as used also in callers within this SCC.  */
      isra_param_flow *ipf = &csum->m_inputs[i];
      for (int j = 0; j < ipf->length; j++)
	{
	  int input_idx = ipf->inputs[j];
	  isra_mark_caller_param_used (from_ifs, input_idx, cs->caller, stack);
	}
      if (ipf->aggregate_pass_through
	  && (*from_ifs->m_parameters)[ipf->param_number].m_locally_unused)
	isra_mark_caller_param_used (from_ifs, ipf->param_number,
				     cs->caller, stack);
    }
}

/* Propagate information that any parameter is not used only locally within a
   SCC to all callers of NODE that are in the same SCC. Push any callers that
   need to be re-processed to STACK.  */

static bool
propagate_nonarg_to_css_callers (cgraph_node *node, void *data)
{
  vec<cgraph_node *> *stack = (vec<cgraph_node *> *) data;
  cgraph_edge *cs;
  for (cs = node->callers; cs; cs = cs->next_caller)
    if (ipa_edge_within_scc (cs))
      propagate_nonlocal_across_edge (cs, stack);
  return false;
}

/* Return true iff all definitive accesses in ARG_DESC are also present as
   definitive accesses in PARAM_DESC.  */

static bool
all_callee_accesses_present_p (isra_param_desc *param_desc,
			       isra_param_desc *arg_desc)
{
  unsigned aclen = vec_safe_length (arg_desc->m_accesses);
  for (unsigned j = 0; j < aclen; j++)
    {
      param_access *argacc = (*arg_desc->m_accesses)[j];
      if (!argacc->definitive)
	continue;
      param_access *pacc = find_param_access (param_desc, argacc->unit_offset,
					      argacc->unit_size);
      if (!pacc || !pacc->definitive)
	return false;
    }
  return true;
}

/* Type internal to function pull_accesses_from_callee.  Unfortunately gcc 4.8
   does not allow intantiating an auto_vec with a type defined within a
   function.  */
enum acc_prop_kind {ACC_PROP_DONT, ACC_PROP_COPY, ACC_PROP_DEFINITIVE};


/* Attempt to propagate all definite accesses from ARG_DESC to PARAM_DESC, if
   they would not violate some constraint there.  If successful, return NULL,
   otherwise return the string reason for failure (which can be written to the
   dump file).  DELTA_OFFSET is the known offset of the actual argument withing
   the formal parameter (so of ARG_DESCS within PARAM_DESCS), ARG_SIZE is the
   size of the actual argument or zero, if not known. In case of success, set
   *CHANGE_P to true if propagation actually changed anything.  */

static const char *
pull_accesses_from_callee (isra_param_desc *param_desc,
			   isra_param_desc *arg_desc,
			   unsigned delta_offset, unsigned arg_size,
			   bool *change_p)
{
  unsigned pclen = vec_safe_length (param_desc->m_accesses);
  unsigned aclen = vec_safe_length (arg_desc->m_accesses);
  unsigned prop_count = 0;
  unsigned prop_size = 0;
  bool change = false;

  auto_vec <enum acc_prop_kind, 8> prop_kinds (aclen);
  for (unsigned j = 0; j < aclen; j++)
    {
      param_access *argacc = (*arg_desc->m_accesses)[j];
      prop_kinds.safe_push (ACC_PROP_DONT);

      if (arg_size > 0
	  && argacc->unit_offset + argacc->unit_size > arg_size)
	return "callee access outsize size boundary";

      if (!argacc->definitive)
	continue;

      unsigned offset = argacc->unit_offset + delta_offset;
      /* Given that accesses are initially stored according to increasing
	 offset and decreasing size in case of equal offsets, the following
	 searches could be written more efficiently (if we kept the ordering
	 when copying). But the number of accesses is capped at
	 PARAM_IPA_SRA_MAX_REPLACEMENTS (so most likely 8) and the code gets
	 messy quickly, so let's improve on that only if necessary.  */

      bool exact_match = false;
      for (unsigned i = 0; i < pclen; i++)
	{
	  /* Check for overlaps.  */
	  param_access *pacc = (*param_desc->m_accesses)[i];
	  if (pacc->unit_offset == offset
	      && pacc->unit_size == argacc->unit_size)
	    {
	      if (argacc->alias_ptr_type != pacc->alias_ptr_type
		  || !types_compatible_p (argacc->type, pacc->type))
		return "propagated access types would not match existing ones";

	      exact_match = true;
	      if (!pacc->definitive)
		{
		  prop_kinds[j] = ACC_PROP_DEFINITIVE;
		  prop_size += argacc->unit_size;
		  change = true;
		}
	      break;
	    }

	  if (offset < pacc->unit_offset + pacc->unit_size
	      && offset + argacc->unit_size > pacc->unit_offset)
	    {
	      /* None permissible with load or store accesses, possible to
		 fit into argument ones.  */
	      if (pacc->definitive
		  || offset < pacc->unit_offset
		  || (offset + argacc->unit_size
		      > pacc->unit_offset + pacc->unit_size))
		return "a propagated access would conflict in caller";
	    }
	}

      if (!exact_match)
	{
	  prop_kinds[j] = ACC_PROP_COPY;
	  prop_count++;
	  prop_size += argacc->unit_size;
	  change = true;
	}
    }

    if (!change)
      return NULL;

    if ((prop_count + pclen
	 > (unsigned) PARAM_VALUE (PARAM_IPA_SRA_MAX_REPLACEMENTS))
	|| size_would_violate_limit_p (param_desc,
				       param_desc->m_size_reached + prop_size))
      return "propagating accesses would violate the count or size limit";

  *change_p = true;
  for (unsigned j = 0; j < aclen; j++)
    {
      if (prop_kinds[j] == ACC_PROP_COPY)
	{
	  param_access *argacc = (*arg_desc->m_accesses)[j];

	  param_access *copy = ggc_cleared_alloc<param_access> ();
	  copy->unit_offset = argacc->unit_offset + delta_offset;
	  copy->unit_size = argacc->unit_size;
	  copy->type = argacc->type;
	  copy->alias_ptr_type = argacc->alias_ptr_type;
	  copy->definitive = true;
	  vec_safe_push (param_desc->m_accesses, copy);
	}
      else if (prop_kinds[j] == ACC_PROP_DEFINITIVE)
	{
	  param_access *argacc = (*arg_desc->m_accesses)[j];
	  param_access *csp
	    = find_param_access (param_desc, argacc->unit_offset + delta_offset,
				 argacc->unit_size);
	  csp->definitive = true;
	}
    }

  param_desc->m_size_reached += prop_size;

  return NULL;
}

/* Propagate parameter splitting information through call graph edge CS.
   Return true if any changes that might need to be propagated within SCCs have
   been made.  */

static bool
param_splitting_across_edge (cgraph_edge *cs)
{
  bool res = false;
  bool cross_scc = !ipa_edge_within_scc (cs);
  enum availability availability;
  cgraph_node *callee = cs->callee->function_symbol (&availability);
  isra_func_summary *from_ifs = func_sums->get (cs->caller);
  gcc_checking_assert (from_ifs && from_ifs->m_parameters);

  isra_call_summary *csum = call_sums->get (cs);
  gcc_checking_assert (csum);
  unsigned args_count = csum->m_inputs.length ();
  isra_func_summary *to_ifs = func_sums->get (callee);
  unsigned param_count
    = ((to_ifs && to_ifs->m_candidate)
       ? vec_safe_length (to_ifs->m_parameters)
       : 0);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Splitting accross %s->%s:\n",
	     cs->caller->dump_name (), callee->dump_name ());

  unsigned i;
  for (i = 0; (i < args_count) && (i < param_count); i++)
    {
      isra_param_desc *arg_desc = &(*to_ifs->m_parameters)[i];
      isra_param_flow *ipf = &csum->m_inputs[i];

      if (arg_desc->m_locally_unused && !arg_desc->m_call_uses)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "    ->%u: unused in callee\n", i);
	  ipf->pointer_pass_through = false;
	  continue;
	}

      if (ipf->pointer_pass_through)
	{
	  int idx = ipf->param_number;
	  isra_param_desc *param_desc = &(*from_ifs->m_parameters)[idx];
	  if (!param_desc->m_split_candidate)
	    continue;
	  gcc_assert (param_desc->m_by_ref);

	  if (!arg_desc->m_split_candidate || !arg_desc->m_by_ref)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  %u->%u: not candidate or not by "
			 "reference in callee\n", idx, i);
	      param_desc->m_split_candidate = false;
	      ipf->pointer_pass_through = false;
	      res = true;
	    }
	  else if (!ipf->safe_to_import_accesses)
	    {
	      if (!all_callee_accesses_present_p (param_desc, arg_desc))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "  %u->%u: cannot import accesses.\n",
			     idx, i);
		  param_desc->m_split_candidate = false;
		  ipf->pointer_pass_through = false;
		  res = true;

		}
	      else
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "  %u->%u: verified callee accesses "
			     "present.\n", idx, i);
		  if (cross_scc)
		    ipf->pointer_pass_through = false;
		}
	    }
	  else
	    {
	      const char *pull_failure
		= pull_accesses_from_callee (param_desc, arg_desc, 0, 0, &res);
	      if (pull_failure)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "  %u->%u: by_ref access pull "
			     "failed: %s.\n", idx, i, pull_failure);
		  param_desc->m_split_candidate = false;
		  ipf->pointer_pass_through = false;
		  res = true;
		}
	      else
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "  %u->%u: by_ref access pull "
			     "succeeded.\n", idx, i);
		  if (cross_scc)
		    ipf->pointer_pass_through = false;
		}
	    }
	}
      else if (ipf->aggregate_pass_through)
	{
	  int idx = ipf->param_number;
	  isra_param_desc *param_desc = &(*from_ifs->m_parameters)[idx];
	  if (!param_desc->m_split_candidate)
	    continue;
	  gcc_assert (!param_desc->m_by_ref);
	  param_access *pacc = find_param_access (param_desc, ipf->unit_offset,
						  ipf->unit_size);
	  gcc_checking_assert (pacc);

	  if (pacc->definitive)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  %u->%u: already definitive\n", idx, i);
	      ipf->aggregate_pass_through = false;
	    }
	  else if (!arg_desc->m_split_candidate || arg_desc->m_by_ref)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  %u->%u: not candidate or by "
			 "reference in callee\n", idx, i);
	      bump_reached_size (param_desc, pacc->unit_size);
	      pacc->definitive = true;
	      ipf->aggregate_pass_through = false;
	      res = true;
	    }
	  else
	    {
	      const char *pull_failure
		= pull_accesses_from_callee (param_desc, arg_desc,
					     ipf->unit_offset,
					     ipf->unit_size, &res);
	      if (pull_failure)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "  %u->%u: arg access pull "
			     "failed: %s.\n", idx, i, pull_failure);
		  bump_reached_size (param_desc, pacc->unit_size);
		  pacc->definitive = true;
		  res = true;
		  ipf->aggregate_pass_through = false;
		}
	      else
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "  %u->%u: arg access pull "
			     "succeeded.\n", idx, i);
		  if (cross_scc)
		    ipf->aggregate_pass_through = false;
		}
	    }
	}
    }

  /* Handle argument-parameter count mismatches. */
  for (; (i < args_count); i++)
    {
      isra_param_flow *ipf = &csum->m_inputs[i];

      if (ipf->pointer_pass_through || ipf->aggregate_pass_through)
	{
	  int idx = ipf->param_number;
	  isra_param_desc *param_desc = &(*from_ifs->m_parameters)[idx];
	  if (!param_desc->m_split_candidate)
	    continue;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  %u->%u: no corresponding formal parameter\n",
		     idx, i);
	  param_desc->m_split_candidate = false;
	  ipf->pointer_pass_through = false;
	  ipf->aggregate_pass_through = false;
	  res = true;
	}
    }
  return res;
}

/* Check for any overlaps of definite param accesses among splitting candidates
   and if any are found disqualify them and return true.  */

static bool
validate_splitting_overlaps (cgraph_node *node)
{
  bool res = false;
  isra_func_summary *ifs = func_sums->get (node);
  if (!ifs || !ifs->m_candidate)
    return res;
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Validating splits for %s\n", node->dump_name ());
  unsigned param_count = vec_safe_length (ifs->m_parameters);

  for (unsigned pidx = 0; pidx < param_count; pidx++)
    {
      isra_param_desc *desc = &(*ifs->m_parameters)[pidx];
      if (!desc->m_split_candidate
	  || (desc->m_locally_unused
	      && desc->m_call_uses == desc->m_scc_uses))
	continue;

      bool definitive_access_present = false;
      unsigned pclen = vec_safe_length (desc->m_accesses);
      for (unsigned i = 0; i < pclen; i++)
	{
	  param_access *a1 = (*desc->m_accesses)[i];

	  if (!a1->definitive)
	    continue;
	  definitive_access_present = true;
	  bool overlap = false;
	  for (unsigned j = i + 1; j < pclen; j++)
	    {
	      param_access *a2 = (*desc->m_accesses)[j];
	      if (a2->definitive
		  && a1->unit_offset < a2->unit_offset + a2->unit_size
		  && a1->unit_offset + a1->unit_size > a2->unit_offset)
		{
		  overlap = true;
		  break;
		}
	    }
	  if (overlap)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "Disqualifying parameter %u of %s"
			 "because of late discovered overlap\n",
			 pidx, node->dump_name ());
	      desc->m_split_candidate = false;
	      res = true;
	      /* !!! Remove after testing.  */
	      gcc_assert (a1->check_overlaps);
	      break;
	    }
	}
      /* !!? remove after testing?  */
      gcc_checking_assert (definitive_access_present);
    }
  return res;
}

/* Worker for call_for_symbol_and_aliases, look at all callers and if all their
   callers ignore the return value, or come from the same SCC and use the
   return value only to compute their return value, return false, otherwise
   return true.  */

static bool
propagate_unused_ret_first_stage (cgraph_node *node, void *)
{
  for (cgraph_edge *cs = node->callers; cs; cs = cs->next_caller)
    {
      isra_call_summary *csum = call_sums->get (cs);
      gcc_checking_assert (csum);
      if (csum->m_return_ignored)
	continue;
      if (!csum->m_return_returned)
	return true;

      isra_func_summary *from_ifs = func_sums->get (cs->caller);
      if (!from_ifs || !from_ifs->m_candidate)
	return true;

      if (!ipa_edge_within_scc (cs)
	  && !from_ifs->m_return_ignored)
	    return true;
    }

  return false;
}

/* Do finall processing of results of IPA propagation regarding NODE, clone it
   if appropriate.  */

static void
process_isra_node_results (cgraph_node *node,
			   hash_map<const char *, unsigned> *clone_num_suffixes)
{
  isra_func_summary *ifs = func_sums->get (node);
  if (!ifs)
    return;

  unsigned param_count = vec_safe_length (ifs->m_parameters);
  bool will_change_function = false;
  if (ifs->m_returns_value && ifs->m_return_ignored)
    will_change_function = true;
  else
    for (unsigned i = 0; i < param_count; i++)
      {
      isra_param_desc *desc = &(*ifs->m_parameters)[i];
      if ((desc->m_locally_unused
	   && desc->m_call_uses == desc->m_scc_uses)
	  || desc->m_split_candidate)
	{
	  will_change_function = true;
	  break;
	}
      }
  if (!will_change_function)
    return;

  if (dump_file)
    {
      fprintf (dump_file, "\nEvaluating analysis results for %s\n",
	       node->dump_name ());
      if (ifs->m_returns_value && ifs->m_return_ignored)
	fprintf (dump_file, "  Will remove return value.\n");
    }

  /* Currently IPA-SRA is the first IPA pass creating param_adjustments.  If
     that ever changes, we'll have to add logic to combine pre-existing
     adjustments with the modifications IPA-SRA wishes to make, similar to what
     is done in IPA-CP.  */
  gcc_assert (!node->clone.param_adjustments);
  vec<ipa_adjusted_param, va_gc> *new_params = NULL;
  for (unsigned parm_num = 0; parm_num < param_count; parm_num++)
    {
      isra_param_desc *desc = &(*ifs->m_parameters)[parm_num];
      if (desc->m_locally_unused
	  && desc->m_call_uses == desc->m_scc_uses)
	{
	  if (dump_file)
	    fprintf (dump_file, "  Will remove parameter %u\n", parm_num);
	  continue;
	}

      if (!desc->m_split_candidate)
	{
	  ipa_adjusted_param adj;
	  memset (&adj, 0, sizeof (adj));
	  adj.op = IPA_PARAM_OP_COPY;
	  adj.base_index = parm_num;
	  adj.prev_clone_index = parm_num;
	  vec_safe_push (new_params, adj);
	  continue;
	}

      if (dump_file)
	fprintf (dump_file, "  Will split parameter %u\n", parm_num);
      unsigned aclen = vec_safe_length (desc->m_accesses);
      for (unsigned j = 0; j < aclen; j++)
	{
	  param_access *pa = (*desc->m_accesses)[j];
	  if (!pa->definitive)
	    continue;
	  if (dump_file)
	    fprintf (dump_file, "    - component at byte offset %u, "
		     "size %u\n", pa->unit_offset, pa->unit_size);

	  ipa_adjusted_param adj;
	  memset (&adj, 0, sizeof (adj));
	  adj.op = IPA_PARAM_OP_SPLIT;
	  adj.base_index = parm_num;
	  adj.prev_clone_index = parm_num;
	  adj.param_prefix_index = IPA_PARAM_PREFIX_ISRA;
	  adj.reverse = false; 	/* FIXME: Really? */
	  adj.type = pa->type;
	  adj.alias_ptr_type = pa->alias_ptr_type;
	  adj.unit_offset = pa->unit_offset;
	  vec_safe_push (new_params, adj);
	}
    }
  ipa_param_adjustments *new_adjustments
    = (new (ggc_alloc <ipa_param_adjustments> ())
       ipa_param_adjustments (new_params, param_count,
			      ifs->m_returns_value && ifs->m_return_ignored));

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n  Created adjustments:\n");
      new_adjustments->dump (dump_file);
    }

  unsigned &suffix_counter = clone_num_suffixes->get_or_insert (
			       IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (
				 node->decl)));
  vec<cgraph_edge *> callers = node->collect_callers ();
  cgraph_node *new_node
    = node->create_virtual_clone (callers, NULL, new_adjustments, "isra",
				  suffix_counter);
  suffix_counter++;

  if (dump_file)
    fprintf (dump_file, "  Created new node %s\n", new_node->dump_name ());
  callers.release ();
}

/* Run the interprocedural part of IPA-SRA. */

static unsigned int
ipa_sra_analysis (void)
{
  if (dump_file)
    {
      fprintf (dump_file, "\n========== IPA-SRA IPA stage ==========\n");
      ipa_sra_dump_all_summaries (dump_file);
    }

  gcc_checking_assert (func_sums);
  gcc_checking_assert (call_sums);
  cgraph_node **order = XCNEWVEC (cgraph_node *, symtab->cgraph_count);
  auto_vec <cgraph_node *, 16> stack;
  int node_scc_count = ipa_reduced_postorder (order, true, NULL);

  /* One swoop from callees to callers for parameter removal and splitting.  */
  for (int i = 0; i < node_scc_count; i++)
    {
      cgraph_node *scc_rep = order[i];
      vec<cgraph_node *> cycle_nodes = ipa_get_nodes_in_cycle (scc_rep);
      unsigned j;

      /* Preliminary IPA function level checks and first step of parameter
	 removal.  */
      cgraph_node *v;
      FOR_EACH_VEC_ELT (cycle_nodes, j, v)
	{
	  isra_func_summary *ifs = func_sums->get (v);
	  if (!ifs)
	    continue;
	  if (!ifs->m_candidate)
	    {
	      gcc_checking_assert (vec_safe_is_empty (ifs->m_parameters));
	      continue;
	    }
	  if (!ipa_sra_ipa_function_checks (v)
	      || check_all_callers_for_issues (v))
	    {
	      ifs->zap ();
	      continue;
	    }

	  for (cgraph_edge *cs = v->indirect_calls; cs; cs = cs->next_callee)
	    process_indirect_edge (cs);
	  for (cgraph_edge *cs = v->callees; cs; cs = cs->next_callee)
	    if (ipa_edge_within_scc (cs))
	      count_param_scc_uses (cs);
	    else
	      param_removal_cross_scc_edge (cs);
	}

      /* Undoing optimistic assumptions for intra-SCC edges during parameter
	 removal.  */
      FOR_EACH_VEC_ELT (cycle_nodes, j, v)
	v->call_for_symbol_thunks_and_aliases (propagate_nonarg_to_css_callers,
					       &stack, true);

      while (!stack.is_empty ())
	{
	  cgraph_node *v = stack.pop ();
	  isra_func_summary *ifs = func_sums->get (v);
	  gcc_checking_assert (ifs && ifs->m_queued);
	  ifs->m_queued = false;

	  v->call_for_symbol_thunks_and_aliases
	    (propagate_nonarg_to_css_callers, &stack, true);
	}

      /* Parameter splitting.  */
      bool repeat_scc_propagation;
      do
	{
	  repeat_scc_propagation = false;
	  bool repeat_edge_propagation;
	  do
	    {
	      repeat_edge_propagation = false;
	      FOR_EACH_VEC_ELT (cycle_nodes, j, v)
		{
		  isra_func_summary *ifs = func_sums->get (v);
		  if (!ifs || !ifs->m_candidate || !ifs->m_parameters)
		    continue;
		  for (cgraph_edge *cs = v->callees; cs; cs = cs->next_callee)
		    if (param_splitting_across_edge (cs))
		      repeat_edge_propagation = true;
		}
	    }
	  while (repeat_edge_propagation);

	  FOR_EACH_VEC_ELT (cycle_nodes, j, v)
	    if (validate_splitting_overlaps (v))
	      repeat_scc_propagation = true;
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "\n");
	}
      while (repeat_scc_propagation);

      cycle_nodes.release ();
    }

  /* One swoop from caller to callees for result removal.  */
  for (int i = node_scc_count - 1; i >= 0 ; i--)
    {
      cgraph_node *scc_rep = order[i];
      vec<cgraph_node *> cycle_nodes = ipa_get_nodes_in_cycle (scc_rep);
      unsigned j;

      cgraph_node *v;
      FOR_EACH_VEC_ELT (cycle_nodes, j, v)
	{
	  isra_func_summary *ifs = func_sums->get (v);
	  if (!ifs || !ifs->m_candidate)
	    continue;

	  bool return_needed
	    = v->call_for_symbol_and_aliases (propagate_unused_ret_first_stage,
					      NULL, true);
	  ifs->m_return_ignored = !return_needed;
	  if (return_needed)
	    isra_push_node_to_stack (v, ifs, &stack);
	}

      while (!stack.is_empty ())
	{
	  cgraph_node *node = stack.pop ();
	  isra_func_summary *ifs = func_sums->get (node);
	  gcc_checking_assert (ifs && ifs->m_queued);
	  ifs->m_queued = false;

	  for (cgraph_edge *cs = node->callees; cs; cs = cs->next_callee)
	    if (ipa_edge_within_scc (cs)
		&& call_sums->get (cs)->m_return_returned)
	      {
		enum availability av;
		cgraph_node *callee = cs->callee->function_symbol (&av);
		isra_func_summary *to_ifs = func_sums->get (callee);
		if (to_ifs && to_ifs->m_return_ignored)
		  {
		    to_ifs->m_return_ignored = false;
		    isra_push_node_to_stack (callee, to_ifs, &stack);
		  }
	      }
	}
      cycle_nodes.release ();
    }

  ipa_free_postorder_info ();
  free (order);

  if (dump_file)
    {
      if (dump_flags & TDF_DETAILS)
	{
	  fprintf (dump_file, "\n========== IPA-SRA propagation final state "
		   " ==========\n");
	  ipa_sra_dump_all_summaries (dump_file);
	}
      fprintf (dump_file, "\n========== IPA-SRA decisions ==========\n");
    }

  hash_map<const char *, unsigned> *clone_num_suffixes
    = new hash_map<const char *, unsigned>;

  cgraph_node *node;
  FOR_EACH_FUNCTION_WITH_GIMPLE_BODY (node)
    process_isra_node_results (node, clone_num_suffixes);

  delete clone_num_suffixes;
  func_sums->release ();
  func_sums = NULL;
  call_sums->release ();
  call_sums = NULL;

  if (dump_file)
    fprintf (dump_file, "\n========== IPA SRA IPA analysis done "
	     "==========\n\n");
  return 0;
}


const pass_data pass_data_ipa_sra =
{
  IPA_PASS, /* type */
  "sra", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_IPA_SRA, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  ( TODO_dump_symtab | TODO_remove_functions ), /* todo_flags_finish */
};

class pass_ipa_sra : public ipa_opt_pass_d
{
public:
  pass_ipa_sra (gcc::context *ctxt)
    : ipa_opt_pass_d (pass_data_ipa_sra, ctxt,
		      ipa_sra_generate_summary, /* generate_summary */
		      ipa_sra_write_summary, /* write_summary */
		      ipa_sra_read_summary, /* read_summary */
		      NULL , /* write_optimization_summary */
		      NULL, /* read_optimization_summary */
		      NULL, /* stmt_fixup */
		      0, /* function_transform_todo_flags_start */
		      NULL, /* function_transform */
		      NULL) /* variable_transform */
  {}

  /* opt_pass methods: */
  virtual bool gate (function *)
    {
      /* FIXME: We should remove the optimize check after we ensure we never run
	 IPA passes when not optimizing.  */
      return (flag_ipa_sra && optimize);
    }

  virtual unsigned int execute (function *) { return ipa_sra_analysis (); }

}; // class pass_ipa_sra

} // anon namespace

ipa_opt_pass_d *
make_pass_ipa_sra (gcc::context *ctxt)
{
  return new pass_ipa_sra (ctxt);
}


#include "gt-ipa-sra.h"
