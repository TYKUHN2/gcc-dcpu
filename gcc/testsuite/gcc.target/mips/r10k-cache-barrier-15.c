/* { dg-mips-options "-O2 -mr10k-cache-barrier=store -mips2" } */
/* { dg-error "requires.*cache.*instruction" "" { target *-*-* } 0 } */
