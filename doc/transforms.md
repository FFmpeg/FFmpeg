The basis transforms used for FFT and various other derived functions are based
on the following unrollings.
The functions can be easily adapted to double precision floats as well.

# Parity permutation
The basis transforms described here all use the following permutation:

``` C
void ff_tx_gen_split_radix_parity_revtab(int *revtab, int len, int inv,
                                         int basis, int dual_stride);
```
Parity means even and odd complex numbers will be split, e.g. the even
coefficients will come first, after which the odd coefficients will be
placed. For example, a 4-point transform's coefficients after reordering:
`z[0].re, z[0].im, z[2].re, z[2].im, z[1].re, z[1].im, z[3].re, z[3].im`

The basis argument is the length of the largest non-composite transform
supported, and also implies that the basis/2 transform is supported as well,
as the split-radix algorithm requires it to be.

The dual_stride argument indicates that both the basis, as well as the
basis/2 transforms support doing two transforms at once, and the coefficients
will be interleaved between each pair in a split-radix like so (stride == 2):
`tx1[0], tx1[2], tx2[0], tx2[2], tx1[1], tx1[3], tx2[1], tx2[3]`
A non-zero number switches this on, with the value indicating the stride
(how many values of 1 transform to put first before switching to the other).
Must be a power of two or 0. Must be less than the basis.
Value will be clipped to the transform size, so for a basis of 16 and a
dual_stride of 8, dual 8-point transforms will be laid out as if dual_stride
was set to 4.
Usually you'll set this to half the complex numbers that fit in a single
register or 0. This allows to reuse SSE functions as dual-transform
functions in AVX mode.
If length is smaller than basis/2 this function will not do anything.

# 4-point FFT transform
The only permutation this transform needs is to swap the `z[1]` and `z[2]`
elements when performing an inverse transform, which in the assembly code is
hardcoded with the function itself being templated and duplicated for each
direction.

``` C
static void fft4(FFTComplex *z)
{
    FFTSample r1 = z[0].re - z[2].re;
    FFTSample r2 = z[0].im - z[2].im;
    FFTSample r3 = z[1].re - z[3].re;
    FFTSample r4 = z[1].im - z[3].im;
    /* r5-r8 second transform */

    FFTSample t1 = z[0].re + z[2].re;
    FFTSample t2 = z[0].im + z[2].im;
    FFTSample t3 = z[1].re + z[3].re;
    FFTSample t4 = z[1].im + z[3].im;
    /* t5-t8 second transform */

    /* 1sub + 1add = 2 instructions */

    /* 2 shufs */
    FFTSample a3 = t1 - t3;
    FFTSample a4 = t2 - t4;
    FFTSample b3 = r1 - r4;
    FFTSample b2 = r2 - r3;

    FFTSample a1 = t1 + t3;
    FFTSample a2 = t2 + t4;
    FFTSample b1 = r1 + r4;
    FFTSample b4 = r2 + r3;
    /* 1 add 1 sub 3 shufs */

    z[0].re = a1;
    z[0].im = a2;
    z[2].re = a3;
    z[2].im = a4;

    z[1].re = b1;
    z[1].im = b2;
    z[3].re = b3;
    z[3].im = b4;
}
```

# 8-point AVX FFT transform
Input must be pre-permuted using the parity lookup table, generated via
`ff_tx_gen_split_radix_parity_revtab`.

``` C
static void fft8(FFTComplex *z)
{
    FFTSample r1 = z[0].re - z[4].re;
    FFTSample r2 = z[0].im - z[4].im;
    FFTSample r3 = z[1].re - z[5].re;
    FFTSample r4 = z[1].im - z[5].im;

    FFTSample r5 = z[2].re - z[6].re;
    FFTSample r6 = z[2].im - z[6].im;
    FFTSample r7 = z[3].re - z[7].re;
    FFTSample r8 = z[3].im - z[7].im;

    FFTSample q1 = z[0].re + z[4].re;
    FFTSample q2 = z[0].im + z[4].im;
    FFTSample q3 = z[1].re + z[5].re;
    FFTSample q4 = z[1].im + z[5].im;

    FFTSample q5 = z[2].re + z[6].re;
    FFTSample q6 = z[2].im + z[6].im;
    FFTSample q7 = z[3].re + z[7].re;
    FFTSample q8 = z[3].im + z[7].im;

    FFTSample s3 = q1 - q3;
    FFTSample s1 = q1 + q3;
    FFTSample s4 = q2 - q4;
    FFTSample s2 = q2 + q4;

    FFTSample s7 = q5 - q7;
    FFTSample s5 = q5 + q7;
    FFTSample s8 = q6 - q8;
    FFTSample s6 = q6 + q8;

    FFTSample e1 = s1 * -1;
    FFTSample e2 = s2 * -1;
    FFTSample e3 = s3 * -1;
    FFTSample e4 = s4 * -1;

    FFTSample e5 = s5 *  1;
    FFTSample e6 = s6 *  1;
    FFTSample e7 = s7 * -1;
    FFTSample e8 = s8 *  1;

    FFTSample w1 =  e5 - e1;
    FFTSample w2 =  e6 - e2;
    FFTSample w3 =  e8 - e3;
    FFTSample w4 =  e7 - e4;

    FFTSample w5 =  s1 - e5;
    FFTSample w6 =  s2 - e6;
    FFTSample w7 =  s3 - e8;
    FFTSample w8 =  s4 - e7;

    z[0].re = w1;
    z[0].im = w2;
    z[2].re = w3;
    z[2].im = w4;
    z[4].re = w5;
    z[4].im = w6;
    z[6].re = w7;
    z[6].im = w8;

    FFTSample z1 = r1 - r4;
    FFTSample z2 = r1 + r4;
    FFTSample z3 = r3 - r2;
    FFTSample z4 = r3 + r2;

    FFTSample z5 = r5 - r6;
    FFTSample z6 = r5 + r6;
    FFTSample z7 = r7 - r8;
    FFTSample z8 = r7 + r8;

    z3 *= -1;
    z5 *= -M_SQRT1_2;
    z6 *= -M_SQRT1_2;
    z7 *=  M_SQRT1_2;
    z8 *=  M_SQRT1_2;

    FFTSample t5 = z7 - z6;
    FFTSample t6 = z8 + z5;
    FFTSample t7 = z8 - z5;
    FFTSample t8 = z7 + z6;

    FFTSample u1 =  z2 + t5;
    FFTSample u2 =  z3 + t6;
    FFTSample u3 =  z1 - t7;
    FFTSample u4 =  z4 + t8;

    FFTSample u5 =  z2 - t5;
    FFTSample u6 =  z3 - t6;
    FFTSample u7 =  z1 + t7;
    FFTSample u8 =  z4 - t8;

    z[1].re = u1;
    z[1].im = u2;
    z[3].re = u3;
    z[3].im = u4;
    z[5].re = u5;
    z[5].im = u6;
    z[7].re = u7;
    z[7].im = u8;
}
```

As you can see, there are 2 independent paths, one for even and one for odd coefficients.
This theme continues throughout the document. Note that in the actual assembly code,
the paths are interleaved to improve unit saturation and CPU dependency tracking, so
to more clearly see them, you'll need to deinterleave the instructions.

# 8-point SSE/ARM64 FFT transform
Input must be pre-permuted using the parity lookup table, generated via
`ff_tx_gen_split_radix_parity_revtab`.

``` C
static void fft8(FFTComplex *z)
{
    FFTSample r1 = z[0].re - z[4].re;
    FFTSample r2 = z[0].im - z[4].im;
    FFTSample r3 = z[1].re - z[5].re;
    FFTSample r4 = z[1].im - z[5].im;

    FFTSample j1 = z[2].re - z[6].re;
    FFTSample j2 = z[2].im - z[6].im;
    FFTSample j3 = z[3].re - z[7].re;
    FFTSample j4 = z[3].im - z[7].im;

    FFTSample q1 = z[0].re + z[4].re;
    FFTSample q2 = z[0].im + z[4].im;
    FFTSample q3 = z[1].re + z[5].re;
    FFTSample q4 = z[1].im + z[5].im;

    FFTSample k1 = z[2].re + z[6].re;
    FFTSample k2 = z[2].im + z[6].im;
    FFTSample k3 = z[3].re + z[7].re;
    FFTSample k4 = z[3].im + z[7].im;
    /* 2 add 2 sub = 4 */

    /* 2 shufs, 1 add 1 sub = 4 */
    FFTSample s1 = q1 + q3;
    FFTSample s2 = q2 + q4;
    FFTSample g1 = k3 + k1;
    FFTSample g2 = k2 + k4;

    FFTSample s3 = q1 - q3;
    FFTSample s4 = q2 - q4;
    FFTSample g4 = k3 - k1;
    FFTSample g3 = k2 - k4;

    /* 1 unpack + 1 shuffle = 2 */

    /* 1 add */
    FFTSample w1 =  s1 + g1;
    FFTSample w2 =  s2 + g2;
    FFTSample w3 =  s3 + g3;
    FFTSample w4 =  s4 + g4;

    /* 1 sub */
    FFTSample h1 =  s1 - g1;
    FFTSample h2 =  s2 - g2;
    FFTSample h3 =  s3 - g3;
    FFTSample h4 =  s4 - g4;

    z[0].re = w1;
    z[0].im = w2;
    z[2].re = w3;
    z[2].im = w4;
    z[4].re = h1;
    z[4].im = h2;
    z[6].re = h3;
    z[6].im = h4;

    /* 1 shuf + 1 shuf + 1 xor + 1 addsub */
    FFTSample z1 = r1 + r4;
    FFTSample z2 = r2 - r3;
    FFTSample z3 = r1 - r4;
    FFTSample z4 = r2 + r3;

    /* 1 mult */
    j1 *=  M_SQRT1_2;
    j2 *= -M_SQRT1_2;
    j3 *= -M_SQRT1_2;
    j4 *=  M_SQRT1_2;

    /* 1 shuf + 1 addsub */
    FFTSample l2 = j1 - j2;
    FFTSample l1 = j2 + j1;
    FFTSample l4 = j3 - j4;
    FFTSample l3 = j4 + j3;

    /* 1 shuf + 1 addsub */
    FFTSample t1 = l3 - l2;
    FFTSample t2 = l4 + l1;
    FFTSample t3 = l1 - l4;
    FFTSample t4 = l2 + l3;

    /* 1 add */
    FFTSample u1 =  z1 - t1;
    FFTSample u2 =  z2 - t2;
    FFTSample u3 =  z3 - t3;
    FFTSample u4 =  z4 - t4;

    /* 1 sub */
    FFTSample o1 =  z1 + t1;
    FFTSample o2 =  z2 + t2;
    FFTSample o3 =  z3 + t3;
    FFTSample o4 =  z4 + t4;

    z[1].re = u1;
    z[1].im = u2;
    z[3].re = u3;
    z[3].im = u4;
    z[5].re = o1;
    z[5].im = o2;
    z[7].re = o3;
    z[7].im = o4;
}
```

Most functions here are highly tuned to use x86's addsub instruction to save on
external sign mask loading.

# 16-point AVX FFT transform
This version expects the output of the 8 and 4-point transforms to follow the
even/odd convention established above.

``` C
static void fft16(FFTComplex *z)
{
    FFTSample cos_16_1 = 0.92387950420379638671875f;
    FFTSample cos_16_3 = 0.3826834261417388916015625f;

    fft8(z);
    fft4(z+8);
    fft4(z+10);

    FFTSample s[32];

    /*
        xorps m1, m1 - free
        mulps m0
        shufps m1, m1, m0
        xorps
        addsub
        shufps
        mulps
        mulps
        addps
        or (fma3)
        shufps
        shufps
        mulps
        mulps
        fma
        fma
     */

    s[0]  =  z[8].re*( 1) - z[8].im*( 0);
    s[1]  =  z[8].im*( 1) + z[8].re*( 0);
    s[2]  =  z[9].re*( 1) - z[9].im*(-1);
    s[3]  =  z[9].im*( 1) + z[9].re*(-1);

    s[4]  = z[10].re*( 1) - z[10].im*( 0);
    s[5]  = z[10].im*( 1) + z[10].re*( 0);
    s[6]  = z[11].re*( 1) - z[11].im*( 1);
    s[7]  = z[11].im*( 1) + z[11].re*( 1);

    s[8]  = z[12].re*(  cos_16_1) - z[12].im*( -cos_16_3);
    s[9]  = z[12].im*(  cos_16_1) + z[12].re*( -cos_16_3);
    s[10] = z[13].re*(  cos_16_3) - z[13].im*( -cos_16_1);
    s[11] = z[13].im*(  cos_16_3) + z[13].re*( -cos_16_1);

    s[12] = z[14].re*(  cos_16_1) - z[14].im*(  cos_16_3);
    s[13] = z[14].im*( -cos_16_1) + z[14].re*( -cos_16_3);
    s[14] = z[15].re*(  cos_16_3) - z[15].im*(  cos_16_1);
    s[15] = z[15].im*( -cos_16_3) + z[15].re*( -cos_16_1);

    s[2] *=  M_SQRT1_2;
    s[3] *=  M_SQRT1_2;
    s[5] *= -1;
    s[6] *=  M_SQRT1_2;
    s[7] *= -M_SQRT1_2;

    FFTSample w5 =  s[0] + s[4];
    FFTSample w6 =  s[1] - s[5];
    FFTSample x5 =  s[2] + s[6];
    FFTSample x6 =  s[3] - s[7];

    FFTSample w3 =  s[4] - s[0];
    FFTSample w4 =  s[5] + s[1];
    FFTSample x3 =  s[6] - s[2];
    FFTSample x4 =  s[7] + s[3];

    FFTSample y5 =  s[8] + s[12];
    FFTSample y6 =  s[9] - s[13];
    FFTSample u5 = s[10] + s[14];
    FFTSample u6 = s[11] - s[15];

    FFTSample y3 = s[12] - s[8];
    FFTSample y4 = s[13] + s[9];
    FFTSample u3 = s[14] - s[10];
    FFTSample u4 = s[15] + s[11];

    /* 2xorps, 2vperm2fs, 2 adds, 2 vpermilps = 8 */

    FFTSample o1  = z[0].re + w5;
    FFTSample o2  = z[0].im + w6;
    FFTSample o5  = z[1].re + x5;
    FFTSample o6  = z[1].im + x6;
    FFTSample o9  = z[2].re + w4; //h
    FFTSample o10 = z[2].im + w3;
    FFTSample o13 = z[3].re + x4;
    FFTSample o14 = z[3].im + x3;

    FFTSample o17 = z[0].re - w5;
    FFTSample o18 = z[0].im - w6;
    FFTSample o21 = z[1].re - x5;
    FFTSample o22 = z[1].im - x6;
    FFTSample o25 = z[2].re - w4; //h
    FFTSample o26 = z[2].im - w3;
    FFTSample o29 = z[3].re - x4;
    FFTSample o30 = z[3].im - x3;

    FFTSample o3  = z[4].re + y5;
    FFTSample o4  = z[4].im + y6;
    FFTSample o7  = z[5].re + u5;
    FFTSample o8  = z[5].im + u6;
    FFTSample o11 = z[6].re + y4; //h
    FFTSample o12 = z[6].im + y3;
    FFTSample o15 = z[7].re + u4;
    FFTSample o16 = z[7].im + u3;

    FFTSample o19 = z[4].re - y5;
    FFTSample o20 = z[4].im - y6;
    FFTSample o23 = z[5].re - u5;
    FFTSample o24 = z[5].im - u6;
    FFTSample o27 = z[6].re - y4; //h
    FFTSample o28 = z[6].im - y3;
    FFTSample o31 = z[7].re - u4;
    FFTSample o32 = z[7].im - u3;

    /* This is just deinterleaving, happens separately */
    z[0]  = (FFTComplex){  o1,  o2 };
    z[1]  = (FFTComplex){  o3,  o4 };
    z[2]  = (FFTComplex){  o5,  o6 };
    z[3]  = (FFTComplex){  o7,  o8 };
    z[4]  = (FFTComplex){  o9, o10 };
    z[5]  = (FFTComplex){ o11, o12 };
    z[6]  = (FFTComplex){ o13, o14 };
    z[7]  = (FFTComplex){ o15, o16 };

    z[8]  = (FFTComplex){ o17, o18 };
    z[9]  = (FFTComplex){ o19, o20 };
    z[10] = (FFTComplex){ o21, o22 };
    z[11] = (FFTComplex){ o23, o24 };
    z[12] = (FFTComplex){ o25, o26 };
    z[13] = (FFTComplex){ o27, o28 };
    z[14] = (FFTComplex){ o29, o30 };
    z[15] = (FFTComplex){ o31, o32 };
}
```

# AVX split-radix synthesis
To create larger transforms, the following unrolling of the C split-radix
function is used.

``` C
#define BF(x, y, a, b)                           \
    do {                                         \
        x = (a) - (b);                           \
        y = (a) + (b);                           \
    } while (0)

#define BUTTERFLIES(a0,a1,a2,a3)               \
    do {                                       \
        r0=a0.re;                              \
        i0=a0.im;                              \
        r1=a1.re;                              \
        i1=a1.im;                              \
        BF(q3, q5, q5, q1);                    \
        BF(a2.re, a0.re, r0, q5);              \
        BF(a3.im, a1.im, i1, q3);              \
        BF(q4, q6, q2, q6);                    \
        BF(a3.re, a1.re, r1, q4);              \
        BF(a2.im, a0.im, i0, q6);              \
    } while (0)

#undef TRANSFORM
#define TRANSFORM(a0,a1,a2,a3,wre,wim)         \
    do {                                       \
        CMUL(q1, q2, a2.re, a2.im, wre, -wim); \
        CMUL(q5, q6, a3.re, a3.im, wre,  wim); \
        BUTTERFLIES(a0, a1, a2, a3);           \
    } while (0)

#define CMUL(dre, dim, are, aim, bre, bim)       \
    do {                                         \
        (dre) = (are) * (bre) - (aim) * (bim);   \
        (dim) = (are) * (bim) + (aim) * (bre);   \
    } while (0)

static void recombine(FFTComplex *z, const FFTSample *cos,
                      unsigned int n)
{
    const int o1 = 2*n;
    const int o2 = 4*n;
    const int o3 = 6*n;
    const FFTSample *wim = cos + o1 - 7;
    FFTSample q1, q2, q3, q4, q5, q6, r0, i0, r1, i1;

#if 0
    for (int i = 0; i < n; i += 4) {
#endif

#if 0
        TRANSFORM(z[ 0 + 0], z[ 0 + 4], z[o2 + 0], z[o2 + 2], cos[0], wim[7]);
        TRANSFORM(z[ 0 + 1], z[ 0 + 5], z[o2 + 1], z[o2 + 3], cos[2], wim[5]);
        TRANSFORM(z[ 0 + 2], z[ 0 + 6], z[o2 + 4], z[o2 + 6], cos[4], wim[3]);
        TRANSFORM(z[ 0 + 3], z[ 0 + 7], z[o2 + 5], z[o2 + 7], cos[6], wim[1]);

        TRANSFORM(z[o1 + 0], z[o1 + 4], z[o3 + 0], z[o3 + 2], cos[1], wim[6]);
        TRANSFORM(z[o1 + 1], z[o1 + 5], z[o3 + 1], z[o3 + 3], cos[3], wim[4]);
        TRANSFORM(z[o1 + 2], z[o1 + 6], z[o3 + 4], z[o3 + 6], cos[5], wim[2]);
        TRANSFORM(z[o1 + 3], z[o1 + 7], z[o3 + 5], z[o3 + 7], cos[7], wim[0]);
#else
        FFTSample h[8], j[8], r[8], w[8];
        FFTSample t[8];
        FFTComplex *m0 = &z[0];
        FFTComplex *m1 = &z[4];
        FFTComplex *m2 = &z[o2 + 0];
        FFTComplex *m3 = &z[o2 + 4];

        const FFTSample *t1  = &cos[0];
        const FFTSample *t2  = &wim[0];

        /* 2 loads (tabs) */

        /* 2 vperm2fs, 2 shufs (im), 2 shufs (tabs) */
        /* 1 xor, 1 add, 1 sub, 4 mults OR 2 mults, 2 fmas */
        /* 13 OR 10ish (-2 each for second passovers!) */

        w[0] = m2[0].im*t1[0] - m2[0].re*t2[7];
        w[1] = m2[0].re*t1[0] + m2[0].im*t2[7];
        w[2] = m2[1].im*t1[2] - m2[1].re*t2[5];
        w[3] = m2[1].re*t1[2] + m2[1].im*t2[5];
        w[4] = m3[0].im*t1[4] - m3[0].re*t2[3];
        w[5] = m3[0].re*t1[4] + m3[0].im*t2[3];
        w[6] = m3[1].im*t1[6] - m3[1].re*t2[1];
        w[7] = m3[1].re*t1[6] + m3[1].im*t2[1];

        j[0] = m2[2].im*t1[0] + m2[2].re*t2[7];
        j[1] = m2[2].re*t1[0] - m2[2].im*t2[7];
        j[2] = m2[3].im*t1[2] + m2[3].re*t2[5];
        j[3] = m2[3].re*t1[2] - m2[3].im*t2[5];
        j[4] = m3[2].im*t1[4] + m3[2].re*t2[3];
        j[5] = m3[2].re*t1[4] - m3[2].im*t2[3];
        j[6] = m3[3].im*t1[6] + m3[3].re*t2[1];
        j[7] = m3[3].re*t1[6] - m3[3].im*t2[1];

        /* 1 add + 1 shuf */
        t[1] = j[0] + w[0];
        t[0] = j[1] + w[1];
        t[3] = j[2] + w[2];
        t[2] = j[3] + w[3];
        t[5] = j[4] + w[4];
        t[4] = j[5] + w[5];
        t[7] = j[6] + w[6];
        t[6] = j[7] + w[7];

        /* 1 sub + 1 xor */
        r[0] =  (w[0] - j[0]);
        r[1] = -(w[1] - j[1]);
        r[2] =  (w[2] - j[2]);
        r[3] = -(w[3] - j[3]);
        r[4] =  (w[4] - j[4]);
        r[5] = -(w[5] - j[5]);
        r[6] =  (w[6] - j[6]);
        r[7] = -(w[7] - j[7]);

        /* Min: 2 subs, 2 adds, 2 vperm2fs (OPTIONAL) */
        m2[0].re = m0[0].re - t[0];
        m2[0].im = m0[0].im - t[1];
        m2[1].re = m0[1].re - t[2];
        m2[1].im = m0[1].im - t[3];
        m3[0].re = m0[2].re - t[4];
        m3[0].im = m0[2].im - t[5];
        m3[1].re = m0[3].re - t[6];
        m3[1].im = m0[3].im - t[7];

        m2[2].re = m1[0].re - r[0];
        m2[2].im = m1[0].im - r[1];
        m2[3].re = m1[1].re - r[2];
        m2[3].im = m1[1].im - r[3];
        m3[2].re = m1[2].re - r[4];
        m3[2].im = m1[2].im - r[5];
        m3[3].re = m1[3].re - r[6];
        m3[3].im = m1[3].im - r[7];

        m0[0].re = m0[0].re + t[0];
        m0[0].im = m0[0].im + t[1];
        m0[1].re = m0[1].re + t[2];
        m0[1].im = m0[1].im + t[3];
        m0[2].re = m0[2].re + t[4];
        m0[2].im = m0[2].im + t[5];
        m0[3].re = m0[3].re + t[6];
        m0[3].im = m0[3].im + t[7];

        m1[0].re = m1[0].re + r[0];
        m1[0].im = m1[0].im + r[1];
        m1[1].re = m1[1].re + r[2];
        m1[1].im = m1[1].im + r[3];
        m1[2].re = m1[2].re + r[4];
        m1[2].im = m1[2].im + r[5];
        m1[3].re = m1[3].re + r[6];
        m1[3].im = m1[3].im + r[7];

        /* Identical for below, but with the following parameters */
        m0 = &z[o1];
        m1 = &z[o1 + 4];
        m2 = &z[o3 + 0];
        m3 = &z[o3 + 4];
        t1  = &cos[1];
        t2  = &wim[-1];

        w[0] = m2[0].im*t1[0] - m2[0].re*t2[7];
        w[1] = m2[0].re*t1[0] + m2[0].im*t2[7];
        w[2] = m2[1].im*t1[2] - m2[1].re*t2[5];
        w[3] = m2[1].re*t1[2] + m2[1].im*t2[5];
        w[4] = m3[0].im*t1[4] - m3[0].re*t2[3];
        w[5] = m3[0].re*t1[4] + m3[0].im*t2[3];
        w[6] = m3[1].im*t1[6] - m3[1].re*t2[1];
        w[7] = m3[1].re*t1[6] + m3[1].im*t2[1];

        j[0] = m2[2].im*t1[0] + m2[2].re*t2[7];
        j[1] = m2[2].re*t1[0] - m2[2].im*t2[7];
        j[2] = m2[3].im*t1[2] + m2[3].re*t2[5];
        j[3] = m2[3].re*t1[2] - m2[3].im*t2[5];
        j[4] = m3[2].im*t1[4] + m3[2].re*t2[3];
        j[5] = m3[2].re*t1[4] - m3[2].im*t2[3];
        j[6] = m3[3].im*t1[6] + m3[3].re*t2[1];
        j[7] = m3[3].re*t1[6] - m3[3].im*t2[1];

        /* 1 add + 1 shuf */
        t[1] = j[0] + w[0];
        t[0] = j[1] + w[1];
        t[3] = j[2] + w[2];
        t[2] = j[3] + w[3];
        t[5] = j[4] + w[4];
        t[4] = j[5] + w[5];
        t[7] = j[6] + w[6];
        t[6] = j[7] + w[7];

        /* 1 sub + 1 xor */
        r[0] =  (w[0] - j[0]);
        r[1] = -(w[1] - j[1]);
        r[2] =  (w[2] - j[2]);
        r[3] = -(w[3] - j[3]);
        r[4] =  (w[4] - j[4]);
        r[5] = -(w[5] - j[5]);
        r[6] =  (w[6] - j[6]);
        r[7] = -(w[7] - j[7]);

        /* Min: 2 subs, 2 adds, 2 vperm2fs (OPTIONAL) */
        m2[0].re = m0[0].re - t[0];
        m2[0].im = m0[0].im - t[1];
        m2[1].re = m0[1].re - t[2];
        m2[1].im = m0[1].im - t[3];
        m3[0].re = m0[2].re - t[4];
        m3[0].im = m0[2].im - t[5];
        m3[1].re = m0[3].re - t[6];
        m3[1].im = m0[3].im - t[7];

        m2[2].re = m1[0].re - r[0];
        m2[2].im = m1[0].im - r[1];
        m2[3].re = m1[1].re - r[2];
        m2[3].im = m1[1].im - r[3];
        m3[2].re = m1[2].re - r[4];
        m3[2].im = m1[2].im - r[5];
        m3[3].re = m1[3].re - r[6];
        m3[3].im = m1[3].im - r[7];

        m0[0].re = m0[0].re + t[0];
        m0[0].im = m0[0].im + t[1];
        m0[1].re = m0[1].re + t[2];
        m0[1].im = m0[1].im + t[3];
        m0[2].re = m0[2].re + t[4];
        m0[2].im = m0[2].im + t[5];
        m0[3].re = m0[3].re + t[6];
        m0[3].im = m0[3].im + t[7];

        m1[0].re = m1[0].re + r[0];
        m1[0].im = m1[0].im + r[1];
        m1[1].re = m1[1].re + r[2];
        m1[1].im = m1[1].im + r[3];
        m1[2].re = m1[2].re + r[4];
        m1[2].im = m1[2].im + r[5];
        m1[3].re = m1[3].re + r[6];
        m1[3].im = m1[3].im + r[7];
#endif

#if 0
        z   +=   4; // !!!
        cos += 2*4;
        wim -= 2*4;
    }
#endif
}
```

The macros used are identical to those in the generic C version, only with all
variable declarations exported to the function body.
An important point here is that the high frequency registers (m2 and m3) have
their high and low halves swapped in the output. This is intentional, as the
inputs must also have the same layout, and therefore, the input swapping is only
performed once for the bottom-most basis transform, with all subsequent combinations
using the already swapped halves.

Also note that this function requires a special iteration way, due to coefficients
beginning to overlap, particularly `[o1]` with `[0]` after the second iteration.
To iterate further, set `z = &z[16]` via `z += 8` for the second iteration. After
the 4th iteration, the layout resets, so repeat the same.
