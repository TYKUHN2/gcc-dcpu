/* { dg-options "-std=gnu99 -Wshadow" } */

typedef __sizeless_1 ta;
typedef __sizeless_2 tb;

/* Sizeless objects with global scope.  */

ta global_ta; /* { dg-error {sizeless variable 'global_ta' cannot have static storage duration} } */
static ta local_ta; /* { dg-error {sizeless variable 'local_ta' cannot have static storage duration} } */
extern ta ta_extern; /* { dg-error {sizeless variable 'ta_extern' cannot have static storage duration} } */
__thread ta tls_ta; /* { dg-error {sizeless variable 'tls_ta' cannot have static storage duration} } */
_Atomic ta atomic_ta; /* { dg-error {sizeless variable 'atomic_ta' cannot have static storage duration} } */

/* Pointers to sizeless types.  */

ta *global_ta_ptr;

/* Sizeless arguments and return values.  */

ta ext_produce_ta ();

/* Main tests for statements and expressions.  */

void
statements (int n)
{
  /* Local declarations.  */

  ta ta1, ta2;
  tb tb1;
  static ta local_static_ta; /* { dg-error {sizeless variable 'local_static_ta' cannot have static storage duration} } */

  /* Layout queries.  */

  sizeof (ta); /* { dg-error {invalid application of 'sizeof' to incomplete type} } */
  sizeof (ta1); /* { dg-error {invalid application of 'sizeof' to incomplete type} } */
  _Alignof (ta); /* { dg-error {invalid application of '(_Alignof|__alignof__)' to incomplete type} } */
  _Alignof (ta1); /* { dg-error {invalid application of '(_Alignof|__alignof__)' to incomplete type} } */

  /* Initialization.  */

  ta init_ta1 = ta1;
  ta init_ta2 = tb1; /* { dg-error {incompatible types when initializing type 'ta' using type 'tb'} } */
  ta init_ta3 = {};

  int initi_a = ta1; /* { dg-error {incompatible types when initializing type 'int' using type 'ta'} } */
  int initi_b = { ta1 }; /* { dg-error {incompatible types when initializing type 'int' using type 'ta'} } */

  /* Compound literals.  */

  (int) { ta1 }; /* { dg-error {incompatible types when initializing type 'int' using type 'ta'} } */

  /* Assignment.  */

  n = ta1; /* { dg-error {incompatible types when assigning to type 'int' from type 'ta'} } */

  ta1 = 0; /* { dg-error {incompatible types when assigning to type 'ta'[^\n]* from type 'int'} } */
  ta1 = ta2;
  ta1 = tb1; /* { dg-error {incompatible types when assigning to type 'ta'[^\n]* from type 'tb'} } */

  /* Casting.  */

  (void) ta1;

  /* Addressing and dereferencing.  */

  ta *ta_ptr = &ta1;

  /* Conditional expressions.  */

  0 ? ta1 : ta1;
  0 ? ta1 : tb1; /* { dg-error {type mismatch in conditional expression} } */
  0 ? ta1 : 0; /* { dg-error {type mismatch in conditional expression} } */
  0 ? 0 : ta1; /* { dg-error {type mismatch in conditional expression} } */
  0 ?: ta1; /* { dg-error {type mismatch in conditional expression} } */

  /* Generic associations.  */

  _Generic (ta1, default: 100);

  /* Statement expressions.  */

  ({ ta1; });
}
