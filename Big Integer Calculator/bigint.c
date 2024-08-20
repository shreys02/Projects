#include "bigint.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
void big_init(bigint *X) {
    if (X!=NULL) {
        *X = BIG_ZERO;
    }
}

void big_free(bigint *X) {
    X->signum = 0;
    X->num_limbs = 0;
    if (X != NULL && X->data != NULL) {
        free(X->data); 
        X->data = NULL; 
    }
}

int big_copy(bigint *X, const bigint *Y) {
    if (Y == NULL || X == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA;
    }
    if (Y->data == NULL) {
        free(X->data);
        X->data = NULL;
    }
    else if (X->num_limbs < Y->num_limbs || X->data == NULL) {
        big_uint *new_data = (big_uint *) realloc(X->data, Y->num_limbs * sizeof(big_uint));
        if (new_data == NULL) {
            return ERR_BIGINT_ALLOC_FAILED;  
        }
        X->data = new_data;
    }
    if (Y->data!=NULL) {
        for (size_t i = 0; i < Y->num_limbs; i++) {
            X->data[i] = Y->data[i];
        }
    }
    X->num_limbs = Y->num_limbs;
    X->signum = Y->signum;

    return 0; 
}

size_t big_bitlen(const bigint *X) {
    if (X == NULL || X->num_limbs == 0 || X->data == NULL) {
        return 0;
    }
    size_t idx = X->num_limbs-1;
    while (idx >= 0) {
        if (X->data[idx] != 0) {
            break;
        }
        if (idx == 0) {
            return 0;
        }
        idx--;
    } 
    size_t bits = 0;
    big_uint value = X->data[idx];
    while (value) {
        value >>= 1;
        bits+=1;
    }
    if (idx == 0 && bits == 0) {
        return 1;
    }
    return bits + (idx)*64;
}

size_t big_size(const bigint *X) {
    if (X == NULL || (X->num_limbs == 0)) {
        return 0;
    }
    return (big_bitlen(X) + 7) / 8;
}

int big_set_nonzero(bigint *X, big_uint limb) {
    if (X == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA;
    }
    big_uint *new_data = (big_uint *) realloc(X->data, sizeof(big_uint));
    if (new_data == NULL) {
        return ERR_BIGINT_ALLOC_FAILED;  
    }
    X->data = new_data;
    X->data[0] = limb;
    X->signum = 1;
    X->num_limbs = 1;
    return 0;
}


bool big_is_zero(const bigint *num) {
    for (size_t i = 0; i < num->num_limbs; i++) {
        if (num->data[i] != 0) {
            return false;
        }
    }
    return true;
}

int big_read_string(bigint *X, const char *s) {
    if (s == NULL || X == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA;
    }
    X->signum = 1;
    int len, pad;
    int offest = 0;
    len = strlen(s);
    if (s[0] == '-') {
        X->signum = -1;
        len -= 1;
        offest = 1;
    }
    pad = (16 - (len % 16)) % 16;
    char actual[len+pad+1];
    memset(actual, '0', pad);
    strcpy(actual + pad, s+offest);
    int limbs = (len+pad) / 16;
    big_uint *value = (big_uint *) malloc(limbs * sizeof(big_uint));
    if (value == NULL) {
        return ERR_BIGINT_ALLOC_FAILED;
    }

    for(int i = 0; i < limbs; i++) {
        char cur_section[17];
        strncpy(cur_section, actual + i*16, 16);
        cur_section[16] = '\0';
        
        value[limbs-i-1] = (big_uint) strtoull(cur_section, NULL, 16);
    }

    
    X->num_limbs = limbs;
    if (X->data != NULL) {
        free(X->data);
    }
    X->data = value;
    return 0;
}
void print_bigint(const bigint *X) {
    printf("BigInt Limbs (%lu):\n", X->num_limbs);
    printf("Pos or Negative: %d\n", X->signum);
    for (size_t i = 0; i < X->num_limbs; i++) {
        printf("Limb %lu: %lu\n", i, X->data[i]);
    }
}

int big_write_string(const bigint *X,
                     char *buf, size_t buflen, size_t *olen) {
    if (X == NULL || buf == NULL || olen == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA;  
    }
    if (X->num_limbs == 0) {
        *olen = 0;
        return 0;
    }
    size_t required_length = 0;  
    bool leading = true;         
    for (size_t i = X->num_limbs; i > 0; i--) {
        unsigned long long cur_limb = X->data[i - 1];
        //printf("What is value trying to convert: %lld\n", cur_limb);
        for (int j = 15; j >= 0; j--) {
            int digit = (cur_limb >> (4 * j)) & 0xF;
            if (digit != 0 || !leading) {
                leading = false;
                required_length += 1;
            }
        }
    }
    if (leading) {  
        required_length = 1;
    }
    if (X->signum < 0 && !big_is_zero(X)) {
        required_length +=1;
    }
    *olen = required_length + 1;  

    if (buflen < *olen) {
        return ERR_BIGINT_BUFFER_TOO_SMALL;
    }

    size_t index = 0; 
    if (X->signum < 0 && !big_is_zero(X)) {
        buf[index++] = '-';
    }   
    leading = true;   

    for (size_t i = X->num_limbs; i > 0; i--) {
        big_uint cur_limb = X->data[i - 1];
        for (int j = 15; j >= 0; j--) {
            int digit = (cur_limb >> (4 * j)) & 0xF;
            if (digit != 0 || !leading) {
                if (digit < 10) {
                    buf[index++] = '0' + digit;
                } else {
                    buf[index++] = 'a' + (digit - 10);
                }
                leading = false;
            }
        }
    }
    if (leading) { 
        buf[index++] = '0';
    }
    buf[index] = '\0';  
    return 0;  
}

int cmp_abs(const bigint *x, const bigint *y) {
    if (x->num_limbs != y->num_limbs) {
        return (x->num_limbs > y->num_limbs) ? 1 : -1;
    }

    for (size_t i = x->num_limbs; i-- > 0; ) {
        if (x->data[i] != y->data[i]) {
            int result = (x->data[i] > y->data[i]) ? 1 : -1;
            return result;
        }
    }
    return 0;
}

int big_sub_helper(bigint *result, const bigint *A, const bigint *B) {
    size_t result_limbs = A->num_limbs;
    big_init(result);
    result->num_limbs = result_limbs;
    result->data = (big_uint *)malloc(result_limbs * sizeof(big_uint));
    if (result->data == NULL) {
        return ERR_BIGINT_ALLOC_FAILED;
    }

    big_uint borrow = 0;

    for (size_t i = 0; i < result_limbs; i++) {
        big_uint a_limb = (i < A->num_limbs) ? A->data[i] : 0;
        big_uint b_limb = (i < B->num_limbs) ? B->data[i] : 0;

        if (borrow) {
            if (a_limb == 0) {
                a_limb = UINT64_MAX;
                borrow = 1;
            } else {
                a_limb--;
                borrow = 0;
            }
        }

        big_uint diff;
        if (a_limb < b_limb) {
            diff = (UINT64_MAX - b_limb) + a_limb + 1;
            borrow = 1;
        } else {
            diff = a_limb - b_limb;
        }

        result->data[i] = diff;
    }

    int needed = 1;
    for(size_t i=result->num_limbs-1; i >= 0; i--) {
        if (result->data[i] != 0) {
            needed = i+1;
            break;
        }
        if (i==0) {
            break;
        }
    }
    if (needed != result->num_limbs) {
        big_uint *temp = realloc(result->data, sizeof(big_uint) * needed);
        if (temp == NULL) {
            return ERR_BIGINT_ALLOC_FAILED;
        }
        result->data = temp;
        result->num_limbs = needed;
    }

    return 0;
}

int big_add_helper(bigint *X, const bigint *A, const bigint *B) {
    size_t num_limbs = (A->num_limbs > B->num_limbs) ? A->num_limbs : B->num_limbs;
    size_t old_limbs = A->num_limbs;
    if (X->num_limbs < num_limbs) {
        big_uint *new_data = realloc(X->data, num_limbs * sizeof(big_uint));
        if (new_data == NULL) {
            return ERR_BIGINT_ALLOC_FAILED;
        }
        X->data = new_data;
        X->num_limbs = num_limbs;
    }

    big_uint carry = 0; 
    for (size_t i = 0; i < num_limbs; i++) {
        big_uint a = (i < old_limbs) ? A->data[i] : 0;
        big_uint b = (i < B->num_limbs) ? B->data[i] : 0;
        big_udbl sum = (big_udbl)a + b + carry;
        X->data[i] = (big_uint)sum;
        carry = (big_uint)(sum >> (sizeof(big_uint) * 8));
    }

    if (carry > 0) {
        big_uint *new_data = realloc(X->data, (num_limbs + 1) * sizeof(big_uint));
        if (new_data == NULL) {
            return ERR_BIGINT_ALLOC_FAILED;
        }
        X->data = new_data;
        X->data[num_limbs] = carry;
        X->num_limbs = num_limbs + 1;
    }

    int needed = 1;
    for(size_t i=X->num_limbs-1; i >= 0; i--) {
        if (X->data[i] != 0) {
            needed = i+1;
            break;
        }
        if (i==0) {
            break;
        }
    }
    if (needed != X->num_limbs) {
        big_uint *temp = realloc(X->data, sizeof(big_uint) * needed);
        if (temp == NULL) {
            return ERR_BIGINT_ALLOC_FAILED;
        }
        X->data = temp;
        X->num_limbs = needed;
    }

    return 0;
}

int big_add(bigint *X, const bigint *A, const bigint *B) {
    if (X == NULL || A == NULL || B == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA;
    }
    int sign = 1;
    int bigger = 1;
    if (cmp_abs(A, B) <= 0) {
        bigger = 0;
    }
    if (cmp_abs(A, B) <= 0 && B->signum == -1) {
       sign = -1; 
    } else if (cmp_abs(B, A) <= 0 && A->signum == -1 ) {
       sign = -1;
    }

    if (B->signum != A->signum) {
        bigint result;
        if (bigger) 
            big_sub_helper(&result, A, B);
        else 
            big_sub_helper(&result, B, A);
        big_copy(X, &result);
        X->signum = sign;
        big_free(&result);
    }
    else {
        // printf("Big_add_helper getting called with A having %zu limbs\n", A->num_limbs);
        big_add_helper(X, A, B);
        X->signum = sign;
    }
    if (big_is_zero(X)) {
        X->signum = 1;
    }
    return 0;
}
int big_cmp(const bigint *x, const bigint *y) {
    if (x->signum != y->signum) {
        if (x->signum > 0) {
            return 1;
        }
        if (y->signum > 0) {
            return -1;
        }
    }

    if (x->num_limbs != y->num_limbs) {
        if (x->signum > 0) {
            return (x->num_limbs > y->num_limbs) ? 1 : -1;
        }
        else {
            return (x->num_limbs > y->num_limbs) ? -1 : 1;
        }
    }

    for (size_t i = x->num_limbs; i-- > 0; ) {
        if (x->data[i] != y->data[i]) {
            int result = (x->data[i] > y->data[i]) ? 1 : -1;
            return (x->signum > 0) ? result : -result;
        }
    }
    return 0;
}

int limb_result(const bigint *A, const bigint *B) {
    return A->num_limbs >= B->num_limbs;
}


int big_sub(bigint *X, const bigint *A, const bigint *B) {
    bool subtract = (A->signum == B->signum);
    int sign = 1;
    if (A->signum < B->signum) {
        sign = -1; 
    } else if (A->signum == B->signum) {
        if (big_cmp(A, B) < 0) {
            sign = -1;
        }
    }
    const bigint *abs_bigger = A;
    const bigint *abs_smaller = B;
    int cmp = cmp_abs(A, B);
    if (cmp < 0) {
        abs_bigger = B;
        abs_smaller = A;
    }
    bigint result;
    if (subtract) {
        big_sub_helper(&result, abs_bigger, abs_smaller);
        result.signum = sign;
        //print_bigint(&result);
    } else {
        X->signum = sign;
        return big_add_helper(X, A, B);
    }

    big_copy(X, &result);
    big_free(&result);
    return 0;
}

int big_mul(bigint *X, const bigint *A, const bigint *B) {
    bigint result;
    big_init(&result);
    size_t max_limbs = A->num_limbs + B->num_limbs;
    big_uint *result_final = (big_uint *)malloc(max_limbs * sizeof(big_uint));
    if (result_final == NULL) {
        big_free(&result);
        return ERR_BIGINT_ALLOC_FAILED;
    }
    for(size_t j = 0; j < max_limbs; j++) {
        result_final[j] = (big_uint) 0;
    }

    for (size_t i = 0; i < A->num_limbs; i++) {
        big_uint carry = 0;
        for (size_t j = 0; j < B->num_limbs; j++) {
            big_udbl product = (big_udbl)A->data[i] * (big_udbl)B->data[j] + (big_udbl)result_final[i + j] + (big_udbl)carry;
            result_final[i + j] = (big_uint)product;
            carry = (big_uint)(product >> (sizeof(big_uint) * 8));
        }
        result_final[i + B->num_limbs] += carry;
    }
    int needed = 1;
    for(size_t i=max_limbs-1; i >= 0; i--) {
        if (result_final[i] != 0) {
            needed = i+1;
            break;
        }
        if (i==0) {
            break;
        }
    }
    if (needed != max_limbs) {
        big_uint *temp = realloc(result_final, sizeof(big_uint) * needed);
        if (temp == NULL) {
            return ERR_BIGINT_ALLOC_FAILED;
        }
        result_final = temp;
        result.num_limbs = needed;
    }
    else {
    result.num_limbs = max_limbs;
    }
    result.data = result_final;
    result.signum = A->signum * B->signum;
    big_copy(X, &result);
    big_free(&result);
    return 0;
}

// EXTENSION STARTS HERE

/*
This function was called for Karatsuba, and splits the A into parts A0 and 
A1, each with m limbs. This means that if A is odd, it'll pad A1 (the "higher" half)
with 0s where necessary, as Karatsuba requires same size. 
*/
int split_bigint(const bigint *A, size_t m, bigint *A0, bigint *A1) {
    if (m >= A->num_limbs) {
        A1->num_limbs = 1;
        A1->signum = A->signum;
        A1->data = malloc(1 * sizeof(big_uint));
        A1->data[0] = 0;
        big_copy(A0, A);
        return 0;
    }
    A0->num_limbs = m;
    A1->num_limbs = m;
    A0->signum = A->signum;
    A1->signum = A->signum;
    A0->data = (big_uint *)malloc(m * sizeof(big_uint));
    A1->data = (big_uint *)malloc((m) * sizeof(big_uint));
    if (A0->data == NULL || A1->data == NULL) {
        big_free(A0);
        big_free(A1);
        return ERR_BIGINT_ALLOC_FAILED;
    }
    memcpy(A0->data, A->data, m * sizeof(big_uint));
    memcpy(A1->data, A->data + m, (A->num_limbs - m) * sizeof(big_uint));
    for (size_t i = 2*m; i > A->num_limbs; i--) {
        A1->data[m-(2*m-i)-1] = 0;
    }
}

/* 
This function shifts src by limb_shift limbs into bigint result. 
*/
void big_shift_left(bigint *result, const bigint *src, size_t limb_shift) {
    if (limb_shift == 0) {
        big_copy(result, src);
        return;
    }
    if (big_is_zero(src)) {
        free(result->data);
        result->data = malloc(sizeof(big_uint));
        result->data[0] = 0;
        result->signum = 1;
        result->num_limbs = 1;
        return;
    }
    big_uint *temp = realloc(result->data, (src->num_limbs + limb_shift) * sizeof(big_uint));
    if (temp == NULL) {
        return;
    }
    result->data = temp;

    for (size_t i = 0; i < limb_shift; i++) {
        result->data[i] = 0;
    }
    for (size_t i = 0; i < src->num_limbs; i++) {
        result->data[i + limb_shift] = src->data[i];
    }

    result->signum = src->signum;
    result->num_limbs = src->num_limbs + limb_shift;
}

/* 
Similar to big_mul, this function multiplies A and B using Karatsuba's logic
defined in the report. 
*/
int big_karatsuba(bigint *X, const bigint *A, const bigint *B) {
    if (A == NULL || B == NULL || X == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA; 
    }

    int sign = 1;
    if (A->signum == -1) {
        sign *= -1;
    }
    if (B->signum == -1) {
        sign *= -1;
    }
    // Threshold to switch to big_mul is 64 limbs, based on results
    // from the experiments. 
    if (A->num_limbs <= 64 || B->num_limbs <= 64) {
        return big_mul(X, A, B);
    }

    // Determines the split point
    size_t n = (A->num_limbs > B->num_limbs ? A->num_limbs : B->num_limbs);
    size_t m = (n + 1) / 2;
    bigint A0, A1, B0, B1, Z0, Z1, Z2;
    big_init(&A0); 
    big_init(&A1); 
    big_init(&B0); 
    big_init(&B1);
    big_init(&Z0);
    big_init(&Z1); 
    big_init(&Z2);
    // Split A and B into high and low parts
    split_bigint(A, m, &A0, &A1);
    split_bigint(B, m, &B0, &B1);
    // Z0 = A0 * B0
    big_karatsuba(&Z0, &A0, &B0);
    // Z2 = A1 * B1
    big_karatsuba(&Z2, &A1, &B1);

    // Z1 = (A0 + A1) * (B0 + B1) - Z0 - Z2
    bigint tempA, tempB;
    big_init(&tempA); 
    big_init(&tempB);
    big_add(&tempA, &A0, &A1);
    big_add(&tempB, &B0, &B1);
    big_karatsuba(&Z1, &tempA, &tempB);
    big_sub(&Z1, &Z1, &Z0);
    big_sub(&Z1, &Z1, &Z2);

    // Combine results
    bigint shifted_Z2, shifted_Z1;
    big_init(&shifted_Z2); 
    big_init(&shifted_Z1);
    
    // Shift Z1 by m limbs
    big_shift_left(&shifted_Z1, &Z1, m); 
    // Shift Z2 by 2 * m limbs
    big_shift_left(&shifted_Z2, &Z2, 2 * m);  
         

    bigint result, temp;
    big_init(&result);
    big_init(&temp);
    big_add(&temp, &Z0, &shifted_Z1);
    big_add(&result, &temp, &shifted_Z2);

    big_copy(X, &result);
    X->signum = sign;

    big_free(&A0); 
    big_free(&A1);
    big_free(&B0); 
    big_free(&B1);
    big_free(&Z0); 
    big_free(&Z1); 
    big_free(&Z2);
    big_free(&tempA); 
    big_free(&tempB);
    big_free(&shifted_Z1); 
    big_free(&shifted_Z2);
    big_free(&result);
    big_free(&temp);

    return 0;
}

/* 
Similar to split_bigint, this function splits A into 3 m equal parts A0, A1, and A2. 
Necessary for Toom-Cook Multiplication
*/
int split_bigint_three(const bigint *A, size_t m, bigint *A0, bigint *A1, bigint *A2) {
    big_init(A0);
    big_init(A1);
    big_init(A2);

    A0->num_limbs = m;
    A1->num_limbs = m;
    A2->num_limbs = m;
    // Necessary for copying over old-data, prevents buffer overflow error
    size_t old = A->num_limbs - 2 * m;
    if (A2->num_limbs == 0) {
        A2->num_limbs = 1;
    }

    A0->signum = 1;
    A1->signum = 1;
    A2->signum = 1;

    A0->data = (big_uint *)malloc(m * sizeof(big_uint));
    A1->data = (big_uint *)malloc(m * sizeof(big_uint));
    A2->data = (big_uint *)malloc(m * sizeof(big_uint));

    if (A0->data == NULL || A1->data == NULL || A2->data == NULL) {
        big_free(A0);
        big_free(A1);
        big_free(A2);
        return ERR_BIGINT_ALLOC_FAILED;
    }

    memcpy(A0->data, A->data, m * sizeof(big_uint));
    memcpy(A1->data, A->data + m, m * sizeof(big_uint));
    memcpy(A2->data, A->data + 2 * m, old * sizeof(big_uint));
    // What isn't copied over needs to be zeroed out 
    for (size_t i = old; i < m; i++) {
        A2->data[i] = 0;
    }
    return 0;
}

/* 
This method allows for src to be bit_shifted left by bit_shift bits, allowing
for easy multiplication by a power of 2. 
*/
int big_bit_shift_left(bigint *result, const bigint *src, size_t bit_shift) {
    // Handles edge case of 0
    if (big_is_zero(src)) {
        free(result->data);
        result->data = malloc(sizeof(big_uint));
        result->data[0] = 0;
        result->signum = 1;
        result->num_limbs = 1;
        return 0;
    }
    size_t bit_offset = bit_shift % 64;
    size_t limb_shift = bit_shift / 64;
    

    size_t add = 0;
    if (bit_offset != 0) {
        add = 1;
    }
    size_t new_num_limbs = src->num_limbs + limb_shift + add;
    big_uint *temp = (big_uint *)calloc(new_num_limbs, sizeof(big_uint));
    if (temp == NULL) {
        return ERR_BIGINT_ALLOC_FAILED;
    }

    // Handles shifting over limbs' bits
    for (size_t i = 0; i < src->num_limbs; i++) {
        size_t new_index = i + limb_shift;
        temp[new_index] |= src->data[i] << bit_offset;
        if (bit_offset != 0 && new_index + 1 < new_num_limbs) {
            temp[new_index + 1] |= src->data[i] >> (64 - bit_offset);
        }
    }

    // Remove leading zeros
    while (new_num_limbs > 1 && temp[new_num_limbs - 1] == 0) {
        new_num_limbs--;
    }

    result->num_limbs = new_num_limbs;
    result->signum = src->signum;
    free(result->data);
    result->data = temp;
    return 0;
}

/* 
This method allows for src to be bit_shifted right by bit_shift bits, allowing
for easy division by a power of 2. 
*/
int big_bit_shift_right(bigint *result, const bigint *src, size_t bit_shift) {
    // Handles edge case of 0
    if (big_is_zero(src)) {
        free(result->data);
        result->data = malloc(sizeof(big_uint));
        result->data[0] = 0;
        result->signum = 1;
        result->num_limbs = 1;
        return 0;
    }

    size_t bit_offset = bit_shift % 64;
    size_t limb_shift = bit_shift / 64;
    size_t new_num_limbs = (src->num_limbs > limb_shift) ? (src->num_limbs - limb_shift) : 0;
    if (new_num_limbs == 0) {
        // Result should be 0 if no new_limbs, edge case handles
        free(result->data);
        result->data = (big_uint *)calloc(1, sizeof(big_uint));
        result->num_limbs = 1;
        result->signum = 0;
        result->data[0] = 0;
        return 0;
    }

    big_uint *temp = (big_uint *)calloc(new_num_limbs, sizeof(big_uint));
    if (temp == NULL) {
        return ERR_BIGINT_ALLOC_FAILED;
    }

    // Handles shifting over limbs' bits
    for (size_t i = 0; i < new_num_limbs; i++) {
        size_t src_index = i + limb_shift;
        temp[i] |= src->data[src_index] >> bit_offset;
        if (bit_offset != 0 && src_index + 1 < src->num_limbs) {
            temp[i] |= src->data[src_index + 1] << (64 - bit_offset);
        }
    }

    // Remove leading zeros
    while (new_num_limbs > 1 && temp[new_num_limbs - 1] == 0) {
        new_num_limbs--;
    }

    result->num_limbs = new_num_limbs;
    result->signum = src->signum;
    free(result->data);
    result->data = temp;
}

/* 
This function evaluates the specific points needed for the given 3 subcomponent
split as required by Toom-Cook at the following points: 0, 1, -1, 2, and "inf". 
Explained in report in detail. 
*/
void evaluate_polynomials(const bigint *A0, const bigint *A1, const bigint *A2, 
                          bigint *P0, bigint *P1, bigint *P_1, bigint *P2, bigint *P_inf) {
    bigint tempA1, tempA2;
    bigint sum;
    big_init(&sum);
    big_init(&tempA1); 
    big_init(&tempA2);

    // P0 = A0
    big_copy(P0, A0);

    // P_inf = A2
    big_copy(P_inf, A2);

    // P1 = A2 + A1 + A0
    big_add(&tempA1, A2, A1);
    big_add(P1, &tempA1, A0);

    // P_1 = A2 - A1 + A0
    big_sub(&tempA1, A2, A1);
    big_add(P_1, &tempA1, A0);

    // P2 = 4*A2 + 2*A1 + A0
    big_bit_shift_left(&tempA1, A2, 2); // 4*A2
    big_bit_shift_left(&tempA2, A1, 1); // 2*A1
    big_add(&sum, &tempA1, &tempA2);
    big_add(P2, &sum, A0);

    big_free(&tempA1); 
    big_free(&tempA2);
    big_free(&sum);
}

/*
Custom divide by 3 for given bigint representation with 2^64 serving as base.
Necessary for improving Toom-Cook's performance as other divide had to be used, 
which crippled performance at large big_ints. Explained in report further. 
Divides the passed in bigint by 3, and returns the remainder!
*/
int divide_by_3(bigint* cur) {
    uint32_t carry = 0;  
    uint32_t final = 0;
    size_t i = cur->num_limbs-1; 
    while (i >= 0) {
        big_uint current = 0;
        if (carry != 0) {
            if (carry == 1) {
                if (i == 0) {
                    current = (big_uint) 6148914691236517205 + cur->data[i] / 3;
                }
                else {
                current = (big_uint) 6148914691236517205 + cur->data[i] / 3;
                }
                big_uint temp = (cur->data[i]) % 3;
                if (temp != 0 && (temp+carry) != 2) {
                    current+=1;
                }
            }
            if (carry == 2) {
                if (i == 0) {
                    // These are errors due to large numbers, but do not affect calculations. 
                    current = 12297829382473034410 + cur->data[i] / 3;
                }
                else {
                current = 12297829382473034410 + cur->data[i] / 3;
                }
                big_uint temp = cur->data[i] % 3;
                if (temp != 0) {
                    current+=1;
                }
            }
        }
        else {
            current = (carry + cur->data[i]) / 3;  
        }
        final = current % 3;
        carry = (cur->data[i] + carry) % 3; 
        cur->data[i] = current;  
        if (i == 0) {
            break;
        }
        i--;
    }
    return carry;
}

/* 
Solves system of interpolated points to recover coefficients a, b, c, d, e of 
the product. As described in the report. 
*/
void interpolate_results(bigint *result, const bigint *R0, const bigint *R1, 
                         const bigint *R_1, const bigint *R2, const bigint *R_inf, size_t m) {
    bigint temp1; 
    bigint temp2;
    bigint temp3;
    bigint temp1shifted;
    bigint temp2shifted;
    bigint temp3shifted;
    bigint temp4shifted;
    bigint a;
    bigint b;
    bigint c;
    bigint d;
    bigint e;

    big_init(&temp1); 
    big_init(&temp1shifted);
    big_init(&temp2shifted);
    big_init(&temp3shifted);
    big_init(&temp4shifted);
    big_init(&temp2); 
    big_init(&temp3);
    big_init(&a); 
    big_init(&b); 
    big_init(&c); 
    big_init(&d); 
    big_init(&e);

    // e
    big_copy(&e, R0);

    // a
    big_copy(&a, R_inf);

    // c
    big_add(&temp1, R_1, R1); 
    big_bit_shift_right(&temp1shifted, &temp1, 1);
    big_sub(&temp1shifted, &temp1shifted, R_inf);
    big_sub(&temp1shifted, &temp1shifted, R0);
    big_copy(&c, &temp1shifted);

    // b
    big_bit_shift_left(&temp2shifted, R_inf, 4);
    big_sub(&temp2, R2, &temp2shifted);
    big_bit_shift_left(&temp3shifted, &c, 2);
    big_sub(&temp2, &temp2, &temp3shifted);
    big_sub(&temp2, &temp2, R0);
    big_sub(&temp2, &temp2, R1);
    bigint tempp;
    big_init(&tempp);
    big_add(&tempp, &temp2, R_1);
    big_bit_shift_right(&b, &tempp, 1);
    big_free(&tempp);

    int remain = divide_by_3(&b);
    if (remain != 0) {
        bigint one;
        big_set_nonzero(&one, 1);
        big_add(&b, &b, &one);
        big_free(&one);
    }

    // d 
    big_sub(&temp3, R1, R_1);
    big_bit_shift_right(&temp4shifted, &temp3, 1);
    big_sub(&temp4shifted, &temp4shifted, &b);
    big_copy(&d, &temp4shifted);

    // Final Result Calculations, shifts each coefficient by necessary amount
    bigint temp_result;
    big_init(&temp_result);

    big_shift_left(&temp_result, &a, 4 * m);
    big_add(result, result, &temp_result);

    big_shift_left(&temp_result, &b, 3 * m);
    big_add(result, result, &temp_result);

    big_shift_left(&temp_result, &c, 2 * m);
    big_add(result, result, &temp_result);

    big_shift_left(&temp_result, &d, m);

    big_add(result, result, &temp_result);
    big_add(result, result, &e);

    big_free(&temp1); 
    big_free(&temp2); 
    big_free(&temp3);
    big_free(&temp1shifted);
    big_free(&temp2shifted);
    big_free(&temp3shifted);
    big_free(&temp4shifted);
    big_free(&a); 
    big_free(&b); 
    big_free(&c); 
    big_free(&d); 
    big_free(&e);
    big_free(&temp_result);
}

// Pads the provided B with 0s up the the target number of limbs
void big_pad(bigint *B, size_t target) {
    big_uint *new_limbs = (big_uint *)calloc(target, sizeof(big_uint));
    memcpy(new_limbs, B->data, B->num_limbs * sizeof(big_uint));
    free(B->data);
    B->num_limbs = target;
    B->data = new_limbs;
}

/* 
Calculates the product of A and B and puts it into X, correctly splits A and B
into 3 equal pieces, recursively evaluates the polynomials, and then interpolates
the results back into X. Described in the report. 
*/
int big_toom_cook(bigint *X, bigint *A, bigint *B) {
    if (A == NULL || B == NULL || X == NULL) {
        return ERR_BIGINT_BAD_INPUT_DATA; 
    }
    // Handles signed numbers
    int sign = 1;
    if (A->signum == -1) {
        sign *= -1;
    }
    if (B->signum == -1) {
        sign *= -1;
    }

    // Base case for small numbers, use big_mul
    if (A->num_limbs <= 225 || B->num_limbs <= 225) {
        return big_mul(X, A, B);
    }

    // Determine if padding is necessary
    if (A->num_limbs < B->num_limbs) {
        big_pad(A, B->num_limbs);
    }
    else if (B->num_limbs < A->num_limbs) {
        big_pad(B, A->num_limbs);
    }

    size_t n = A->num_limbs;
    size_t m = (n + 2) / 3;

    bigint A0, A1, A2, B0, B1, B2;
    bigint P0, P1, P_1, P2, P_inf, Q0, Q1, Q_1, Q2, Q_inf;
    bigint R0, R1, R_1, R2, R_inf;

    big_init(&A0); 
    big_init(&A1); 
    big_init(&A2);

    big_init(&B0);
    big_init(&B1); 
    big_init(&B2);

    big_init(&P0);
    big_init(&P1); 
    big_init(&P_1);
    big_init(&P2); 
    big_init(&P_inf); 

    big_init(&Q0);
    big_init(&Q1); 
    big_init(&Q_1); 
    big_init(&Q2);
    big_init(&Q_inf); 

    big_init(&R0); 
    big_init(&R1);
    big_init(&R_1); 
    big_init(&R2); 
    big_init(&R_inf);

    // Split A and B into three parts
    split_bigint_three(A, m, &A0, &A1, &A2);
    split_bigint_three(B, m, &B0, &B1, &B2);

    // Evaluate polynomials at different points
    evaluate_polynomials(&A0, &A1, &A2, &P0, &P1, &P_1, &P2, &P_inf);
    evaluate_polynomials(&B0, &B1, &B2, &Q0, &Q1, &Q_1, &Q2, &Q_inf);

    // Perform recursive multiplications
    big_toom_cook(&R0, &P0, &Q0);
    big_toom_cook(&R1, &P1, &Q1);
    big_toom_cook(&R_1, &P_1, &Q_1);
    big_toom_cook(&R2, &P2, &Q2);
    big_toom_cook(&R_inf, &P_inf, &Q_inf);

    // Interpolate results to get final product
    interpolate_results(X, &R0, &R1, &R_1, &R2, &R_inf, m);
    X->signum = sign;
    

    big_free(&A0); 
    big_free(&A1); 
    big_free(&A2);

    big_free(&B0); 
    big_free(&B1); 
    big_free(&B2);

    big_free(&P0); 
    big_free(&P1); 
    big_free(&P_1);
    big_free(&P2); 
    big_free(&P_inf); 

    big_free(&Q0);
    big_free(&Q1); 
    big_free(&Q_1); 
    big_free(&Q2);
    big_free(&Q_inf);

    big_free(&R0); 
    big_free(&R1);
    big_free(&R_1); 
    big_free(&R2); 
    big_free(&R_inf);

    return 0;
}

// One_Limb Multiplication Tests, used largely for edge cases
bool one_limb_tests() {
    bigint first;
    bigint second;
    bigint toom_result;
    bigint karatsuba_result;
    bigint actual;

    char a_buf[4000];
    size_t temp;
    char k_buf[4000];
    char t_buf[4000];

    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // Tests 1 x 1
    big_set_nonzero(&first, 1);
    big_set_nonzero(&second, 1);

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Tests 0 x 0 
    big_set_nonzero(&first, 0);
    big_set_nonzero(&second, 0);

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Tests UINTMAX x UINTMAX (1 of them is negative)
    big_set_nonzero(&first, UINT64_MAX);
    big_set_nonzero(&second, UINT64_MAX);

    first.signum = -1;
    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);
    first.signum = 1;

    // Tests multiplication of 2 random numbers that are both negative
    big_set_nonzero(&first, 12312436426728);
    big_set_nonzero(&second, UINT64_MAX);

    first.signum = -1;
    second.signum = -1;
    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);
    first.signum = 1;
    second.signum = 1;

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    printf("One_Limb_Tests passed!\n");
}

/*
Tests multiplication of two numbers with different number of limbs
*/
bool multiple_diff_limb_tests() {
    bigint first;
    bigint second;
    bigint toom_result;
    bigint karatsuba_result;
    bigint actual;

    char a_buf[4000];
    size_t temp;
    char k_buf[4000];
    char t_buf[4000];

    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // First test case, "easy" one 3x5 limbs
    first.num_limbs = 3;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = 1;
    first.data[1] = 1;
    first.data[2] = 1;

    second.num_limbs = 5;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = 1;
    second.data[0] = 1;
    second.data[1] = 1;
    second.data[2] = 1;
    second.data[3] = 1;
    second.data[4] = 1;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    big_init(&first);
    big_init(&second);
    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // Harder test case, 10 x 5 limbs
    first.num_limbs = 10;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = 1123123123123;
    first.data[1] = 11241312;
    first.data[2] = 1324234234;
    first.data[3] = 1234234;
    first.data[4] = 12342342;
    first.data[5] = 1123123123123;
    first.data[6] = 11241312;
    first.data[7] = 1324234234;
    first.data[8] = 1234234;
    first.data[9] = 12342342;

    second.num_limbs = 5;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = 1;
    second.data[0] = 1234234;
    second.data[1] = 1234234;
    second.data[2] = 1234234;
    second.data[3] = 1234234;
    second.data[4] = 1234234;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    big_init(&first);
    big_init(&second);
    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // Larger test case + Handling Carries with UINTMAX 8 x 13 limbs
    first.num_limbs = 8;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = UINT64_MAX;
    first.data[1] = UINT64_MAX;
    first.data[2] = UINT64_MAX;
    first.data[3] = UINT64_MAX;
    first.data[4] = UINT64_MAX;
    first.data[5] = UINT64_MAX;
    first.data[6] = UINT64_MAX;
    first.data[7] = UINT64_MAX;

    second.num_limbs = 13;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = -1;
    second.data[0] = UINT64_MAX;
    second.data[1] = UINT64_MAX;
    second.data[2] = UINT64_MAX;
    second.data[3] = UINT64_MAX;
    second.data[4] = UINT64_MAX;
    second.data[5] = UINT64_MAX;
    second.data[6] = UINT64_MAX;
    second.data[7] = UINT64_MAX;
    second.data[8] = UINT64_MAX;
    second.data[9] = UINT64_MAX;
    second.data[10] = UINT64_MAX;
    second.data[11] = UINT64_MAX;
    second.data[12] = UINT64_MAX;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Large stress test, between 30-40 limbs each
    big_read_string(&first, "1234567890ABCDEF1234DEF1234567890ABCD7890ABCDEF1234567890ABCDEFEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF12345678EF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890AB90ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF");
    big_read_string(&second, "1234567890ABCDEF11231231286837423874636999746656239493289234727423646236842838293479674293949723475756734957347234DEF1234567890ABCD7890ABCDEF1234567890ABCDEFEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF12345678EF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890AB90ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF");

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Large stress test, between 62-63 limbs each
    big_read_string(&first, "051c22bceb632398a58eb1b8f0f89f92199525b091729be7df289dd8827d52b63748752ec257569cc026daba1f207679c77a8001a1dd36123a3576f03b16a40d598f665ba7f7d226dac7da6668a777ad25587abcf5412dc45940a3af5fbd620074b5bb9f8ab34e514e4b1953bc3c32b381c96cc8a4c6ade44393d6d3ea16e73fb1a816f5482d49258c8cf2931090a801424f3acb0e6b375e35c07920480e930d92bbabb73dfb2fb1a9637ecafc7a18332289cec199a13596a34589a02961b8d73c0ffab3c0e11e78347195d07ea8154703a9fead511052493cf9a646228f7b19ae542720b8db1242aa71f6d5ceb40419ca44844b196b4f30d8e1d62b28fe4688e2e2933334cea2c3211d406593661a531565096c209256233f9a5ec0bd71337e2b13d1bc980bae1526fd20299de1d5626a2f917a2fa9e7d3814f3d2e66547b95dbb9bebab7106bc7b6f01ba3293e4dc02f75b6a18846116d11b57135b4a73b323a551512cf5a92a1e11f04e20d9973aa2423bbabae7c5b08ac8a80f616230b77e54ef87e7f1eb020f3cfe45bd6d9419f8a4b0940bc3f0253ad24f8c808275a3db55bf906b5f6e384f6ff0e47b8f4dedab6e123de96ac4dafb2d665b2af4a7cc3a005607be9aecaa53b7b20117f772ee1e25709d052a4304fdd7d6fb4c0f3ce56578352ed3c3fca6597b5ee47fe90cb124be36541");
    big_read_string(&second, "799a19bd8212cdc3822e37dbc474358b310e3e5d896e6136debef22b4d06e98ec102f45c3012158f071a37bb898e04422dfc83d4e3193d2a749798f4797c2a1ab0bc41ad7b752b438eb25f185f7e8f2fb11b17382bd93a8c7dab68b55c524523bc5e16158f755f795ff50b45ae6c9e7298c279ace1a4dd38dc646646183204816df568dda3057f1dd3120a42f3568382111cedc3545323f114b2f4d180f9f1b6599a0ae67c82d3db3018c3d15f990ebc750fab0cb82c722be6f2ec3871496e177bb6aa4cd28cfa93bb2e527a4fd81d8dd18e11babe9efd48d63fa79b323e35ab3f9658eab75f5e40509f11c6b63c729385db886e987697938ef9727ce510b8dc84a67d41dc61c7e872f8fee8bd74dcd0d0774ab86d9b6669ffddf3817a39aacc74d93c131e9757fb6c188d05d6aa0df43f1eef7535d434969291721a48e4e79297edc20361b777c2e10a97cbf06bef9fbd84133a3e942e2eb171b13a217a190faa9c1718813881643b5c4df90346f3eb057908e43aa530c9906cc1bf4bfed5b2ae9ba21612d07cd3e454f007608900008ce46377afdbc953c012735f7a5e2c75ff939c3fad759539f8801361e5330aa2fcb0e2956ec5086b095f411936d4db86780aa4d7f5a13fed073d54cb7c148e99d10bddb061f7080b2af7d7fe1b38e03aa63c56ae830f78bc5ec7009276a5a5847ffc7bce");

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);
    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Large stress test, between 62-63 limbs each
    big_read_string(&first, "02c8829f01eb3ed605fabfa8a995d2a387bee957048d31b8734e7f9d9f1cdffb620362b4fdf3ef93f499f836ff81485fd049c0c56b05131554c3f0933f61b5039b42395f85970feccba0da1deda3f992e3dc7b47f6cf538f901ac621d46be1bb52c0980d5251afc5f75b08fec2f61f9a3d27a02f385205a4acf3b629d0eba49d202449d138735b8e25ca8b720649df3e1258a76deb309259be7faaf2948b4b50762dd11d248e0c3c8fc5bdf2fe2590cb91878b46de47714b4e09c1490cc9449996484925c62a1156b849c1301d8c498ed94ec1cf4929e1681a05bcf70dfd4651bc2f6b1f48a37f257671e4ca0604017c7ce58e138d80632e0bb923eda9e2c655b3218035127d8128ba9625d59ce2285fa6581e6bf777eba521b27cfa666c166198459752e2969b45f98948530b2472b2199ad62792686a840ace0a5c9b123dc60bc0e89f47458cbb2f59a84036d8439d12526a50c6b4ae38801a6e64e6ae360370806a6974346ba90f0301049329ae4adacda945b550bb8ad9b8567c57211705ffd728402ebbf8b16f96cfb2df9a361e0779af80011a4adfcf0634bd4f7ab84a27ba92b4377abf199b3aec690d73b145f7c74ce65cd64690e9dc7acc8ef9d7c302f24b9b99c06d375c17c21d338ebf83173121dd2187f7e0d1f3be719323bad8a0e5137e792baa9ecad188abbeaae49f4a149070");
    big_read_string(&second, "3fdd56856497118617ebb06eebedcc91be32b0b1679ee797003e19d638cc5ea94257c5ba7162d2c5d0d67d7982933a4e963c44b643e8be08fa32e3a53698f7c96a94fbb9f286446f81ab2f834c94c790a88000b7566019cde9b6a0ca30760f07a880eb26362b00b7190f110be199c55c9de3d363835f0e82293e8959ce67ab819f53db3f7258ee7c13f3a6ac432a8c666c432afad0404e40131447ebe78cabce5e704ea554a362f8b96119f77350f99588a40c57a4d4c5bbf79758c2095a990e475a0ae9ea92244424dad32bc933fee96947b849eedea00a517ed29337ae5e99a552305b63b3adf9a26d49c79a51ba31f24ff7946a990393815539d425e81e0c5667b3d341da5e7c69d6350b684adb7d7f00394772e32289282baa6a3b9f5ced2118daef8e9d353aedf931722005ab0164e9c89e05f67f45cd411bd5a7241fcd9a92f1a1897b0b9e6ee87f31dc4ae9502fd874911335b903104cd2746dd6ac917342d2d2ca62c2874247be229653767447cf8dd3cfa9d491ae4f39decf6bec70d981a76c4f7fd488dacff1ad65d6fe29db44930173282cb15efe2d4402c870c88a20f9b223263baabff4492d9e64688cbdecb6dee0e086abbb23d0da044476879606b2c985e1c56fdfce167199e5730eb77c6715134956dfea69333711d7d5e5b3aab48a807362041d564fe29237b81c9d64b698");

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);
    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Large stress test, between 62-63 limbs each
    big_read_string(&first, "defeda60ca0cea1deae4b3f6efbca62e7a5b85d4319d13a4f4d03ea033fc5684e6437725b470700bfe65852c84b3ac4219c1e53d7681203dd4a460c7c495ed62af5b2b992e684a95cfac9e24b6998b36e7cfbe5fa26bdcb75fb6a558764feec774654835de6b5cf5084f6d6c01e4e6316bfbd3e5be499ecdd02f37c4179209d6a1ecd08da973d54626a9545cce32f9140a0a5d87b5e204c1b2878c795fb88c40fb2079e359ec7922c0c2c0f979c5e362c03f3226e08ac73dd621fb881d641a2baa3e22554a6c018722353b04b4963bde0e3755f27494b1d4363f14005750802c9a021f19f6e65fccc82802227f2140423ca2631c4d9b58985609c38c372975a8c989418497de909e9afa6e5ff947369e3e956693bce2d870d4aba73b7630c8fb9bf9043785235b45dc780008950ada03089753e911858163a69fb4ffab2ab00f269b406e0dc42a7633679248290ba1cb9ac24287bb8d355b4432db1e31c31565668f52da9f0c2e7e4bcafcbce8abd7b275353fc92ed558c3c788b6780570d065b741cf62bba356590f9ace84a4b4d3e1df387cb0b5f04b8746659b9e7046757d6675de311279abea94a7d5a29f4b9c1f69c6d5015cb217bd5546b4c266b91905c2e63a4b7d2d5ef64270cf997d1c8be2ca84d382814ba593052bc582a770c3fb0ad3d6dda9237e2519cf61202261746218b52390");
    big_read_string(&second, "f91e615533153dace94a3ac541e0deccbed3435c61eac4bf1571fbafbe2c4e3c5f9e1af757d2bdb1c6e987bd80879dfef564f9c3a993940658f84e85872b1aedf702b55bbc52ab5cf779f716262c4ce62fa16a83d0f1726e8ac1793c2fcbe24582536927e21c4c4ee67c66cd958432a29c08a2ddd852089e8816a28b4551629926fa50589921d7736e8b3f56506100eb8bf727672c3d10e19e33f269c428f5ef12606e6e4493929400c839e5966ce6972ae853b34918b0d66436a4c2b8ca9468d00407fc18497f4437490f1dfb4f0dc4eb76652459481ac695c8975990a919906daec6e24b06648731d8d5224479680f038914f9466cf92b8481013e67678990842c1662a81221fce574c83a7bbff8190b1e3f9c78053d32d245d17e8ff1bf0fae80183f6973321766db3dfaeff5aef90474b2dc099fa7c2961b4b51ee3ec5c71c8632bffc7c80929bf87680ca28d32a5d600a895f2d23c1a9ef364052a07a11c497a31e4263074fd517f78c8126cce17a7bebe7de9eb87455240ba8ccca8809c2c7e1fabe69ee5ea8024b7aba0a9e4bb06da0b85a6ffe6944bb08a2c2b2396028600b36ed91240e2ceec9232ccb0dd13b9bbb4fa970b7aa0599d0f5eab71b585f3718a501ee4674930c09045f022f647bb79b74446167ebf1767551374e0f62a0fb16c8b077999d32c70cdc037e011ffe893fa2");

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);
    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Large stress test, between 50-60 limbs each
    big_read_string(&first, "687a5e9afce310334d2b9a243e3e2b0fda499e46c17b6cd3922da1c6a4d919f6fbfab8345654db500eaf364d47e714a3e6983a8f6d967c1e1f1847f481029325a52e35c527accbb1be9eeb946d7feb2710e119faff0cbb71ad3def09b2c041f98a7b6c281cc619d8ee35b100b997d254566111974cf404e3892e13e2d39f612a862c71c4a3c1671e395527a183a4748e47b57040a823dda71b2190d63425cbfb5b8dba3bbfce8222bc4763e6aed086f808dcc5fba0ecca0f93061340cf8545edd9db0ed866fd102ab1675ae0166957023c46c74a3f2b83844738a97380127180e4c00f5aec5ce94253a763c8a0aa0a6977087311416abd12039e4fe9f08e2b6d4d85c3b29ae64d5e70e0ee19f5ceb96c89893d83b886be46343e0f23a3402e9990af141837ae6d8e47db2abd202f59ecc31ec21d74a4d5dc150a1af77ec26d87d5a6d0c20bfb983ff94a58999d25da54c65480d8e9a2541dd4f7e93bbd8a6860bed6c48fb28bfe2bae3d10560e6a46f3571ec4767104e420f1817fadde9d5850f3caceb8902765f1355b7d4f8a0cc0824f968bb5e77cb127d1862f1b792ba84e398db5501333b8aacd583f6fd9000e9a8e80a4bd112feed761ea9e9583423cedf5929237f3e1a6f8a58e5580a5c1946dfbfa1c65c4d368e07eccbcbedf53430b80fbab3ebc92c49c8ade80e66dae431ff189f0ae");
    big_read_string(&second, "c29ca4d4af402f5c04491194e0f0d42f2c863355fd72b91df3f94da582c236353fa2cff727ab8dab039621c53400bb08ef04fbd4ff31fefa039a3fc3aa012107941e4b346032b994d08c838e11d8a94abd5b61492cfaae54e84e185c84e4b3aa3a1bc1ae73868c422bd15abd0d1a5bf0ef2b48a966310cf68087303cde8d5aaff64aeac7610ca8d76bbdac960be7c2f985bffdd3256d54ef6214a68f1447fd714d91947ae66ca51a4f24e5c82bd12347aee80f39ce516d2ba8eaef3fdba7aa39796130c7c0f9fcc6f78db3e7af15c696e64829bb1d096b4d69862e67feb252986aa931017afba6c071783f86696a9bba78b815967cee1ee6eff7a493afa6b20b23ab8142afcf2b9cb7aa6c61eab8337e78b84798be089755223bfc6879faaadb620e1960046ee7a008e62a5ad173601c840e67c2cdd61055498ca33f51b48cf0a0bb65b13a01331017e3baacbb4c22203aec2826cfcb825f4702e33d79df474ee6589519da0a11b0013a3211ac471832ddc7297423015f520beb6d6d05b885fdf733890dd11c2fa9e6d6170e05a91c61f2cb93f16a1c12a9d4d78f90f3815fd1db3d89f639a8d95ddd160e599e1f7b53a3d04e4393fe65bee7f5cbb1a9cce6e8a4c0c2b2e3064cf7e916f0a70e4669f895470602575110c954ea946f8560b771aab36f134f");

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);
    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    printf("Multiple_diff_limbs_tests passed!\n");
}

/*
Tests multiplication of two numbers with same number of limbs
*/
bool multiple_same_limb_tests() {
    bigint first;
    bigint second;
    bigint toom_result;
    bigint karatsuba_result;
    bigint actual;

    char a_buf[4000];
    size_t temp;
    char k_buf[4000];
    char t_buf[4000];

    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // Easy 3x3 limb multiplication
    first.num_limbs = 3;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = 1;
    first.data[1] = 1;
    first.data[2] = 1;

    second.num_limbs = 3;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = 1;
    second.data[0] = 1;
    second.data[1] = 1;
    second.data[2] = 1;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    big_init(&first);
    big_init(&second);
    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // Harder 5x5 limb multiplication
    first.num_limbs = 5;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = 1123123123123;
    first.data[1] = 11241312;
    first.data[2] = 1324234234;
    first.data[3] = 1234234;
    first.data[4] = 12342342;

    second.num_limbs = 5;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = 1;
    second.data[0] = 1234234;
    second.data[1] = 1234234;
    second.data[2] = 1234234;
    second.data[3] = 1234234;
    second.data[4] = 1234234;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    big_init(&first);
    big_init(&second);
    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);

    // Ensure this works with all 0s
    first.num_limbs = 5;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = 0;
    first.data[1] = 0;
    first.data[2] = 0;
    first.data[3] = 0;
    first.data[4] = 0;

    second.num_limbs = 5;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = 1;
    second.data[0] = 1;
    second.data[1] = 1;
    second.data[2] = 1;
    second.data[3] = 1;
    second.data[4] = 1;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    big_init(&first);
    big_init(&second);
    big_init(&toom_result);
    big_init(&karatsuba_result);
    big_init(&actual);
    // Ensure it works to handle UINTMAX Borrows, along with multiple limbs of
    // just 0s 8x8 limb multiplication
    // 1 number is negative
    first.num_limbs = 8;
    first.data = malloc(first.num_limbs*sizeof(big_uint));
    first.signum = 1;
    first.data[0] = UINT64_MAX;
    first.data[1] = 0;
    first.data[2] = 0;
    first.data[3] = UINT64_MAX;
    first.data[4] = UINT64_MAX;
    first.data[5] = UINT64_MAX;
    first.data[6] = UINT64_MAX;
    first.data[7] = UINT64_MAX;

    second.num_limbs = 8;
    second.data = malloc(second.num_limbs*sizeof(big_uint));
    second.signum = -1;
    second.data[0] = UINT64_MAX;
    second.data[1] = UINT64_MAX;
    second.data[2] = UINT64_MAX;
    second.data[3] = 0;
    second.data[4] = 0;
    second.data[5] = 0;
    second.data[6] = UINT64_MAX;
    second.data[7] = UINT64_MAX;

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    // Stress test of the same number multiplied together
    // Number is negative
    big_read_string(&first, "-1234567890ABCDEF1234DEF1234567890ABCD7890ABCDEF1234567890ABCDEFEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF12345678EF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890AB90ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF");
    big_read_string(&second, "-1234567890ABCDEF1234DEF1234567890ABCD7890ABCDEF1234567890ABCDEFEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF12345678EF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890AB90ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF");

    big_mul(&actual, &first, &second);
    big_karatsuba(&karatsuba_result, &first, &second);
    big_toom_cook(&toom_result, &first, &second);

    big_write_string(&actual, a_buf, sizeof(a_buf), &temp);
    big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
    big_write_string(&toom_result, t_buf, sizeof(t_buf), &temp);

    assert(strcmp(a_buf, k_buf) == 0);
    assert(strcmp(a_buf, t_buf) == 0);

    big_free(&first);
    big_free(&second);
    big_free(&toom_result);
    big_free(&karatsuba_result);
    big_free(&actual);

    printf("Multiple_same_limb_tests passed!\n");
}

// Generates a ramdom hexadecimal string of length length
void gen_rand_hex(char *output, size_t length) {
    const char hex_dict[] = "0123456789abcdef";
    for (size_t i = 0; i < length; i++) {
        output[i] = hex_dict[rand() % 16];
    }
    output[length] = '\0'; 
}

// Generates a random number between low-high
int gen_rand(int low, int high) {
    if (low > high) {
        int temp = low;
        low = high;
        high = temp;
    }
    return low + (rand() % (high - low + 1));
}

/* Performs experiment1 as described in the report. 
By default, performs 10 multiplications of 2 numbers which have 
a random number represented by some length between the range low, high!
Times the multiplications and returns the total time taken up by each 
strategy, and checks for correctness!
*/
void experiment1(int low, int high) {
    srand(time(NULL)); 
    int length_first = gen_rand(low, high);
    int length_second = gen_rand(low, high);

    struct timeval start, end;
    bigint first, second, result;
    bigint karatsuba_result;
    bigint toomcook_result;
    big_init(&first);
    big_init(&second);   
    big_init(&result);
    big_init(&karatsuba_result);
    big_init(&toomcook_result);
    char hex_string[length_first + 1]; 
    char hex_string2[length_second + 1]; 
    double regtime = 0;
    double karattime = 0;
    double toomcooktime = 0;
    int counter = 0;
    size_t temp;
    // Note limit here, product limited to 100,000 hexadecimal characters
    char a_buf[100000];
    char k_buf[100000];
    char t_buf[100000];
    while (counter < 10) {
        gen_rand_hex(hex_string, length_first);
        gen_rand_hex(hex_string2, length_second);
        big_read_string(&first, hex_string);
        big_read_string(&second, hex_string);

        gettimeofday(&start, NULL);
        big_karatsuba(&karatsuba_result, &first, &second);
        gettimeofday(&end, NULL);
        karattime += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

        gettimeofday(&start, NULL);
        big_mul(&result, &first, &second);
        gettimeofday(&end, NULL);
        regtime += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        
        gettimeofday(&start, NULL);
        big_toom_cook(&toomcook_result, &first, &second);
        gettimeofday(&end, NULL);
        toomcooktime += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        
        counter+=1;
        big_write_string(&result, a_buf, sizeof(a_buf), &temp);
        big_write_string(&karatsuba_result, k_buf, sizeof(k_buf), &temp);
        big_write_string(&toomcook_result, t_buf, sizeof(t_buf), &temp);
        if (strcmp(a_buf, k_buf) != 0) {
            print_bigint(&first);
            print_bigint(&second);
            print_bigint(&result);
            print_bigint(&karatsuba_result);
        }
        assert(strcmp(a_buf, k_buf) == 0);
        assert(strcmp(a_buf, t_buf) == 0);
    }
    printf("Experiment 1!\n");
    printf("This is how long reg mul took: %f seconds\n", regtime);
    printf("This is how long karat mul took: %f seconds\n", karattime);
    printf("This is how long toom mul took: %f seconds\n", toomcooktime);
    printf("\n");
    big_free(&first);
    big_free(&second);
    big_free(&result);
    big_free(&karatsuba_result);
    big_free(&toomcook_result);
    return;
}

// Conducts an experiment of 2 random bigints multiplied together, 
// by default using Karatsuba multiplication (can be switched out). 
// Size is determined by number of hexadecimal characters passed in,
// the numbers are randomly generated with those quantity of characters!
void experiment2(int size) {
    struct timeval start, end;
    gettimeofday(&start, NULL);
    srand(time(NULL)); 

    bigint first, second, result;
    big_init(&first);
    big_init(&second);
    big_init(&result);

    size_t length = size;
    char hex_string[length + 1]; 
    char hex_string2[length + 1]; 

    int counter = 0;
    while (counter < 10) {
        gen_rand_hex(hex_string, length);
        gen_rand_hex(hex_string2, length);
        big_read_string(&first, hex_string);
        big_read_string(&second, hex_string);
        big_karatsuba(&result, &first, &second);
        counter+=1;
    }
    big_free(&first);
    big_free(&second);
    big_free(&result);
    gettimeofday(&end, NULL);
    double timeElapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    printf("Experiment 2!: This is how long it took: %f seconds\n", timeElapsed);
    return;
}

int main() {
    // Note please wait a few seconds for the experiments, they will print out results!
    one_limb_tests();
    multiple_diff_limb_tests();
    multiple_same_limb_tests();
    experiment1(50000, 100000);
    experiment2(5000);
    return 0;
}