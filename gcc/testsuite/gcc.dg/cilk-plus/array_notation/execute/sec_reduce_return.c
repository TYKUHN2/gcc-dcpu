int add_all (int *my_array, int size)
{
  return __sec_reduce_add (my_array[0:size]);
}

int mult_all (int *my_array, int size)
{
  return __sec_reduce_mul (my_array[0:size]);
}

int main (int argc, char **argv)
{
  int array[10000];

  array[:] = argc; /* All elements should be one.  */

  if (add_all (array, 10000) != 10000)
    return 1;

  if (mult_all (array, 10000) != 1)
    return 1;

  return 0;
}
