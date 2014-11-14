/* Setting LOGICAL_OP_NON_SHORT_CIRCUIT to 0 leads to two conditional jumps
   when evaluating an && condition.  VRP is not able to optimize this.  */
/* { dg-do compile { target { ! { logical_op_short_circuit || { m68k*-*-* mmix*-*-* mep*-*-* bfin*-*-* v850*-*-* moxie*-*-* cris*-*-* m32c*-*-* fr30*-*-* mcore*-*-* powerpc*-*-* xtensa*-*-* hppa*-*-* } } } } } */
/* { dg-options "-O2 -fdump-tree-forwprop1-details" } */

extern char *frob (void);
extern _Bool testit (void);
extern void oof (void);

void
test (int code)
{
  char *temp = frob ();
  int rotate = (code == 22);
  if (temp == 0 && !rotate)
    oof ();
}

void
test_2 (int code)
{
  char *temp = frob ();
  int rotate = (code == 22);
  if (!rotate && temp == 0)
    oof ();
}

void
test_3 (int code)
{
  char *temp = frob ();
  int rotate = (code == 22);
  if (!rotate || temp == 0)
    oof ();
}

void
test_4 (int code)
{
  char *temp = frob ();
  int rotate = (code == 22);
  if (temp == 0 || !rotate)
    oof ();
}

void
test_5 (int code)
{
  _Bool temp = testit ();
  _Bool rotate = (code == 22);
  if (temp == 0 && !rotate)
    oof ();
}

void
test_6 (int code)
{
  _Bool temp = testit ();
  _Bool rotate = (code == 22);
  if (!rotate && temp == 0)
    oof ();
}

void
test_7 (int code)
{
  _Bool temp = testit ();
  _Bool rotate = (code == 22);
  if (!rotate || temp == 0)
    oof ();
}

void
test_8 (int code)
{
  _Bool temp = testit ();
  _Bool rotate = (code == 22);
  if (temp == 0 || !rotate)
    oof ();
}

/* ???  This used to check for 8 times transforming the combined conditional
   to a ordered compare.  But the transform does not trigger if we transform
   the negated code == 22 compare to code != 22 first.  It turns out if
   we do that we even generate better code on x86 at least.  */

/* { dg-final { scan-tree-dump-times "simplified to if \\\(\[^ ]* <" 4 "forwprop1"} } */
/* { dg-final { cleanup-tree-dump "forwprop1" } } */

