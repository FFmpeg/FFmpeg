/*
 * Copyright (c) 2005-2014 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <float.h>

#include "config.h"
#include "common.h"
#include "mem.h"
#include "avassert.h"
#include "avstring.h"
#include "bprint.h"

typedef struct FFFILE {
    size_t buf_size;
    unsigned char *buf;
    unsigned char *rpos, *rend;
    unsigned char *shend;
    ptrdiff_t shlim, shcnt;
    void *cookie;
    size_t (*read)(struct FFFILE *, unsigned char *, size_t);
} FFFILE;

#define SIZE_hh -2
#define SIZE_h  -1
#define SIZE_def 0
#define SIZE_l   1
#define SIZE_L   2
#define SIZE_ll  3

#define shcnt(f) ((f)->shcnt + ((f)->rpos - (f)->buf))

static int fftoread(FFFILE *f)
{
    f->rpos = f->rend = f->buf + f->buf_size;
    return 0;
}

static size_t ffstring_read(FFFILE *f, unsigned char *buf, size_t len)
{
    char *src = f->cookie;
    size_t k = len+256;
    char *end = memchr(src, 0, k);

    if (end) k = end-src;
    if (k < len) len = k;
    memcpy(buf, src, len);
    f->rpos = (void *)(src+len);
    f->rend = (void *)(src+k);
    f->cookie = src+k;

    return len;
}

static int ffuflow(FFFILE *f)
{
    unsigned char c;
    if (!fftoread(f) && f->read(f, &c, 1)==1) return c;
    return EOF;
}

static void ffshlim(FFFILE *f, ptrdiff_t lim)
{
    f->shlim = lim;
    f->shcnt = f->buf - f->rpos;
    /* If lim is nonzero, rend must be a valid pointer. */
    if (lim && f->rend - f->rpos > lim)
        f->shend = f->rpos + lim;
    else
        f->shend = f->rend;
}

static int ffshgetc(FFFILE *f)
{
    int c;
    ptrdiff_t cnt = shcnt(f);
    if (f->shlim && cnt >= f->shlim || (c=ffuflow(f)) < 0) {
        f->shcnt = f->buf - f->rpos + cnt;
        f->shend = 0;
        return EOF;
    }
    cnt++;
    if (f->shlim && f->rend - f->rpos > f->shlim - cnt)
        f->shend = f->rpos + (f->shlim - cnt);
    else
        f->shend = f->rend;
    f->shcnt = f->buf - f->rpos + cnt;
    if (f->rpos[-1] != c) f->rpos[-1] = c;
    return c;
}

#define shlim(f, lim) ffshlim((f), (lim))
#define shgetc(f) (((f)->rpos != (f)->shend) ? *(f)->rpos++ : ffshgetc(f))
#define shunget(f) ((f)->shend ? (void)(f)->rpos-- : (void)0)

static const unsigned char table[] = { -1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

static unsigned long long ffintscan(FFFILE *f, unsigned base, int pok, unsigned long long lim)
{
    const unsigned char *val = table+1;
    int c, neg=0;
    unsigned x;
    unsigned long long y;
    if (base > 36 || base == 1) {
        errno = EINVAL;
        return 0;
    }
    while (av_isspace((c=shgetc(f))));
    if (c=='+' || c=='-') {
        neg = -(c=='-');
        c = shgetc(f);
    }
    if ((base == 0 || base == 16) && c=='0') {
        c = shgetc(f);
        if ((c|32)=='x') {
            c = shgetc(f);
            if (val[c]>=16) {
                shunget(f);
                if (pok) shunget(f);
                else shlim(f, 0);
                return 0;
            }
            base = 16;
        } else if (base == 0) {
            base = 8;
        }
    } else {
        if (base == 0) base = 10;
        if (val[c] >= base) {
            shunget(f);
            shlim(f, 0);
            errno = EINVAL;
            return 0;
        }
    }
    if (base == 10) {
        for (x=0; c-'0'<10U && x<=UINT_MAX/10-1; c=shgetc(f))
            x = x*10 + (c-'0');
        for (y=x; c-'0'<10U && y<=ULLONG_MAX/10 && 10*y<=ULLONG_MAX-(c-'0'); c=shgetc(f))
            y = y*10 + (c-'0');
        if (c-'0'>=10U) goto done;
    } else if (!(base & base-1)) {
        int bs = "\0\1\2\4\7\3\6\5"[(0x17*base)>>5&7];
        for (x=0; val[c]<base && x<=UINT_MAX/32; c=shgetc(f))
            x = x<<bs | val[c];
        for (y=x; val[c]<base && y<=ULLONG_MAX>>bs; c=shgetc(f))
            y = y<<bs | val[c];
    } else {
        for (x=0; val[c]<base && x<=UINT_MAX/36-1; c=shgetc(f))
            x = x*base + val[c];
        for (y=x; val[c]<base && y<=ULLONG_MAX/base && base*y<=ULLONG_MAX-val[c]; c=shgetc(f))
            y = y*base + val[c];
    }
    if (val[c]<base) {
        for (; val[c]<base; c=shgetc(f));
        errno = ERANGE;
        y = lim;
        if (lim&1) neg = 0;
    }
done:
    shunget(f);
    if (y>=lim) {
        if (!(lim&1) && !neg) {
            errno = ERANGE;
            return lim-1;
        } else if (y>lim) {
            errno = ERANGE;
            return lim;
        }
    }
    return (y^neg)-neg;
}

static long long scanexp(FFFILE *f, int pok)
{
    int c;
    int x;
    long long y;
    int neg = 0;

    c = shgetc(f);
    if (c=='+' || c=='-') {
        neg = (c=='-');
        c = shgetc(f);
        if (c-'0'>=10U && pok) shunget(f);
    }
    if (c-'0'>=10U) {
        shunget(f);
        return LLONG_MIN;
    }
    for (x=0; c-'0'<10U && x<INT_MAX/10; c = shgetc(f))
        x = 10*x + (c-'0');
    for (y=x; c-'0'<10U && y<LLONG_MAX/100; c = shgetc(f))
        y = 10*y + (c-'0');
    for (; c-'0'<10U; c = shgetc(f));
    shunget(f);
    return neg ? -y : y;
}

#define LD_B1B_DIG 2
#define LD_B1B_MAX 9007199, 254740991
#define KMAX 128
#define MASK (KMAX-1)

static double decfloat(FFFILE *f, int c, int bits, int emin, int sign, int pok)
{
    uint32_t x[KMAX];
    static const uint32_t th[] = { LD_B1B_MAX };
    int i, j, k, a, z;
    long long lrp=0, dc=0;
    long long e10=0;
    int lnz = 0;
    int gotdig = 0, gotrad = 0;
    int rp;
    int e2;
    int emax = -emin-bits+3;
    int denormal = 0;
    double y;
    double frac=0;
    double bias=0;
    static const int p10s[] = { 10, 100, 1000, 10000,
        100000, 1000000, 10000000, 100000000 };

    j=0;
    k=0;

    /* Don't let leading zeros consume buffer space */
    for (; c=='0'; c = shgetc(f)) gotdig=1;
    if (c=='.') {
        gotrad = 1;
        for (c = shgetc(f); c=='0'; c = shgetc(f)) gotdig=1, lrp--;
    }

    x[0] = 0;
    for (; c-'0'<10U || c=='.'; c = shgetc(f)) {
        if (c == '.') {
            if (gotrad) break;
            gotrad = 1;
            lrp = dc;
        } else if (k < KMAX-3) {
            dc++;
            if (c!='0') lnz = dc;
            if (j) x[k] = x[k]*10 + c-'0';
            else x[k] = c-'0';
            if (++j==9) {
                k++;
                j=0;
            }
            gotdig=1;
        } else {
            dc++;
            if (c!='0') {
                lnz = (KMAX-4)*9;
                x[KMAX-4] |= 1;
            }
        }
    }
    if (!gotrad) lrp=dc;

    if (gotdig && (c|32)=='e') {
        e10 = scanexp(f, pok);
        if (e10 == LLONG_MIN) {
            if (pok) {
                shunget(f);
            } else {
                shlim(f, 0);
                return 0;
            }
            e10 = 0;
        }
        lrp += e10;
    } else if (c>=0) {
        shunget(f);
    }
    if (!gotdig) {
        errno = EINVAL;
        shlim(f, 0);
        return 0;
    }

    /* Handle zero specially to avoid nasty special cases later */
    if (!x[0]) return sign * 0.0;

    /* Optimize small integers (w/no exponent) and over/under-flow */
    if (lrp==dc && dc<10 && (bits>30 || x[0]>>bits==0))
        return sign * (double)x[0];
    if (lrp > -emin/2) {
        errno = ERANGE;
        return sign * DBL_MAX * DBL_MAX;
    }
    if (lrp < emin-2*DBL_MANT_DIG) {
        errno = ERANGE;
        return sign * DBL_MIN * DBL_MIN;
    }

    /* Align incomplete final B1B digit */
    if (j) {
        for (; j<9; j++) x[k]*=10;
        k++;
        j=0;
    }

    a = 0;
    z = k;
    e2 = 0;
    rp = lrp;

    /* Optimize small to mid-size integers (even in exp. notation) */
    if (lnz<9 && lnz<=rp && rp < 18) {
        int bitlim;
        if (rp == 9) return sign * (double)x[0];
        if (rp < 9) return sign * (double)x[0] / p10s[8-rp];
        bitlim = bits-3*(int)(rp-9);
        if (bitlim>30 || x[0]>>bitlim==0)
            return sign * (double)x[0] * p10s[rp-10];
    }

    /* Drop trailing zeros */
    for (; !x[z-1]; z--);

    /* Align radix point to B1B digit boundary */
    if (rp % 9) {
        int rpm9 = rp>=0 ? rp%9 : rp%9+9;
        int p10 = p10s[8-rpm9];
        uint32_t carry = 0;
        for (k=a; k!=z; k++) {
            uint32_t tmp = x[k] % p10;
            x[k] = x[k]/p10 + carry;
            carry = 1000000000/p10 * tmp;
            if (k==a && !x[k]) {
                a = (a+1 & MASK);
                rp -= 9;
            }
        }
        if (carry) x[z++] = carry;
        rp += 9-rpm9;
    }

    /* Upscale until desired number of bits are left of radix point */
    while (rp < 9*LD_B1B_DIG || (rp == 9*LD_B1B_DIG && x[a]<th[0])) {
        uint32_t carry = 0;
        e2 -= 29;
        for (k=(z-1 & MASK); ; k=(k-1 & MASK)) {
            uint64_t tmp = ((uint64_t)x[k] << 29) + carry;
            if (tmp > 1000000000) {
                carry = tmp / 1000000000;
                x[k] = tmp % 1000000000;
            } else {
                carry = 0;
                x[k] = tmp;
            }
            if (k==(z-1 & MASK) && k!=a && !x[k]) z = k;
            if (k==a) break;
        }
        if (carry) {
            rp += 9;
            a = (a-1 & MASK);
            if (a == z) {
                z = (z-1 & MASK);
                x[z-1 & MASK] |= x[z];
            }
            x[a] = carry;
        }
    }

    /* Downscale until exactly number of bits are left of radix point */
    for (;;) {
        uint32_t carry = 0;
        int sh = 1;
        for (i=0; i<LD_B1B_DIG; i++) {
            k = (a+i & MASK);
            if (k == z || x[k] < th[i]) {
                i=LD_B1B_DIG;
                break;
            }
            if (x[a+i & MASK] > th[i]) break;
        }
        if (i==LD_B1B_DIG && rp==9*LD_B1B_DIG) break;
        /* FIXME: find a way to compute optimal sh */
        if (rp > 9+9*LD_B1B_DIG) sh = 9;
        e2 += sh;
        for (k=a; k!=z; k=(k+1 & MASK)) {
            uint32_t tmp = x[k] & (1<<sh)-1;
            x[k] = (x[k]>>sh) + carry;
            carry = (1000000000>>sh) * tmp;
            if (k==a && !x[k]) {
                a = (a+1 & MASK);
                i--;
                rp -= 9;
            }
        }
        if (carry) {
            if ((z+1 & MASK) != a) {
                x[z] = carry;
                z = (z+1 & MASK);
            } else x[z-1 & MASK] |= 1;
        }
    }

    /* Assemble desired bits into floating point variable */
    for (y=i=0; i<LD_B1B_DIG; i++) {
        if ((a+i & MASK)==z) x[(z=(z+1 & MASK))-1] = 0;
        y = 1000000000.0L * y + x[a+i & MASK];
    }

    y *= sign;

    /* Limit precision for denormal results */
    if (bits > DBL_MANT_DIG+e2-emin) {
        bits = DBL_MANT_DIG+e2-emin;
        if (bits<0) bits=0;
        denormal = 1;
    }

    /* Calculate bias term to force rounding, move out lower bits */
    if (bits < DBL_MANT_DIG) {
        bias = copysign(scalbn(1, 2*DBL_MANT_DIG-bits-1), y);
        frac = fmod(y, scalbn(1, DBL_MANT_DIG-bits));
        y -= frac;
        y += bias;
    }

    /* Process tail of decimal input so it can affect rounding */
    if ((a+i & MASK) != z) {
        uint32_t t = x[a+i & MASK];
        if (t < 500000000 && (t || (a+i+1 & MASK) != z))
            frac += 0.25*sign;
        else if (t > 500000000)
            frac += 0.75*sign;
        else if (t == 500000000) {
            if ((a+i+1 & MASK) == z)
                frac += 0.5*sign;
            else
                frac += 0.75*sign;
        }
        if (DBL_MANT_DIG-bits >= 2 && !fmod(frac, 1))
            frac++;
    }

    y += frac;
    y -= bias;

    if ((e2+DBL_MANT_DIG & INT_MAX) > emax-5) {
        if (fabs(y) >= pow(2, DBL_MANT_DIG)) {
            if (denormal && bits==DBL_MANT_DIG+e2-emin)
                denormal = 0;
            y *= 0.5;
            e2++;
        }
        if (e2+DBL_MANT_DIG>emax || (denormal && frac))
            errno = ERANGE;
    }

    return scalbn(y, e2);
}

static double hexfloat(FFFILE *f, int bits, int emin, int sign, int pok)
{
    uint32_t x = 0;
    double y = 0;
    double scale = 1;
    double bias = 0;
    int gottail = 0, gotrad = 0, gotdig = 0;
    long long rp = 0;
    long long dc = 0;
    long long e2 = 0;
    int d;
    int c;

    c = shgetc(f);

    /* Skip leading zeros */
    for (; c=='0'; c = shgetc(f))
        gotdig = 1;

    if (c=='.') {
        gotrad = 1;
        c = shgetc(f);
        /* Count zeros after the radix point before significand */
        for (rp=0; c=='0'; c = shgetc(f), rp--) gotdig = 1;
    }

    for (; c-'0'<10U || (c|32)-'a'<6U || c=='.'; c = shgetc(f)) {
        if (c=='.') {
            if (gotrad) break;
            rp = dc;
            gotrad = 1;
        } else {
            gotdig = 1;
            if (c > '9') d = (c|32)+10-'a';
            else d = c-'0';
            if (dc<8) {
                x = x*16 + d;
            } else if (dc < DBL_MANT_DIG/4+1) {
                y += d*(scale/=16);
            } else if (d && !gottail) {
                y += 0.5*scale;
                gottail = 1;
            }
            dc++;
        }
    }
    if (!gotdig) {
        shunget(f);
        if (pok) {
            shunget(f);
            if (gotrad) shunget(f);
        } else {
            shlim(f, 0);
        }
        return sign * 0.0;
    }
    if (!gotrad) rp = dc;
    while (dc<8) x *= 16, dc++;
    if ((c|32)=='p') {
        e2 = scanexp(f, pok);
        if (e2 == LLONG_MIN) {
            if (pok) {
                shunget(f);
            } else {
                shlim(f, 0);
                return 0;
            }
            e2 = 0;
        }
    } else {
        shunget(f);
    }
    e2 += 4*rp - 32;

    if (!x) return sign * 0.0;
    if (e2 > -emin) {
        errno = ERANGE;
        return sign * DBL_MAX * DBL_MAX;
    }
    if (e2 < emin-2*DBL_MANT_DIG) {
        errno = ERANGE;
        return sign * DBL_MIN * DBL_MIN;
    }

    while (x < 0x80000000) {
        if (y>=0.5) {
            x += x + 1;
            y += y - 1;
        } else {
            x += x;
            y += y;
        }
        e2--;
    }

    if (bits > 32+e2-emin) {
        bits = 32+e2-emin;
        if (bits<0) bits=0;
    }

    if (bits < DBL_MANT_DIG)
        bias = copysign(scalbn(1, 32+DBL_MANT_DIG-bits-1), sign);

    if (bits<32 && y && !(x&1)) x++, y=0;

    y = bias + sign*(double)x + sign*y;
    y -= bias;

    if (!y) errno = ERANGE;

    return scalbn(y, e2);
}

static double fffloatscan(FFFILE *f, int prec, int pok)
{
    int sign = 1;
    size_t i;
    int bits;
    int emin;
    int c;

    switch (prec) {
    case 0:
        bits = FLT_MANT_DIG;
        emin = FLT_MIN_EXP-bits;
        break;
    case 1:
        bits = DBL_MANT_DIG;
        emin = DBL_MIN_EXP-bits;
        break;
    case 2:
        bits = DBL_MANT_DIG;
        emin = DBL_MIN_EXP-bits;
        break;
    default:
        return 0;
    }

    while (av_isspace((c = shgetc(f))));

    if (c=='+' || c=='-') {
        sign -= 2*(c=='-');
        c = shgetc(f);
    }

    for (i=0; i<8 && (c|32)=="infinity"[i]; i++)
        if (i<7) c = shgetc(f);
    if (i==3 || i==8 || (i>3 && pok)) {
        if (i!=8) {
            shunget(f);
            if (pok) for (; i>3; i--) shunget(f);
        }
        return sign * INFINITY;
    }
    if (!i) for (i=0; i<3 && (c|32)=="nan"[i]; i++)
        if (i<2) c = shgetc(f);
    if (i==3) {
        if (shgetc(f) != '(') {
            shunget(f);
            return NAN;
        }
        for (i=1; ; i++) {
            c = shgetc(f);
            if (c-'0'<10U || c-'A'<26U || c-'a'<26U || c=='_')
                continue;
            if (c==')') return NAN;
            shunget(f);
            if (!pok) {
                errno = EINVAL;
                shlim(f, 0);
                return 0;
            }
            while (i--) shunget(f);
            return NAN;
        }
        return NAN;
    }

    if (i) {
        shunget(f);
        errno = EINVAL;
        shlim(f, 0);
        return 0;
    }

    if (c=='0') {
        c = shgetc(f);
        if ((c|32) == 'x')
            return hexfloat(f, bits, emin, sign, pok);
        shunget(f);
        c = '0';
    }

    return decfloat(f, c, bits, emin, sign, pok);
}

static void *arg_n(va_list ap, unsigned int n)
{
    void *p;
    unsigned int i;
    va_list ap2;
    va_copy(ap2, ap);
    for (i=n; i>1; i--) va_arg(ap2, void *);
    p = va_arg(ap2, void *);
    va_end(ap2);
    return p;
}

static void store_int(void *dest, int size, unsigned long long i)
{
    if (!dest) return;
    switch (size) {
    case SIZE_hh:
        *(char *)dest = i;
        break;
    case SIZE_h:
        *(short *)dest = i;
        break;
    case SIZE_def:
        *(int *)dest = i;
        break;
    case SIZE_l:
        *(long *)dest = i;
        break;
    case SIZE_ll:
        *(long long *)dest = i;
        break;
    }
}

static int ff_vfscanf(FFFILE *f, const char *fmt, va_list ap)
{
    int width;
    int size;
    int base;
    const unsigned char *p;
    int c, t;
    char *s;
    void *dest=NULL;
    int invert;
    int matches=0;
    unsigned long long x;
    double y;
    ptrdiff_t pos = 0;
    unsigned char scanset[257];
    size_t i;

    for (p=(const unsigned char *)fmt; *p; p++) {

        if (av_isspace(*p)) {
            while (av_isspace(p[1])) p++;
            shlim(f, 0);
            while (av_isspace(shgetc(f)));
            shunget(f);
            pos += shcnt(f);
            continue;
        }
        if (*p != '%' || p[1] == '%') {
            shlim(f, 0);
            if (*p == '%') {
                p++;
                while (av_isspace((c=shgetc(f))));
            } else {
                c = shgetc(f);
            }
            if (c!=*p) {
                shunget(f);
                if (c<0) goto input_fail;
                goto match_fail;
            }
            pos += shcnt(f);
            continue;
        }

        p++;
        if (*p=='*') {
            dest = 0; p++;
        } else if (av_isdigit(*p) && p[1]=='$') {
            dest = arg_n(ap, *p-'0'); p+=2;
        } else {
            dest = va_arg(ap, void *);
        }

        for (width=0; av_isdigit(*p); p++) {
            width = 10*width + *p - '0';
        }

        if (*p=='m') {
            s = 0;
            p++;
        }

        size = SIZE_def;
        switch (*p++) {
        case 'h':
            if (*p == 'h') p++, size = SIZE_hh;
            else size = SIZE_h;
            break;
        case 'l':
            if (*p == 'l') p++, size = SIZE_ll;
            else size = SIZE_l;
            break;
        case 'j':
            size = SIZE_ll;
            break;
        case 'z':
        case 't':
            size = SIZE_l;
            break;
        case 'L':
            size = SIZE_L;
            break;
        case 'd': case 'i': case 'o': case 'u': case 'x':
        case 'a': case 'e': case 'f': case 'g':
        case 'A': case 'E': case 'F': case 'G': case 'X':
        case 's': case 'c': case '[':
        case 'S': case 'C':
        case 'p': case 'n':
            p--;
            break;
        default:
            goto fmt_fail;
        }

        t = *p;

        /* C or S */
        if ((t&0x2f) == 3) {
            t |= 32;
            size = SIZE_l;
        }

        switch (t) {
            case 'c':
                if (width < 1) width = 1;
            case '[':
                break;
            case 'n':
                store_int(dest, size, pos);
                /* do not increment match count, etc! */
                continue;
            default:
                shlim(f, 0);
                while (av_isspace(shgetc(f)));
                shunget(f);
                pos += shcnt(f);
        }

        shlim(f, width);
        if (shgetc(f) < 0) goto input_fail;
        shunget(f);

        switch (t) {
            case 's':
            case 'c':
            case '[':
                if (t == 'c' || t == 's') {
                    memset(scanset, -1, sizeof scanset);
                    scanset[0] = 0;
                    if (t == 's') {
                        scanset[1 + '\t'] = 0;
                        scanset[1 + '\n'] = 0;
                        scanset[1 + '\v'] = 0;
                        scanset[1 + '\f'] = 0;
                        scanset[1 + '\r'] = 0;
                        scanset[1 + ' ' ] = 0;
                    }
                } else {
                    if (*++p == '^') p++, invert = 1;
                    else invert = 0;
                    memset(scanset, invert, sizeof scanset);
                    scanset[0] = 0;
                    if (*p == '-') p++, scanset[1+'-'] = 1-invert;
                    else if (*p == ']') p++, scanset[1+']'] = 1-invert;
                    for (; *p != ']'; p++) {
                        if (!*p) goto fmt_fail;
                        if (*p=='-' && p[1] && p[1] != ']')
                            for (c=p++[-1]; c<*p; c++)
                                scanset[1+c] = 1-invert;
                        scanset[1+*p] = 1-invert;
                    }
                }
                s = 0;
                i = 0;
                if ((s = dest)) {
                    while (scanset[(c=shgetc(f))+1])
                        s[i++] = c;
                } else {
                    while (scanset[(c=shgetc(f))+1]);
                }
                shunget(f);
                if (!shcnt(f)) goto match_fail;
                if (t == 'c' && shcnt(f) != width) goto match_fail;
                if (t != 'c') {
                    if (s) s[i] = 0;
                }
                break;
            case 'p':
            case 'X':
            case 'x':
                base = 16;
                goto int_common;
            case 'o':
                base = 8;
                goto int_common;
            case 'd':
            case 'u':
                base = 10;
                goto int_common;
            case 'i':
                base = 0;
int_common:
                x = ffintscan(f, base, 0, ULLONG_MAX);
                if (!shcnt(f))
                    goto match_fail;
                if (t=='p' && dest)
                    *(void **)dest = (void *)(uintptr_t)x;
                else
                    store_int(dest, size, x);
                break;
            case 'a': case 'A':
            case 'e': case 'E':
            case 'f': case 'F':
            case 'g': case 'G':
                y = fffloatscan(f, size, 0);
                if (!shcnt(f))
                    goto match_fail;
                if (dest) {
                    switch (size) {
                    case SIZE_def:
                        *(float *)dest = y;
                        break;
                    case SIZE_l:
                        *(double *)dest = y;
                        break;
                    case SIZE_L:
                        *(double *)dest = y;
                        break;
                    }
                }
                break;
        }

        pos += shcnt(f);
        if (dest) matches++;
    }
    if (0) {
fmt_fail:
input_fail:
        if (!matches) matches--;
    }
match_fail:
    return matches;
}

static int ff_vsscanf(const char *s, const char *fmt, va_list ap)
{
    FFFILE f = {
        .buf = (void *)s, .cookie = (void *)s,
        .read = ffstring_read,
    };

    return ff_vfscanf(&f, fmt, ap);
}

int av_sscanf(const char *string, const char *format, ...)
{
    int ret;
    va_list ap;
    va_start(ap, format);
    ret = ff_vsscanf(string, format, ap);
    va_end(ap);
    return ret;
}
