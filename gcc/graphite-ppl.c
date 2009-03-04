/* Gimple Represented as Polyhedra.
   Copyright (C) 2009 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <sebastian.pop@amd.com>
   and Tobias Grosser <grosser@fim.uni-passau.de>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "ppl_c.h"
#include "cloog/cloog.h"
#include "graphite-ppl.h"

/* Translates row ROW of the CloogMatrix MATRIX to a PPL Constraint.  */

static ppl_Constraint_t
cloog_matrix_to_ppl_constraint (CloogMatrix *matrix, int row)
{
  int j;
  ppl_Constraint_t cstr;
  ppl_Coefficient_t coef;
  ppl_Linear_Expression_t expr;
  ppl_dimension_type dim = matrix->NbColumns - 2;

  ppl_new_Coefficient (&coef);
  ppl_new_Linear_Expression_with_dimension (&expr, dim);

  for (j = 1; j < matrix->NbColumns - 1; j++)
    {
      ppl_assign_Coefficient_from_mpz_t (coef, matrix->p[row][j]);
      ppl_Linear_Expression_add_to_coefficient (expr, j - 1, coef);
    }

  ppl_assign_Coefficient_from_mpz_t (coef,
				     matrix->p[row][matrix->NbColumns - 1]);
  ppl_Linear_Expression_add_to_inhomogeneous (expr, coef);
  ppl_delete_Coefficient (coef);

  if (value_zero_p (matrix->p[row][0]))
    ppl_new_Constraint (&cstr, expr, PPL_CONSTRAINT_TYPE_EQUAL);
  else
    ppl_new_Constraint (&cstr, expr, PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL);

  ppl_delete_Linear_Expression (expr);
  return cstr;
}

/* Creates a PPL constraint system from MATRIX.  */

static void
new_Constraint_System_from_Cloog_Matrix (ppl_Constraint_System_t *pcs,
					 CloogMatrix *matrix)
{
  int i;

  ppl_new_Constraint_System (pcs);

  for (i = 0; i < matrix->NbRows; i++)
    {
      ppl_Constraint_t c = cloog_matrix_to_ppl_constraint (matrix, i);
      ppl_Constraint_System_insert_Constraint (*pcs, c);
      ppl_delete_Constraint (c);
    }
}

/* Creates a PPL Polyhedron from MATRIX.  */

void
new_NNC_Polyhedron_from_Cloog_Matrix (ppl_Polyhedron_t *ph,
				      CloogMatrix *matrix)
{
  ppl_Constraint_System_t cs;
  new_Constraint_System_from_Cloog_Matrix (&cs, matrix);
  ppl_new_NNC_Polyhedron_recycle_Constraint_System (ph, cs);
}

/* Counts the number of constraints in PCS.  */

static int
ppl_Constrain_System_number_of_constraints (ppl_const_Constraint_System_t pcs)
{
  ppl_Constraint_System_const_iterator_t cit, end;
  int num = 0;

  ppl_new_Constraint_System_const_iterator (&cit);
  ppl_new_Constraint_System_const_iterator (&end);

  for (ppl_Constraint_System_begin (pcs, cit),
        ppl_Constraint_System_end (pcs, end);
       !ppl_Constraint_System_const_iterator_equal_test (cit, end);
       ppl_Constraint_System_const_iterator_increment (cit))
    num++;

  ppl_delete_Constraint_System_const_iterator (cit);
  ppl_delete_Constraint_System_const_iterator (end);
  return num;
}

static void
oppose_constraint (CloogMatrix *m, int row)
{
  int k;

  /* Do not oppose the first column: it is the eq/ineq one.  */
  for (k = 1; k < m->NbColumns; k++)
    value_oppose (m->p[row][k], m->p[row][k]);
}

/* Inserts constraint CSTR at row ROW of matrix M.  */

void
insert_constraint_into_matrix (CloogMatrix *m, int row,
			       ppl_const_Constraint_t cstr)
{
  ppl_Coefficient_t c;
  ppl_dimension_type i, dim, nb_cols = m->NbColumns;

  ppl_Constraint_space_dimension (cstr, &dim);
  ppl_new_Coefficient (&c);

  for (i = 0; i < dim; i++)
    {
      ppl_Constraint_coefficient (cstr, i, c);
      ppl_Coefficient_to_mpz_t (c, m->p[row][i + 1]);
    }

  for (i = dim; i < nb_cols - 1; i++)
    value_set_si (m->p[row][i + 1], 0);

  ppl_Constraint_inhomogeneous_term  (cstr, c);
  ppl_Coefficient_to_mpz_t (c, m->p[row][nb_cols - 1]);
  value_set_si (m->p[row][0], 1);

  switch (ppl_Constraint_type (cstr))
    {
    case PPL_CONSTRAINT_TYPE_LESS_THAN:
      oppose_constraint (m, row);
    case PPL_CONSTRAINT_TYPE_GREATER_THAN:
      value_sub_int (m->p[row][nb_cols - 1],
		     m->p[row][nb_cols - 1], 1);
      break;

    case PPL_CONSTRAINT_TYPE_LESS_OR_EQUAL:
      oppose_constraint (m, row);
    case PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL:
      break;

    case PPL_CONSTRAINT_TYPE_EQUAL:
      value_set_si (m->p[row][0], 0);
      break;

    default:
      /* Not yet implemented.  */
      gcc_unreachable();
    }

  ppl_delete_Coefficient (c);
}

/* Creates a CloogMatrix from constraint system PCS.  */

static CloogMatrix *
new_Cloog_Matrix_from_ppl_Constraint_System (ppl_const_Constraint_System_t pcs)
{
  CloogMatrix *matrix;
  ppl_Constraint_System_const_iterator_t cit, end;
  ppl_dimension_type dim;
  int rows;
  int row = 0;

  rows = ppl_Constrain_System_number_of_constraints (pcs);
  ppl_Constraint_System_space_dimension (pcs, &dim);
  matrix = cloog_matrix_alloc (rows, dim + 2);
  ppl_new_Constraint_System_const_iterator (&cit);
  ppl_new_Constraint_System_const_iterator (&end);

  for (ppl_Constraint_System_begin (pcs, cit),
        ppl_Constraint_System_end (pcs, end);
       !ppl_Constraint_System_const_iterator_equal_test (cit, end);
       ppl_Constraint_System_const_iterator_increment (cit))
    {
      ppl_const_Constraint_t c;
      ppl_Constraint_System_const_iterator_dereference (cit, &c);
      insert_constraint_into_matrix (matrix, row, c);
      row++;
    }

  ppl_delete_Constraint_System_const_iterator (cit);
  ppl_delete_Constraint_System_const_iterator (end);

  return matrix;
}

/* Creates a CloogMatrix from polyhedron PH.  */

CloogMatrix *
new_Cloog_Matrix_from_ppl_Polyhedron (ppl_const_Polyhedron_t ph)
{
  ppl_const_Constraint_System_t pcs;
  CloogMatrix *res;

  ppl_Polyhedron_get_constraints (ph, &pcs);
  res = new_Cloog_Matrix_from_ppl_Constraint_System (pcs);

  return res;
}

/* Creates a CloogDomain from polyhedron PH.  */

CloogDomain *
new_Cloog_Domain_from_ppl_Polyhedron (ppl_const_Polyhedron_t ph)
{
  CloogMatrix *mat = new_Cloog_Matrix_from_ppl_Polyhedron (ph);
  CloogDomain *res = cloog_domain_matrix2domain (mat);
  cloog_matrix_free (mat);
  return res;
}

/* Set the inhomogeneous term of E to X.  */

static void
set_inhomogeneous (ppl_Linear_Expression_t e, int x)
{
  Value v0, v1;
  ppl_Coefficient_t c;

  value_init (v0);
  value_init (v1);
  ppl_new_Coefficient (&c);

  ppl_Linear_Expression_inhomogeneous_term (e, c);
  ppl_Coefficient_to_mpz_t (c, v1);
  value_oppose (v1, v1);
  value_set_si (v0, x);
  value_addto (v0, v0, v1);
  ppl_assign_Coefficient_from_mpz_t (c, v0);
  ppl_Linear_Expression_add_to_inhomogeneous (e, c);

  value_clear (v0);
  value_clear (v1);
  ppl_delete_Coefficient (c);
}

/* Set E[I] to X.  */

static void
set_coef (ppl_Linear_Expression_t e, ppl_dimension_type i, int x)
{
  Value v0, v1;
  ppl_Coefficient_t c;

  value_init (v0);
  value_init (v1);
  ppl_new_Coefficient (&c);

  ppl_Linear_Expression_coefficient (e, i, c);
  ppl_Coefficient_to_mpz_t (c, v1);
  value_oppose (v1, v1);
  value_set_si (v0, x);
  value_addto (v0, v0, v1);
  ppl_assign_Coefficient_from_mpz_t (c, v0);
  ppl_Linear_Expression_add_to_coefficient (e, i, c);

  value_clear (v0);
  value_clear (v1);
  ppl_delete_Coefficient (c);
}

/* Insert after X NB_NEW_DIMS empty dimensions into PH. 
   
   With x = 3 and nb_new_dims = 4

   |  d0 d1 d2 d3 d4
   
   is transformed to
   
   |  d0 d1 d2 x0 x1 x2 x3 d3 d4  */

void
ppl_insert_dimensions (ppl_Polyhedron_t ph, int x,
		       int nb_new_dims)
{
  ppl_dimension_type i, dim;
  ppl_dimension_type *map;
  ppl_dimension_type x_ppl, nb_new_dims_ppl;

  x_ppl = (ppl_dimension_type) x;
  nb_new_dims_ppl = (ppl_dimension_type) nb_new_dims;

  ppl_Polyhedron_space_dimension (ph, &dim);
  ppl_Polyhedron_add_space_dimensions_and_embed (ph, nb_new_dims);

  map = (ppl_dimension_type *) XNEWVEC (ppl_dimension_type, dim + nb_new_dims);
  for (i = 0; i < x_ppl; i++)
    map[i] = i;
  for (i = x_ppl; i < x_ppl + nb_new_dims_ppl; i++)
    map[i] = dim + i - x_ppl;
  for (i = x_ppl + nb_new_dims_ppl ; i < dim + nb_new_dims_ppl; i++)
    map[i] = i - nb_new_dims_ppl;

  ppl_Polyhedron_map_space_dimensions (ph, map, dim + nb_new_dims);
  free (map);
}

/* Based on the original polyhedron PH, returns a new polyhedron with
   an extra dimension placed at position LOOP + 1 that slices the
   dimension LOOP into strips of size STRIDE.  */

ppl_Polyhedron_t
ppl_strip_loop (ppl_Polyhedron_t ph, ppl_dimension_type loop, int stride)
{
  ppl_const_Constraint_System_t pcs;
  ppl_Constraint_System_const_iterator_t cit, end;
  ppl_const_Constraint_t cstr;
  ppl_Linear_Expression_t expr;
  int v;
  ppl_dimension_type dim;
  ppl_Polyhedron_t res;
  ppl_Coefficient_t c;
  Value val;

  value_init (val);
  ppl_new_Coefficient (&c);

  ppl_Polyhedron_space_dimension (ph, &dim);
  ppl_Polyhedron_get_constraints (ph, &pcs);

  /* Start from a copy of the constraints.  */
  ppl_new_NNC_Polyhedron_from_space_dimension (&res, dim + 1, 0);
  ppl_Polyhedron_add_constraints (res, pcs);

  /* Add an empty dimension for the strip loop.  */
  ppl_insert_dimensions (res, loop, 1);

  /* Identify the constraints that define the lower and upper bounds
     of the strip-mined loop, and add them to the strip loop.  */
  {
    ppl_Polyhedron_t tmp;

    ppl_new_NNC_Polyhedron_from_space_dimension (&tmp, dim + 1, 0);
    ppl_new_Constraint_System_const_iterator (&cit);
    ppl_new_Constraint_System_const_iterator (&end);

    for (ppl_Constraint_System_begin (pcs, cit),
	   ppl_Constraint_System_end (pcs, end);
	 !ppl_Constraint_System_const_iterator_equal_test (cit, end);
	 ppl_Constraint_System_const_iterator_increment (cit))
      {
	ppl_Constraint_System_const_iterator_dereference (cit, &cstr);
	ppl_new_Linear_Expression_from_Constraint (&expr, cstr);
	ppl_Linear_Expression_coefficient (expr, loop, c);
	ppl_delete_Linear_Expression (expr);
	ppl_Coefficient_to_mpz_t (c, val);
	v = value_get_si (val);

	if (0 < v || v < 0)
	  ppl_Polyhedron_add_constraint (tmp, cstr);
      }
    ppl_delete_Constraint_System_const_iterator (cit);
    ppl_delete_Constraint_System_const_iterator (end);

    ppl_insert_dimensions (tmp, loop + 1, 1);
    ppl_Polyhedron_get_constraints (tmp, &pcs);
    ppl_Polyhedron_add_constraints (res, pcs);
    ppl_delete_Polyhedron (tmp);
  }

  /* Lower bound of a tile starts at "stride * outer_iv".  */
  {
    ppl_Constraint_t new_cstr;
    ppl_new_Linear_Expression_with_dimension (&expr, dim + 1);

    set_coef (expr, loop + 1, 1);
    set_coef (expr, loop, -1 * stride);

    ppl_new_Constraint (&new_cstr, expr, PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL);
    ppl_delete_Linear_Expression (expr);
    ppl_Polyhedron_add_constraint (res, new_cstr);
    ppl_delete_Constraint (new_cstr);
  }

  /* Upper bound of a tile stops at "stride * outer_iv + stride - 1",
     or at the old upper bound that is not modified.  */
  {  
    ppl_Constraint_t new_cstr;
    ppl_new_Linear_Expression_with_dimension (&expr, dim + 1);

    set_coef (expr, loop + 1, -1);
    set_coef (expr, loop, stride);
    set_inhomogeneous (expr, stride - 1);

    ppl_new_Constraint (&new_cstr, expr, PPL_CONSTRAINT_TYPE_GREATER_OR_EQUAL);
    ppl_delete_Linear_Expression (expr);
    ppl_Polyhedron_add_constraint (res, new_cstr);
    ppl_delete_Constraint (new_cstr);
  }

  value_clear (val);
  ppl_delete_Coefficient (c);
  return res;
}

/* Lexicographically compares two linear expressions A and B and
   returns negative when A < B, 0 when A == B and positive when A > B.  */

int
ppl_lexico_compare_linear_expressions (ppl_Linear_Expression_t a,
				       ppl_Linear_Expression_t b)
{
  ppl_dimension_type min_length, length1, length2;
  ppl_dimension_type i;
  ppl_Coefficient_t c;
  int res;
  Value va, vb;

  ppl_Linear_Expression_space_dimension (a, &length1);
  ppl_Linear_Expression_space_dimension (b, &length2);
  ppl_new_Coefficient (&c);
  value_init (va);
  value_init (vb);

  if (length1 < length2)
    min_length = length1;
  else
    min_length = length2;

  for (i = 0; i < min_length; i++)
    {
      ppl_Linear_Expression_coefficient (a, i, c);
      ppl_Coefficient_to_mpz_t (c, va);
      ppl_Linear_Expression_coefficient (b, i, c);
      ppl_Coefficient_to_mpz_t (c, vb);
      res = value_compare (va, vb);

      if (res == 0)
	continue;

      value_clear (va);
      value_clear (vb);
      ppl_delete_Coefficient (c);
      return res;
    }

  value_clear (va);
  value_clear (vb);
  ppl_delete_Coefficient (c);
  return length1 - length2;
}

/* Print to FILE the polyhedron PH under its PolyLib matrix form.  */

void
ppl_print_polyhedron_matrix (FILE *file, ppl_Polyhedron_t ph)
{
  CloogMatrix *mat = new_Cloog_Matrix_from_ppl_Polyhedron (ph);
  cloog_matrix_print (file, mat);
  cloog_matrix_free (mat);
}

/* Print to STDERR the polyhedron PH under its PolyLib matrix form.  */

void
debug_ppl_polyhedron_matrix (ppl_Polyhedron_t ph)
{
  ppl_print_polyhedron_matrix (stderr, ph);
}

/* Read from FILE a polyhedron under PolyLib matrix form and return a
   PPL polyhedron object.  */

void
ppl_read_polyhedron_matrix (ppl_Polyhedron_t *ph, FILE *file)
{
  CloogMatrix *mat = cloog_matrix_read (file);
  new_NNC_Polyhedron_from_Cloog_Matrix (ph, mat);
  cloog_matrix_free (mat);
}
