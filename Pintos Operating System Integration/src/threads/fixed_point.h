#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdio.h>
#include <limits.h>
#include <stdint.h>

typedef uint32_t fixed_point_t;
#define FIXED_POINT_FRACTIONAL_BITS 14
inline fixed_point_t convertInteger(int n);
inline fixed_point_t convertFloat(float n);
inline int convertFixedPointZero(fixed_point_t fp);
inline int convertFixedPointNearest(fixed_point_t fp);
inline fixed_point_t add(fixed_point_t a, fixed_point_t b);
inline fixed_point_t subtract(fixed_point_t a, fixed_point_t b);
inline fixed_point_t addMixed(fixed_point_t x, int n);
inline fixed_point_t subtractMixed(fixed_point_t x, int n);
inline fixed_point_t multiply(fixed_point_t x, fixed_point_t y);
inline fixed_point_t multiplyMixed(fixed_point_t x, int n);
inline fixed_point_t divide(fixed_point_t x, fixed_point_t y);
inline fixed_point_t divideMixed(fixed_point_t x, int n);

/* Converts integer to fixed point */
inline fixed_point_t convertInteger(int n) {
    int fixed_point = (int) (n << FIXED_POINT_FRACTIONAL_BITS);
    if (fixed_point < 0) {
        fixed_point = -fixed_point;
        fixed_point = ~fixed_point;
        fixed_point += 1;
    }
    return (fixed_point_t)(fixed_point);
}

/* Converts float to fixed point */
inline fixed_point_t convertFloat(float n) {
    int fixed_point = (int) ((1 << FIXED_POINT_FRACTIONAL_BITS) * n);
    if (fixed_point < 0) {
        fixed_point = -fixed_point;
        fixed_point = ~fixed_point;
        fixed_point += 1;
    }
    return (fixed_point_t)(fixed_point);
}

/* Converts fixed point to integer, rounded towards zero */
inline int convertFixedPointZero(fixed_point_t fp) {
    int sign = 1;
    if (fp >= INT_MAX) {
        fp -= 1;
        fp = ~fp;
        sign = -1;
    }
    return (int) sign * ((fp / (1 << FIXED_POINT_FRACTIONAL_BITS)));
}

/* Converts fixed point to integer, rounded towards nearest */
inline int convertFixedPointNearest(fixed_point_t fp) {
    if (fp <= INT_MAX ) {
        return (int)(((fp + (1 << (FIXED_POINT_FRACTIONAL_BITS - 1))) / (1 << (FIXED_POINT_FRACTIONAL_BITS))));
    }
    else {
        fp -= 1;
        fp = ~fp;
        int sign = -1;
        return (int) sign * (((fp - (1 << (FIXED_POINT_FRACTIONAL_BITS - 1))) / (1 << (FIXED_POINT_FRACTIONAL_BITS))));
    }
}

/* Adds two fixed points */
inline fixed_point_t add(fixed_point_t a, fixed_point_t b) {
    return (a + b);
}

/* Subtracts two fixed points */
inline fixed_point_t subtract(fixed_point_t a, fixed_point_t b) {
    return (a - b);
}

/* Adds fixed point and an integer */
inline fixed_point_t addMixed(fixed_point_t x, int n) {
    return (x + convertInteger(n));
}

/* Subtracts integer from fixed point */
inline fixed_point_t subtractMixed(fixed_point_t x, int n) {
    return (x - convertInteger(n));
}

/* Multiplies two fixed points */
inline fixed_point_t multiply(fixed_point_t x, fixed_point_t y) {
    return (((int64_t) x * y) / (1 << FIXED_POINT_FRACTIONAL_BITS));
}

/* Adds fixed point and an integer */
inline fixed_point_t multiplyMixed(fixed_point_t x, int n) {
    return (x * n);
}

/* Divides two fixed points */
inline fixed_point_t divide(fixed_point_t x, fixed_point_t y) {
    return (((int64_t) x * (1 << FIXED_POINT_FRACTIONAL_BITS)) / y);
}

/* Divides fixed point by an integer */
inline fixed_point_t divideMixed(fixed_point_t x, int n) {
    return (x / n);
}

#endif /* threads/fixed_point.h */