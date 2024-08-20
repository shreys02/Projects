/**
 * \file bigint.h
 *
 * \brief Big integer library
 */
#ifndef BIGINT_H
#define BIGINT_H

#include <stddef.h>
#include <stdint.h>

#define ERR_BIGINT_BAD_INPUT_DATA    -0x0004   /**< Bad input parameters to function. */
#define ERR_BIGINT_INVALID_CHARACTER -0x0006   /**< There is an invalid character in the digit string. */
#define ERR_BIGINT_BUFFER_TOO_SMALL  -0x0008   /**< The buffer is too small to write to. */
#define ERR_BIGINT_NEGATIVE_VALUE    -0x000A   /**< The input arguments are negative or result in illegal output. */
#define ERR_BIGINT_DIVISION_BY_ZERO  -0x000C   /**< The input argument for division is zero, which is not allowed. */
#define ERR_BIGINT_NOT_ACCEPTABLE    -0x000E   /**< The input arguments are not acceptable. */
#define ERR_BIGINT_ALLOC_FAILED      -0x0010   /**< Memory allocation failed. */

typedef int64_t big_sint;
typedef uint64_t big_uint;
typedef unsigned __int128 big_udbl;
const int small_primes[] = {
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
    31, 37, 41, 43, 47, 53, 59, 61, 67,
    71, 73, 79, 83, 89, 97
};
const int bases[5] = {2, 3, 5, 7, 11};
/**
 * \brief          bigint structure
 */
typedef struct {
    int signum;       /*!<  integer sign      */
    size_t num_limbs; /*!<  total # of limbs  */
    big_uint *data;   /*!<  pointer to limbs  */
} bigint;

#define BIG_ZERO ((bigint){.signum = 0, .num_limbs = 0, .data = NULL})

/**
 * \brief           Initialize one bigint (make internal references valid)
 *                  This just makes it ready to be set or freed,
 *                  but does not define a value for the bigint.
 *
 * \param X         One bigint to initialize.
 */
void big_init(bigint *X);

/**
 * \brief          Unallocate one bigint
 *
 * \param X        One bigint to unallocate.
 */
void big_free(bigint *X);

/**
 * \brief          Copy the contents of Y into X
 *
 * \param X        Destination bigint. It is enlarged if necessary.
 * \param Y        Source bigint.
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed
 */
int big_copy(bigint *X, const bigint *Y);

/**
 * \brief          Return the number of bits up to and including the most
 *                 significant '1' bit'
 *
 * \note           This is also the one-based index of the most significant
 *                 '1' bit.
 *
 * \param X        bigint to use
 */
size_t big_bitlen(const bigint *X);

/**
 * \brief          Return the total size in bytes
 *
 * \param X        bigint to use
 */
size_t big_size(const bigint *X);

/**
 * \brief          Set X = limb; with limb > 0
 * 
 * \param X        Destination bigint
 * \param limb     Value to place into X, must be >0
 * 
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed
 */
int big_set_nonzero(bigint *X, big_uint limb);

/**
 * \brief          Import from an ASCII hexadecimal string
 *
 * \param X        Destination bigint
 * \param s        Null-terminated string buffer
 *
 * \return         0 if successful, or a ERR_BIGINT_XXX error code
 * 
 * \note           Must support lower hexadecimal. May also support
 *                 upper, or a mix.
 */
int big_read_string(bigint *X, const char *s);

/**
 * \brief          Export into an ASCII lower hexadecimal string
 *
 * \param X        Source bigint
 * \param buf      Buffer to write the string to
 * \param buflen   Length of buf
 * \param olen     Length of the string written, including final NUL byte
 *
 * \return         0 if successful, or a ERR_BIGINT_XXX error code.
 *                 *olen is always updated to reflect the amount
 *                 of data that has (or would have) been written.
 *
 * \note           Call this function with buflen = 0 to obtain the
 *                 minimum required buffer size in *olen.
 */
int big_write_string(const bigint *X,
                     char *buf, size_t buflen, size_t *olen);

/**
 * \brief          Import X from unsigned binary data, big endian
 *
 * \param X        Destination bigint
 * \param buf      Input buffer
 * \param buflen   Input buffer size
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed
 */
int big_read_binary(bigint *X, const uint8_t *buf, size_t buflen);

/**
 * \brief          Export X into unsigned binary data, big endian.
 *                 Always fills the whole buffer, which will start with zeros
 *                 if the number is smaller.
 *
 * \param X        Source bigint
 * \param buf      Output buffer
 * \param buflen   Output buffer size
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_BUFFER_TOO_SMALL if buf isn't large enough
 */
int big_write_binary(const bigint *X, uint8_t *buf, size_t buflen);

/**
 * \brief          Signed addition: X = A + B
 *
 * \param X        Destination bigint
 * \param A        Left-hand bigint
 * \param B        Right-hand bigint
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed
 */
int big_add(bigint *X, const bigint *A, const bigint *B);

/**
 * \brief          Signed subtraction: X = A - B
 *
 * \param X        Destination bigint
 * \param A        Left-hand bigint
 * \param B        Right-hand bigint
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed
 */
int big_sub(bigint *X, const bigint *A, const bigint *B);

/**
 * \brief          Compare signed values
 *
 * \param X        Left-hand bigint
 * \param Y        Right-hand bigint
 *
 * \return         1 if X is greater than Y,
 *                 -1 if X is lesser than Y, or
 *                 0 if X is equal to Y
 */
int big_cmp(const bigint *X, const bigint *Y);

/**
 * \brief          Baseline multiplication: X = A * B
 *
 * \param X        Destination bigint
 * \param A        Left-hand bigint
 * \param B        Right-hand bigint
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed
 */
int big_mul(bigint *X, const bigint *A, const bigint *B);

/**
 * \brief          Division by bigint: A = Q * B + R
 *
 * \param Q        Destination bigint for the quotient
 * \param R        Destination bigint for the rest value
 * \param A        Left-hand bigint
 * \param B        Right-hand bigint
 *
 * \return         0 if successful,
 *                 ERR_BIGINT_ALLOC_FAILED if memory allocation failed,
 *                 ERR_BIGINT_DIVISION_BY_ZERO if B == 0
 *
 * \note           Either Q or R can be NULL.
 */

#endif /* BIGINT_H */