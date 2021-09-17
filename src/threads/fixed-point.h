#ifndef FIXED_POINT_H
#define FIXED_POINT_H

/* Simulate calculation on real quantities with integers.

   The fundamental idea is to treat the rightmost bits of an integer as
   representing a fraction. For example, we can designate the lowest 14 bits of
   a signed 32-bit integer as fractional bits, so that an integer x represents
   the real number x/(2**14), where ** represents exponentiation. This is
   called a 17.14 fixed-point number representation, because there are 17 bits
   before the decimal point, 14 bits after it, and one sign bit.(7) A number
   in 17.14 format represents, at maximum, a value of (2**31 - 1)/(2**14) =
   approx. 131,071.999.

   The following table summarizes how fixed-point arithmetic operations can be
   implemented in C. In the table, x and y are fixed-point numbers, n is an
   integer, fixed-point numbers are in signed p.q format where p + q = 31, and
   f is 1 << q:

    Convert n to fixed point:	n * f
    Convert x to integer (rounding toward zero):	x / f
    Convert x to integer (rounding to nearest):	(x + f / 2) / f if x >= 0,
    (x - f / 2) / f if x <= 0.
    Add x and y:	x + y
    Subtract y from x:	x - y
    Add x and n:	x + n * f
    Subtract n from x:	x - n * f
    Multiply x by y:	((int64_t) x) * y / f
    Multiply x by n:	x * n
    Divide x by y:	((int64_t) x) * f / y
    Divide x by n:	x / n. */

/* Fixed point format. */
#define FIXED_FRACTION_BITS 14
#define FIXED_FACTOR (1 << FIXED_FRACTION_BITS)

/* Fixed point number type. */
typedef int fixed_t;

fixed_t fixed_from_int (int n);
int fixed_to_int (fixed_t x);
int fixed_round (fixed_t x);
int fixed_round_shifted (fixed_t x, int scale);

fixed_t fixed_add (fixed_t x, fixed_t y);
fixed_t fixed_sub (fixed_t x, fixed_t y);
fixed_t fixed_add_int (fixed_t x, int n);
fixed_t fixed_sub_int (fixed_t x, int n);

fixed_t fixed_mul (fixed_t x, fixed_t y);
fixed_t fixed_div (fixed_t x, fixed_t y);
fixed_t fixed_mul_int (fixed_t x, int n);
fixed_t fixed_div_int (fixed_t x, int n);

#endif /* thread/fixed-point.h */
