/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file utils.h
 * @brief Utility functions for checkasm tests
 *
 * This header provides utility functions commonly needed when writing checkasm
 * tests, including:
 * - Random number generation (uniform and normal distributions)
 * - Memory initialization and randomization
 * - Floating-point comparison utilities
 * - Buffer definition and checking helpers
 */

#ifndef CHECKASM_UTILS_H
#define CHECKASM_UTILS_H

#include <stdint.h>

#include "checkasm/attributes.h"

/**
 * @defgroup rng Random Number Generation
 * @brief Functions for generating uniformly distributed random numbers
 *
 * These functions use the seed specified in CheckasmConfig (or a time-based
 * seed if not specified) to generate deterministic, reproducible random values.
 * @{
 */

/**
 * @brief Generate a random non-negative integer
 * @return Random value in range [0, INT_MAX]
 */
CHECKASM_API int checkasm_rand(void);

/**
 * @brief Generate a random double-precision floating-point number
 * @return Random value in range [0.0, 1.0)
 */
CHECKASM_API double checkasm_randf(void);

/**
 * @brief Generate a random 32-bit unsigned integer
 * @return Random value in range [0, UINT32_MAX]
 */
CHECKASM_API uint32_t checkasm_rand_uint32(void);

/**
 * @brief Generate a random 32-bit signed integer
 * @return Random value in range [INT32_MIN, INT32_MAX]
 */
CHECKASM_API int32_t checkasm_rand_int32(void);

/** @} */ /* rng */

/**
 * @brief Describes a normal (Gaussian) distribution
 *
 * Structure specifying the parameters of a normal distribution for use
 * with random number generation functions.
 */
typedef struct CheckasmDist {
    double mean;   /**< Mean (center) of the distribution */
    double stddev; /**< Standard deviation (spread) of the distribution */
} CheckasmDist;

/**
 * @def checkasm_dist_standard
 * @brief Standard normal distribution (mean=0, stddev=1)
 */
#define checkasm_dist_standard ((CheckasmDist) { 0.0, 1.0 })

/**
 * @brief Generate a normally distributed random number
 * @param[in] dist Distribution parameters (mean and standard deviation)
 * @return Random value from the specified normal distribution
 */
CHECKASM_API double checkasm_rand_dist(CheckasmDist dist);

/**
 * @brief Generate a random number from the standard normal distribution
 * @return Random value from N(0,1) distribution
 * @see checkasm_dist_standard
 */
CHECKASM_API double checkasm_rand_norm(void);

/**
 * @defgroup memory Memory Initialization
 * @brief Functions for filling buffers with various patterns of data
 *
 * A collection of functions to initialize memory buffers with random data,
 * constant values, or pathological test patterns. These are useful for setting
 * up input/output data buffers for checkasm tests.
 * @{
 */

/**
 * @brief Fill a buffer with uniformly chosen random bytes
 * @param[out] buf Buffer to fill
 * @param[in] bytes Number of bytes to randomize
 */
CHECKASM_API void checkasm_randomize(void *buf, size_t bytes);

/**
 * @brief Fill a uint8_t buffer with random values chosen uniformly within a mask
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 * @param[in] mask Bit mask to apply to each random value
 */
CHECKASM_API void checkasm_randomize_mask8(uint8_t *buf, int width, uint8_t mask);

/**
 * @brief Fill a uint16_t buffer with random values chosen uniformly within a mask
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 * @param[in] mask Bit mask to apply to each random value
 */
CHECKASM_API void checkasm_randomize_mask16(uint16_t *buf, int width, uint16_t mask);

/**
 * @brief Fill a double buffer with random values chosen uniformly below a limit
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 * @param[in] range Exclusive upper bound on value (range is [0, range))
 */
CHECKASM_API void checkasm_randomize_range(double *buf, int width, double range);

/**
 * @brief Fill a float buffer with random values chosen uniformly below a limit
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 * @param[in] range Exclusive upper bound on value (range is [0, range))
 */
CHECKASM_API void checkasm_randomize_rangef(float *buf, int width, float range);

/**
 * @brief Fill a double buffer with normally distributed random values
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 * @param[in] dist Distribution parameters (mean and standard deviation)
 */
CHECKASM_API void checkasm_randomize_dist(double *buf, int width, CheckasmDist dist);

/**
 * @brief Fill a float buffer with normally distributed random values
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 * @param[in] dist Distribution parameters (mean and standard deviation)
 */
CHECKASM_API void checkasm_randomize_distf(float *buf, int width, CheckasmDist dist);

/**
 * @brief Fill a double buffer with values from a standard normal distribution
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 */
CHECKASM_API void checkasm_randomize_norm(double *buf, int width);

/**
 * @brief Fill a float buffer with values from a standard normal distribution
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to randomize
 */
CHECKASM_API void checkasm_randomize_normf(float *buf, int width);

/**
 * @brief Clear a buffer to a pre-determined pattern (currently 0xAA)
 * @param[out] buf Buffer to clear
 * @param[in] bytes Number of bytes to clear
 */
CHECKASM_API void checkasm_clear(void *buf, size_t bytes);

/**
 * @brief Fill a uint8_t buffer with a constant value
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to set
 * @param[in] val Value to write to each element
 * @note This is functionally equivalent to memset(), and merely provided for
 *       consistency.
 */
CHECKASM_API void checkasm_clear8(uint8_t *buf, int width, uint8_t val);

/**
 * @brief Fill a uint16_t buffer with a constant value
 * @param[out] buf Buffer to fill
 * @param[in] width Number of elements to set
 * @param[in] val Value to write to each element
 */
CHECKASM_API void checkasm_clear16(uint16_t *buf, int width, uint16_t val);

/**
 * @brief Initialize a buffer with pathological test patterns
 *
 * Fills a buffer with a random mixture of edge cases, test patterns, and
 * random data designed to trigger potential bugs. The exact pattern depends
 * on the random seed and may include a mix of low values, high values,
 * alternating bits, random bytes, and so on.
 *
 * @param[out] buf Buffer to initialize
 * @param[in] bytes Number of bytes to initialize
 */
CHECKASM_API void checkasm_init(void *buf, size_t bytes);

/**
 * @brief Initialize a uint8_t buffer with pathological values within a mask
 * @param[out] buf Buffer to initialize
 * @param[in] width Number of elements to initialize
 * @param[in] mask Bit mask to apply to values
 * @see checkasm_init()
 */
CHECKASM_API void checkasm_init_mask8(uint8_t *buf, int width, uint8_t mask);

/**
 * @brief Initialize a uint16_t buffer with pathological values within a mask
 * @param[out] buf Buffer to initialize
 * @param[in] width Number of elements to initialize
 * @param[in] mask Bit mask to apply to values
 * @see checkasm_init()
 */
CHECKASM_API void checkasm_init_mask16(uint16_t *buf, int width, uint16_t mask);

/**
 * @def CLEAR_BUF(buf)
 * @brief Clear a fixed size buffer (convenience macro)
 * @param buf Fixed-size buffer array to clear
 */
#define CLEAR_BUF(buf) checkasm_clear(buf, sizeof(buf))

/**
 * @def RANDOMIZE_BUF(buf)
 * @brief Fill a fixed size buffer with random data (convenience macro)
 * @param buf Fixed-size buffer array to randomize
 */
#define RANDOMIZE_BUF(buf) checkasm_randomize(buf, sizeof(buf))

/**
 * @def INITIALIZE_BUF(buf)
 * @brief Fill a fixed size buffer with pathological test data (convenience macro)
 * @param buf Fixed-size buffer array to initialize
 * @see checkasm_init()
 */
#define INITIALIZE_BUF(buf) checkasm_init(buf, sizeof(buf))

/** @} */ /* memory */

/**
 * @defgroup floatcmp Floating-Point Comparison
 * @brief Utilities for comparing floating-point values with tolerance
 *
 * These functions compare floating-point values allowing for acceptable
 * differences due to rounding, precision loss, or different computation orders.
 * @{
 */

/**
 * @brief Compare floats using ULP (Units in Last Place) tolerance
 * @param[in] a First value
 * @param[in] b Second value
 * @param[in] max_ulp Maximum acceptable ULP distance
 * @return Non-zero if values are within tolerance, 0 otherwise
 */
CHECKASM_API int checkasm_float_near_ulp(float a, float b, unsigned max_ulp);

/**
 * @brief Compare floats using absolute epsilon tolerance
 * @param[in] a First value
 * @param[in] b Second value
 * @param[in] eps Maximum acceptable absolute difference
 * @return Non-zero if |a-b| < eps, 0 otherwise
 */
CHECKASM_API int checkasm_float_near_abs_eps(float a, float b, float eps);

/**
 * @brief Compare floats using both epsilon and ULP tolerances
 * @param[in] a First value
 * @param[in] b Second value
 * @param[in] eps Maximum acceptable absolute difference
 * @param[in] max_ulp Maximum acceptable ULP distance
 * @return Non-zero if within either tolerance, 0 otherwise
 */
CHECKASM_API int checkasm_float_near_abs_eps_ulp(float a, float b, float eps,
                                                 unsigned max_ulp);

/**
 * @brief Compare float arrays using ULP tolerance
 * @param[in] a First array
 * @param[in] b Second array
 * @param[in] max_ulp Maximum acceptable ULP distance
 * @param[in] len Number of elements to compare
 * @return Non-zero if all elements are within tolerance, 0 otherwise
 */
CHECKASM_API int checkasm_float_near_ulp_array(const float *a, const float *b,
                                               unsigned max_ulp, int len);

/**
 * @brief Compare float arrays using absolute epsilon tolerance
 * @param[in] a First array
 * @param[in] b Second array
 * @param[in] eps Maximum acceptable absolute difference per element
 * @param[in] len Number of elements to compare
 * @return Non-zero if all elements are within tolerance, 0 otherwise
 */
CHECKASM_API int checkasm_float_near_abs_eps_array(const float *a, const float *b,
                                                   float eps, int len);

/**
 * @brief Compare float arrays using both epsilon and ULP tolerances
 * @param[in] a First array
 * @param[in] b Second array
 * @param[in] eps Maximum acceptable absolute difference per element
 * @param[in] max_ulp Maximum acceptable ULP distance
 * @param[in] len Number of elements to compare
 * @return Non-zero if all elements are within tolerance, 0 otherwise
 */
CHECKASM_API int checkasm_float_near_abs_eps_array_ulp(const float *a, const float *b,
                                                       float eps, unsigned max_ulp,
                                                       int len);

/**
 * @brief Compare doubles using absolute epsilon tolerance
 * @param[in] a First value
 * @param[in] b Second value
 * @param[in] eps Maximum acceptable absolute difference
 * @return Non-zero if |a-b| <= eps, 0 otherwise
 */
CHECKASM_API int checkasm_double_near_abs_eps(double a, double b, double eps);

/**
 * @brief Compare double arrays using absolute epsilon tolerance
 * @param[in] a First array
 * @param[in] b Second array
 * @param[in] eps Maximum acceptable absolute difference per element
 * @param[in] len Number of elements to compare
 * @return Non-zero if all elements are within tolerance, 0 otherwise
 */
CHECKASM_API int checkasm_double_near_abs_eps_array(const double *a, const double *b,
                                                    double eps, unsigned len);

/** @} */ /* floatcmp */

/** @addtogroup aliases
 *  @{ */
#define float_near_ulp               checkasm_float_near_ulp
#define float_near_abs_eps           checkasm_float_near_abs_eps
#define float_near_abs_eps_ulp       checkasm_float_near_abs_eps_ulp
#define float_near_ulp_array         checkasm_float_near_ulp_array
#define float_near_abs_eps_array     checkasm_float_near_abs_eps_array
#define float_near_abs_eps_array_ulp checkasm_float_near_abs_eps_array_ulp
#define double_near_abs_eps          checkasm_double_near_abs_eps
#define double_near_abs_eps_array    checkasm_double_near_abs_eps_array
/** @} */

/**
 * @defgroup bufcmp Buffer Comparison Utilities
 * @brief Functions and macros for comparing multi-dimensional buffers
 *
 * These utilities compare 2D buffers (with stride support) and detect
 * differences, including in padding regions. Used to verify that optimized
 * implementations produce bit-identical output to reference implementations.
 * @{
 */

/**
 * @def CHECKASM_ALIGN(x)
 * @brief Declare a variable with platform-specific alignment requirements
 * @param x Variable declaration
 * @note This must be applied to each buffer individually!
 *
 * @code
 * // correct
 * CHECKASM_ALIGN(uint8_t buf1[64*64]);
 * CHECKASM_ALIGN(uint8_t buf2[64*64]);
 *
 * // wrong
 * CHECKASM_ALIGN(uint8_t buf1[64*64], buf2[64*64]);
 * @endcode
 */
#ifdef _MSC_VER
  #define CHECKASM_ALIGN(x) __declspec(align(CHECKASM_ALIGNMENT)) x
#else
  #define CHECKASM_ALIGN(x) x __attribute__((aligned(CHECKASM_ALIGNMENT)))
#endif

/**
 * @addtogroup internal
 * @{
 */

#define DECL_CHECK_FUNC(NAME, TYPE)                                                      \
    int (NAME)(const char *const file, const int line, const TYPE *const buf1,           \
               const ptrdiff_t stride1, const TYPE *const buf2, const ptrdiff_t stride2, \
               const int w, const int h, const char *const buf_name, const int align_w,  \
               const int align_h, const int padding)

#define DECL_CHECKASM_CHECK_FUNC(type)                                                   \
    CHECKASM_API DECL_CHECK_FUNC(checkasm_check_impl_##type, type)

DECL_CHECKASM_CHECK_FUNC(int);
DECL_CHECKASM_CHECK_FUNC(int8_t);
DECL_CHECKASM_CHECK_FUNC(int16_t);
DECL_CHECKASM_CHECK_FUNC(int32_t);

DECL_CHECKASM_CHECK_FUNC(unsigned);
DECL_CHECKASM_CHECK_FUNC(uint8_t);
DECL_CHECKASM_CHECK_FUNC(uint16_t);
DECL_CHECKASM_CHECK_FUNC(uint32_t);

/**
 * @brief Compare float buffers with ULP tolerance
 */
CHECKASM_API int checkasm_check_impl_float_ulp(const char *file, int line,
                                               const float *buf1, ptrdiff_t stride1,
                                               const float *buf2, ptrdiff_t stride2,
                                               int w, int h, const char *name,
                                               unsigned max_ulp, int align_w, int align_h,
                                               int padding);

#define checkasm_check_impl2(type) checkasm_check_impl_##type
#define checkasm_check_impl(type)  checkasm_check_impl2(type)
#define checkasm_check1(type, ...) checkasm_check_impl_##type(__VA_ARGS__)
#define checkasm_check2(type, ...) checkasm_check1(type, __FILE__, __LINE__, __VA_ARGS__)

/** @} */ /* internal */

/**
 * @def checkasm_check2d(type, buf1, stride1, buf2, stride2, w, h, name, ...)
 * @brief Compare two 2D buffers and fail test if different
 * @param type Element type (e.g., uint8_t, int, float)
 * @param buf1 First buffer pointer to compare
 * @param stride1 First buffer stride in bytes
 * @param buf2 Second buffer pointer to compare
 * @param stride2 Second buffer stride in bytes
 * @param w Width of the buffers in elements
 * @param h Height of the buffers in lines
 * @param name Name of the buffer (for error reporting)
 * @param ... Extra parameters (e.g. max_ulp for checkasm_check2d(float_ulp, ...))
 * @note This will automatically print a hexdump of the differing regions on
 *       failure, if verbose mode is enabled.
 *
 * @code
 * CHECKASM_ALIGN(uint8_t buf1[64][64]);
 * CHECKASM_ALIGN(uint8_t buf2[64][64]);
 * const ptrdiff_t stride = sizeof(buf1[0]);
 *
 * for (int h = 8; h <= 64; h <<= 1) {
 *     for (int w = 8; w <= 64; w <<= 1) {
 *         if (checkasm_check_func(..., "myfunc_%dx%d", w, h)) {
 *             checkasm_call_ref(buf1, strude, w, h);
 *             checkasm_call_new(buf2, strude, w, h);
 *             checkasm_check2d(uint8_t, buf1, stride, buf2, stride, w, h, "buffer");
 *         }
 * }
 * @endcode
 */
#define checkasm_check2d(type, ...) checkasm_check2(type, __VA_ARGS__, 0, 0, 0)

/**
 * @def checkasm_check2d_padded(type, buf1, stride1, buf2, stride2, w, h, name,
 *                              ..., align_w, align_h, padding)
 * @brief Compare two 2D buffers, including padding regions (detect over-write)
 * @param type Element type (e.g., uint8_t, int, float)
 * @param buf1 First buffer pointer to compare
 * @param stride1 First buffer stride in bytes
 * @param buf2 Second buffer pointer to compare
 * @param stride2 Second buffer stride in bytes
 * @param w Width of the buffers in elements
 * @param h Height of the buffers in lines
 * @param name Name of the buffer (for error reporting)
 * @param ... Extra parameters (e.g. max_ulp for checkasm_check2d_padded(float_ulp, ...))
 * @param align_w Horizontal alignment of the allowed over-write (elements)
 * @param align_h Vertical alignment of the allowed over-write (lines), or
 *                0 to disable top/bottom overwrite checks.
 * @param padding Number of extra elements/lines of padding to check (past the
 *        alignment boundaries)
 * @see checkasm_check2d(), checkasm_check_rect_padded()
 */
#define checkasm_check2d_padded(type, ...) checkasm_check2(type, __VA_ARGS__)

/**
 * @def checkasm_check1d(type, buf1, buf2, len, name, ...)
 * @brief Compare two 1D buffers and fail test if different
 * @param type Element type (e.g., uint8_t, int, float)
 * @param buf1 First buffer pointer to compare
 * @param buf2 Second buffer pointer to compare
 * @param len Length of the buffers in elements
 * @param ... Extra parameters (e.g. max_ulp for checkasm_check(float_ulp, ...))
 * @see checkasm_check2d()
 *
 * @code
 * CHECKASM_ALIGN(uint8_t buf1[256]);
 * CHECKASM_ALIGN(uint8_t buf2[256]);
 * for (int w = 8; w <= 256; w <<= 1) {
 *     if (checkasm_check_func(..., "myfunc_w%d", w)) {
 *         checkasm_call_ref(buf1, w);
 *         checkasm_call_new(buf2, w);
 *         checkasm_check1d(uint8_t, buf1, buf2, w, "buffer");
 *     }
 */
#define checkasm_check1d(type, buf1, buf2, len, ...) \
    checkasm_check2d(type, buf1, 0, buf2, 0, len, 1, __VA_ARGS__)

/**
 * @def checkasm_check1d_padded(type, buf1, buf2, len, name, align_w, padding)
 * @brief Compare two 1D buffers, including padding regions (detect over-write)
 * @param type Element type (e.g., uint8_t, int, float)
 * @param buf1 First buffer pointer to compare
 * @param buf2 Second buffer pointer to compare
 * @param len Length of the buffers in elements
 * @param name Name of the buffer (for error reporting)
 * @param align Alignment of the allowed over-write (elements)
 * @param padding Number of extra elements of padding to check (past the
 *        alignment boundary)
 * @see checkasm_check2d_padded()
 *
 * @note For implementation reasons, this macro does not accept variadic
 *       parameters (e.g. for `float_ulp` checks). If you need those, use
 *       `checkasm_check2d_padded()` with `align_h = 0` instead.
 */
#define checkasm_check1d_padded(type, buf1, buf2, len, name, align, padding) \
    checkasm_check2d_padded(type, buf1, 0, buf2, 0, len, 1, name, align, 0, padding)

/** @} */ /* bufcmp */

/** @addtogroup aliases
 *  @{ */
#define checkasm_check        checkasm_check2d
#define checkasm_check_padded checkasm_check2d_padded
/* @} */

/**
 * @defgroup bufrect Rectangular Buffer Helpers
 * @brief Macros for creating aligned, padded 2D test buffers
 *
 * These macros simplify creating properly aligned and padded rectangular
 * buffers for testing, including automatic stride calculation and padding
 * detection support.
 * @{
 */

/**
 * @def CHECKASM_ROUND(x, a)
 * @brief Round up to nearest multiple of a
 * @param x Value to round
 * @param a Alignment (must be power of 2)
 * @private
 */
#define CHECKASM_ROUND(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

/**
 * @def BUF_RECT(type, name, w, h)
 * @brief Declare an aligned, padded rectangular buffer
 *
 * Creates a properly aligned rectangular buffer with padding on all sides for
 * use with checkasm_check_rect_padded(). Sets up associated metadata (stride,
 * height) and declares a pointer to the usable data region.
 *
 * @param type Element type (e.g., uint8_t, int16_t)
 * @param name Base name for the buffer variables
 * @param w Width of the usable buffer region
 * @param h Height of the usable buffer region
 *
 * Creates:
 * - name_buf: Full buffer array (with padding)
 * - name_stride: Stride in bytes
 * - name_buf_h: Total buffer height in lines (with padding)
 * - name: Pointer to start of usable region
 *
 * @code
 * BUF_RECT(uint8_t, src, 64, 32);
 * BUF_RECT(uint8_t, dst, 64, 32);
 * // uint8_t *src, *dst; // now point to a 64x32 usable area
 * INITIALIZE_BUF_RECT(src);
 * CLEAR_BUF_RECT(dst);
 * // ...
 * checkasm_call_new(dst, dst_stride, src, src_stride, 64, 32);
 * @endcode
 */
#define BUF_RECT(type, name, w, h)                                                       \
    DECL_CHECK_FUNC(*checkasm_check_impl_##name##_type, type)                            \
        = checkasm_check_impl_##type;                                                    \
    CHECKASM_ALIGN(type name##_buf[((h) + 32) * (CHECKASM_ROUND(w, 64) + 64) + 64]);     \
    const int name##_buf_w = CHECKASM_ROUND(w, 64) + 64;                                 \
    const int name##_buf_h = (h) + 32;                                                   \
    ptrdiff_t name##_stride = sizeof(type) * name##_buf_w;                               \
    (void) checkasm_check_impl(name##_type);                                             \
    (void) name##_stride;                                                                \
    (void) name##_buf_h;                                                                 \
    type *name = name##_buf + name##_buf_w * 16 + 64

/**
 * @def CLEAR_BUF_RECT(name)
 * @brief Clear a rectangular buffer (including padding)
 * @param name Buffer name (from BUF_RECT)
 * @see checkasm_clear()
 */
#define CLEAR_BUF_RECT(name) CLEAR_BUF(name##_buf)

/**
 * @def INITIALIZE_BUF_RECT(name)
 * @brief Initialize a rectangular buffer (including padding) with pathological values
 * @param name Buffer name (from BUF_RECT)
 * @see checkasm_init()
 */
#define INITIALIZE_BUF_RECT(name) INITIALIZE_BUF(name##_buf)

/**
 * @def RANDOMIZE_BUF_RECT(name)
 * @brief Randomize a rectangular buffer (including padding)
 * @param name Buffer name (from BUF_RECT)
 * @see checkasm_randomize()
 */
#define RANDOMIZE_BUF_RECT(name) RANDOMIZE_BUF(name##_buf)

/**
 * @def checkasm_check_rect(rect1, ...)
 * @brief Compare two rectangular buffers
 * @param rect1 First buffer (from BUF_RECT)
 * @param ... rect2, stride2, w, h, name
 * @see checkasm_check2d()
 */
#define checkasm_check_rect(rect1, ...) checkasm_check2d(rect1##_type, rect1, __VA_ARGS__)

/**
 * @def checkasm_check_rect_padded(rect1, ...)
 * @brief Compare two rectangular buffers including padding
 * @param rect1 First buffer (from BUF_RECT)
 * @param ... rect2, stride2, w, h, name
 * @see checkasm_check2d()
 */
#define checkasm_check_rect_padded(rect1, ...)                                           \
    checkasm_check2d_padded(rect1##_type, rect1, __VA_ARGS__, 1, 1, 8)

/**
 * @def checkasm_check_rect_padded_align(rect1, ...)
 * @brief Compare two rectangular buffers, with custom alignment (over-write)
 * @param rect1 First buffer (from BUF_RECT)
 * @param ... rect2, stride2, w, h, name, align
 * @see checkasm_check2d_padded()
 *
 * @code
 * // Code is allowed to over-write up to 16 elements on the right edge only
 * checkasm_check_rect_padded_align(src, src_stride, dst, dst_stride, w, h,
 *                                  "buffer", 16, 1);
 * @endcode
 */
#define checkasm_check_rect_padded_align(rect1, ...)                                     \
    checkasm_check2d_padded(rect1##_type, rect1, __VA_ARGS__, 8)

/**
 * @def CHECK_BUF_RECT(buf1, buf2, w, h)
 * @brief Compare two rectangular buffers (convenience macro)
 * @param buf1 First buffer (from BUF_RECT)
 * @param buf2 Second buffer (from BUF_RECT)
 * @param w Width of the usable buffer region
 * @param h Height of the usable buffer region
 * @see checkasm_check_rect_padded()
 */
#define CHECK_BUF_RECT(buf1, buf2, w, h)                                                 \
    checkasm_check_rect_padded(buf1, buf1##_stride, buf2, buf2##_stride, w, h,           \
                               #buf1 " vs " #buf2)

/** @} */ /* bufrect */

#endif /* CHECKASM_UTILS_H */
