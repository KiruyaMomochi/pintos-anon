#include "threads/fixed-point.h"
#include <stdint.h>

/* Convert n to fixed point. */
fixed_t
fixed_from_int (int n)
{
  return (fixed_t)n << FIXED_FRACTION_BITS;
}

/* Convert fixed point to integer (rounding toward zero). */
int
fixed_to_int (fixed_t n)
{
  return n >> FIXED_FRACTION_BITS;
}

/* Convert fixed point to integer (rounding to nearest). */
int
fixed_round (fixed_t x)
{
  return fixed_round_shifted (x, 0);
}

/* Convert fixed point to integer, with scaling of 2**SHIFT (rounding to
   nearest). */
int
fixed_round_shifted (fixed_t x, int shift)
{
  int fraction_bits = FIXED_FRACTION_BITS - shift;
  int round_const = 1 << (fraction_bits - 1);

  if (x >= 0)
    return (x + round_const) >> fraction_bits;
  else
    return (x - round_const) >> fraction_bits;
}

/* Add two fixed point numbers. */
fixed_t
fixed_add (fixed_t x, fixed_t y)
{
  return x + y;
}

/* Subtract a fixed point number from another. */
fixed_t
fixed_sub (fixed_t x, fixed_t y)
{
  return x - y;
}

/* Add a fixed point number and an integer. */
fixed_t
fixed_add_int (fixed_t x, int n)
{
  return x + fixed_from_int (n);
}

/* Subtract an integer from a fixed point number. */
fixed_t
fixed_sub_int (fixed_t x, int n)
{
  return x - fixed_from_int (n);
}

/* Multiply two fixed point numbers. */
fixed_t
fixed_mul (fixed_t x, fixed_t y)
{
  return ((int64_t)x * y) >> FIXED_FRACTION_BITS;
}

/* Divide a fixed point number by another. */
fixed_t
fixed_div (fixed_t x, fixed_t y)
{
  return ((int64_t)x << FIXED_FRACTION_BITS) / y;
}

/* Multiply a fixed point number by an integer. */
fixed_t
fixed_mul_int (fixed_t x, int n)
{
  return x * n;
}

/* Divide a fixed point number by an integer. */
fixed_t
fixed_div_int (fixed_t x, int n)
{
  return x / n;
}
