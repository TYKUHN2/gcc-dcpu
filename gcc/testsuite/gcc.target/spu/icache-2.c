/* { dg-do compile } */
/* { dg-options "-O2 -msoftware-icache " } */

int i;

int main()
{
  for (;;)
  {
    switch (i)
    {
      case 0:
      case 1:
        return 1;

      case 2:
      case 3:
        return 0;

      case 5:
        --i;
    }
  }
}

 /* { dg-final { scan-assembler-times "icache_ptr_handler" 1 } } */

