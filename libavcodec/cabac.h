/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Context Adaptive Binary Arithmetic Coder.
 */

#ifndef AVCODEC_CABAC_H
#define AVCODEC_CABAC_H

#include "put_bits.h"

//#undef NDEBUG
#include <assert.h>
#include "libavutil/x86_cpu.h"

#define CABAC_BITS 16
#define CABAC_MASK ((1<<CABAC_BITS)-1)
#define BRANCHLESS_CABAC_DECODER 1
//#define ARCH_X86_DISABLED 1

typedef struct CABACContext{
    int low;
    int range;
    int outstanding_count;
#ifdef STRICT_LIMITS
    int symCount;
#endif
    const uint8_t *bytestream_start;
    const uint8_t *bytestream;
    const uint8_t *bytestream_end;
    PutBitContext pb;
}CABACContext;

extern uint8_t ff_h264_mlps_state[4*64];
extern uint8_t ff_h264_lps_range[4*2*64];  ///< rangeTabLPS
extern uint8_t ff_h264_mps_state[2*64];     ///< transIdxMPS
extern uint8_t ff_h264_lps_state[2*64];     ///< transIdxLPS
extern const uint8_t ff_h264_norm_shift[512];


void ff_init_cabac_encoder(CABACContext *c, uint8_t *buf, int buf_size);
void ff_init_cabac_decoder(CABACContext *c, const uint8_t *buf, int buf_size);
void ff_init_cabac_states(CABACContext *c);


static inline void put_cabac_bit(CABACContext *c, int b){
    put_bits(&c->pb, 1, b);
    for(;c->outstanding_count; c->outstanding_count--){
        put_bits(&c->pb, 1, 1-b);
    }
}

static inline void renorm_cabac_encoder(CABACContext *c){
    while(c->range < 0x100){
        //FIXME optimize
        if(c->low<0x100){
            put_cabac_bit(c, 0);
        }else if(c->low<0x200){
            c->outstanding_count++;
            c->low -= 0x100;
        }else{
            put_cabac_bit(c, 1);
            c->low -= 0x200;
        }

        c->range+= c->range;
        c->low += c->low;
    }
}

#ifdef TEST
static void put_cabac(CABACContext *c, uint8_t * const state, int bit){
    int RangeLPS= ff_h264_lps_range[2*(c->range&0xC0) + *state];

    if(bit == ((*state)&1)){
        c->range -= RangeLPS;
        *state= ff_h264_mps_state[*state];
    }else{
        c->low += c->range - RangeLPS;
        c->range = RangeLPS;
        *state= ff_h264_lps_state[*state];
    }

    renorm_cabac_encoder(c);

#ifdef STRICT_LIMITS
    c->symCount++;
#endif
}

static void put_cabac_static(CABACContext *c, int RangeLPS, int bit){
    assert(c->range > RangeLPS);

    if(!bit){
        c->range -= RangeLPS;
    }else{
        c->low += c->range - RangeLPS;
        c->range = RangeLPS;
    }

    renorm_cabac_encoder(c);

#ifdef STRICT_LIMITS
    c->symCount++;
#endif
}

/**
 * @param bit 0 -> write zero bit, !=0 write one bit
 */
static void put_cabac_bypass(CABACContext *c, int bit){
    c->low += c->low;

    if(bit){
        c->low += c->range;
    }
//FIXME optimize
    if(c->low<0x200){
        put_cabac_bit(c, 0);
    }else if(c->low<0x400){
        c->outstanding_count++;
        c->low -= 0x200;
    }else{
        put_cabac_bit(c, 1);
        c->low -= 0x400;
    }

#ifdef STRICT_LIMITS
    c->symCount++;
#endif
}

/**
 *
 * @return the number of bytes written
 */
static int put_cabac_terminate(CABACContext *c, int bit){
    c->range -= 2;

    if(!bit){
        renorm_cabac_encoder(c);
    }else{
        c->low += c->range;
        c->range= 2;

        renorm_cabac_encoder(c);

        assert(c->low <= 0x1FF);
        put_cabac_bit(c, c->low>>9);
        put_bits(&c->pb, 2, ((c->low>>7)&3)|1);

        flush_put_bits(&c->pb); //FIXME FIXME FIXME XXX wrong
    }

#ifdef STRICT_LIMITS
    c->symCount++;
#endif

    return (put_bits_count(&c->pb)+7)>>3;
}

/**
 * put (truncated) unary binarization.
 */
static void put_cabac_u(CABACContext *c, uint8_t * state, int v, int max, int max_index, int truncated){
    int i;

    assert(v <= max);

#if 1
    for(i=0; i<v; i++){
        put_cabac(c, state, 1);
        if(i < max_index) state++;
    }
    if(truncated==0 || v<max)
        put_cabac(c, state, 0);
#else
    if(v <= max_index){
        for(i=0; i<v; i++){
            put_cabac(c, state+i, 1);
        }
        if(truncated==0 || v<max)
            put_cabac(c, state+i, 0);
    }else{
        for(i=0; i<=max_index; i++){
            put_cabac(c, state+i, 1);
        }
        for(; i<v; i++){
            put_cabac(c, state+max_index, 1);
        }
        if(truncated==0 || v<max)
            put_cabac(c, state+max_index, 0);
    }
#endif
}

/**
 * put unary exp golomb k-th order binarization.
 */
static void put_cabac_ueg(CABACContext *c, uint8_t * state, int v, int max, int is_signed, int k, int max_index){
    int i;

    if(v==0)
        put_cabac(c, state, 0);
    else{
        const int sign= v < 0;

        if(is_signed) v= FFABS(v);

        if(v<max){
            for(i=0; i<v; i++){
                put_cabac(c, state, 1);
                if(i < max_index) state++;
            }

            put_cabac(c, state, 0);
        }else{
            int m= 1<<k;

            for(i=0; i<max; i++){
                put_cabac(c, state, 1);
                if(i < max_index) state++;
            }

            v -= max;
            while(v >= m){ //FIXME optimize
                put_cabac_bypass(c, 1);
                v-= m;
                m+= m;
            }
            put_cabac_bypass(c, 0);
            while(m>>=1){
                put_cabac_bypass(c, v&m);
            }
        }

        if(is_signed)
            put_cabac_bypass(c, sign);
    }
}
#endif /* TEST */

static void refill(CABACContext *c){
#if CABAC_BITS == 16
        c->low+= (c->bytestream[0]<<9) + (c->bytestream[1]<<1);
#else
        c->low+= c->bytestream[0]<<1;
#endif
    c->low -= CABAC_MASK;
    c->bytestream+= CABAC_BITS/8;
}

#if ! ( ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE && !defined(BROKEN_RELOCATIONS) )
static void refill2(CABACContext *c){
    int i, x;

    x= c->low ^ (c->low-1);
    i= 7 - ff_h264_norm_shift[x>>(CABAC_BITS-1)];

    x= -CABAC_MASK;

#if CABAC_BITS == 16
        x+= (c->bytestream[0]<<9) + (c->bytestream[1]<<1);
#else
        x+= c->bytestream[0]<<1;
#endif

    c->low += x<<i;
    c->bytestream+= CABAC_BITS/8;
}
#endif

static inline void renorm_cabac_decoder(CABACContext *c){
    while(c->range < 0x100){
        c->range+= c->range;
        c->low+= c->low;
        if(!(c->low & CABAC_MASK))
            refill(c);
    }
}

static inline void renorm_cabac_decoder_once(CABACContext *c){
#ifdef ARCH_X86_DISABLED
    int temp;
#if 0
    //P3:683    athlon:475
    __asm__(
        "lea -0x100(%0), %2         \n\t"
        "shr $31, %2                \n\t"  //FIXME 31->63 for x86-64
        "shl %%cl, %0               \n\t"
        "shl %%cl, %1               \n\t"
        : "+r"(c->range), "+r"(c->low), "+c"(temp)
    );
#elif 0
    //P3:680    athlon:474
    __asm__(
        "cmp $0x100, %0             \n\t"
        "setb %%cl                  \n\t"  //FIXME 31->63 for x86-64
        "shl %%cl, %0               \n\t"
        "shl %%cl, %1               \n\t"
        : "+r"(c->range), "+r"(c->low), "+c"(temp)
    );
#elif 1
    int temp2;
    //P3:665    athlon:517
    __asm__(
        "lea -0x100(%0), %%eax      \n\t"
        "cltd                       \n\t"
        "mov %0, %%eax              \n\t"
        "and %%edx, %0              \n\t"
        "and %1, %%edx              \n\t"
        "add %%eax, %0              \n\t"
        "add %%edx, %1              \n\t"
        : "+r"(c->range), "+r"(c->low), "+a"(temp), "+d"(temp2)
    );
#elif 0
    int temp2;
    //P3:673    athlon:509
    __asm__(
        "cmp $0x100, %0             \n\t"
        "sbb %%edx, %%edx           \n\t"
        "mov %0, %%eax              \n\t"
        "and %%edx, %0              \n\t"
        "and %1, %%edx              \n\t"
        "add %%eax, %0              \n\t"
        "add %%edx, %1              \n\t"
        : "+r"(c->range), "+r"(c->low), "+a"(temp), "+d"(temp2)
    );
#else
    int temp2;
    //P3:677    athlon:511
    __asm__(
        "cmp $0x100, %0             \n\t"
        "lea (%0, %0), %%eax        \n\t"
        "lea (%1, %1), %%edx        \n\t"
        "cmovb %%eax, %0            \n\t"
        "cmovb %%edx, %1            \n\t"
        : "+r"(c->range), "+r"(c->low), "+a"(temp), "+d"(temp2)
    );
#endif
#else
    //P3:675    athlon:476
    int shift= (uint32_t)(c->range - 0x100)>>31;
    c->range<<= shift;
    c->low  <<= shift;
#endif
    if(!(c->low & CABAC_MASK))
        refill(c);
}

static av_always_inline int get_cabac_inline(CABACContext *c, uint8_t * const state){
    //FIXME gcc generates duplicate load/stores for c->low and c->range
#define LOW          "0"
#define RANGE        "4"
#if ARCH_X86_64
#define BYTESTART   "16"
#define BYTE        "24"
#define BYTEEND     "32"
#else
#define BYTESTART   "12"
#define BYTE        "16"
#define BYTEEND     "20"
#endif
#if ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE && !defined(BROKEN_RELOCATIONS)
    int bit;

#ifndef BRANCHLESS_CABAC_DECODER
    __asm__ volatile(
        "movzbl (%1), %0                        \n\t"
        "movl "RANGE    "(%2), %%ebx            \n\t"
        "movl "RANGE    "(%2), %%edx            \n\t"
        "andl $0xC0, %%ebx                      \n\t"
        "movzbl "MANGLE(ff_h264_lps_range)"(%0, %%ebx, 2), %%esi\n\t"
        "movl "LOW      "(%2), %%ebx            \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "subl %%esi, %%edx                      \n\t"
        "movl %%edx, %%ecx                      \n\t"
        "shll $17, %%ecx                        \n\t"
        "cmpl %%ecx, %%ebx                      \n\t"
        " ja 1f                                 \n\t"

#if 1
        //athlon:4067 P3:4110
        "lea -0x100(%%edx), %%ecx               \n\t"
        "shr $31, %%ecx                         \n\t"
        "shl %%cl, %%edx                        \n\t"
        "shl %%cl, %%ebx                        \n\t"
#else
        //athlon:4057 P3:4130
        "cmp $0x100, %%edx                      \n\t" //FIXME avoidable
        "setb %%cl                              \n\t"
        "shl %%cl, %%edx                        \n\t"
        "shl %%cl, %%ebx                        \n\t"
#endif
        "movzbl "MANGLE(ff_h264_mps_state)"(%0), %%ecx   \n\t"
        "movb %%cl, (%1)                        \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "test %%bx, %%bx                        \n\t"
        " jnz 2f                                \n\t"
        "mov  "BYTE     "(%2), %%"REG_S"        \n\t"
        "subl $0xFFFF, %%ebx                    \n\t"
        "movzwl (%%"REG_S"), %%ecx              \n\t"
        "bswap %%ecx                            \n\t"
        "shrl $15, %%ecx                        \n\t"
        "add  $2, %%"REG_S"                     \n\t"
        "addl %%ecx, %%ebx                      \n\t"
        "mov  %%"REG_S", "BYTE    "(%2)         \n\t"
        "jmp 2f                                 \n\t"
        "1:                                     \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "subl %%ecx, %%ebx                      \n\t"
        "movl %%esi, %%edx                      \n\t"
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%esi), %%ecx   \n\t"
        "shll %%cl, %%ebx                       \n\t"
        "shll %%cl, %%edx                       \n\t"
        "movzbl "MANGLE(ff_h264_lps_state)"(%0), %%ecx   \n\t"
        "movb %%cl, (%1)                        \n\t"
        "add  $1, %0                            \n\t"
        "test %%bx, %%bx                        \n\t"
        " jnz 2f                                \n\t"

        "mov  "BYTE     "(%2), %%"REG_c"        \n\t"
        "movzwl (%%"REG_c"), %%esi              \n\t"
        "bswap %%esi                            \n\t"
        "shrl $15, %%esi                        \n\t"
        "subl $0xFFFF, %%esi                    \n\t"
        "add  $2, %%"REG_c"                     \n\t"
        "mov  %%"REG_c", "BYTE    "(%2)         \n\t"

        "leal -1(%%ebx), %%ecx                  \n\t"
        "xorl %%ebx, %%ecx                      \n\t"
        "shrl $15, %%ecx                        \n\t"
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%ecx), %%ecx   \n\t"
        "neg %%ecx                              \n\t"
        "add $7, %%ecx                          \n\t"

        "shll %%cl , %%esi                      \n\t"
        "addl %%esi, %%ebx                      \n\t"
        "2:                                     \n\t"
        "movl %%edx, "RANGE    "(%2)            \n\t"
        "movl %%ebx, "LOW      "(%2)            \n\t"
        :"=&a"(bit) //FIXME this is fragile gcc either runs out of registers or miscompiles it (for example if "+a"(bit) or "+m"(*state) is used
        :"r"(state), "r"(c)
        : "%"REG_c, "%ebx", "%edx", "%"REG_S, "memory"
    );
    bit&=1;
#else /* BRANCHLESS_CABAC_DECODER */


#if HAVE_FAST_CMOV
#define BRANCHLESS_GET_CABAC_UPDATE(ret, cabac, statep, low, lowword, range, tmp, tmpbyte)\
        "mov    "tmp"       , %%ecx                                     \n\t"\
        "shl    $17         , "tmp"                                     \n\t"\
        "cmp    "low"       , "tmp"                                     \n\t"\
        "cmova  %%ecx       , "range"                                   \n\t"\
        "sbb    %%ecx       , %%ecx                                     \n\t"\
        "and    %%ecx       , "tmp"                                     \n\t"\
        "sub    "tmp"       , "low"                                     \n\t"\
        "xor    %%ecx       , "ret"                                     \n\t"
#else /* HAVE_FAST_CMOV */
#define BRANCHLESS_GET_CABAC_UPDATE(ret, cabac, statep, low, lowword, range, tmp, tmpbyte)\
        "mov    "tmp"       , %%ecx                                     \n\t"\
        "shl    $17         , "tmp"                                     \n\t"\
        "sub    "low"       , "tmp"                                     \n\t"\
        "sar    $31         , "tmp"                                     \n\t" /*lps_mask*/\
        "sub    %%ecx       , "range"                                   \n\t" /*RangeLPS - range*/\
        "and    "tmp"       , "range"                                   \n\t" /*(RangeLPS - range)&lps_mask*/\
        "add    %%ecx       , "range"                                   \n\t" /*new range*/\
        "shl    $17         , %%ecx                                     \n\t"\
        "and    "tmp"       , %%ecx                                     \n\t"\
        "sub    %%ecx       , "low"                                     \n\t"\
        "xor    "tmp"       , "ret"                                     \n\t"
#endif /* HAVE_FAST_CMOV */


#define BRANCHLESS_GET_CABAC(ret, cabac, statep, low, lowword, range, tmp, tmpbyte)\
        "movzbl "statep"    , "ret"                                     \n\t"\
        "mov    "range"     , "tmp"                                     \n\t"\
        "and    $0xC0       , "range"                                   \n\t"\
        "movzbl "MANGLE(ff_h264_lps_range)"("ret", "range", 2), "range" \n\t"\
        "sub    "range"     , "tmp"                                     \n\t"\
        BRANCHLESS_GET_CABAC_UPDATE(ret, cabac, statep, low, lowword, range, tmp, tmpbyte)\
        "movzbl " MANGLE(ff_h264_norm_shift) "("range"), %%ecx          \n\t"\
        "shl    %%cl        , "range"                                   \n\t"\
        "movzbl "MANGLE(ff_h264_mlps_state)"+128("ret"), "tmp"          \n\t"\
        "mov    "tmpbyte"   , "statep"                                  \n\t"\
        "shl    %%cl        , "low"                                     \n\t"\
        "test   "lowword"   , "lowword"                                 \n\t"\
        " jnz   1f                                                      \n\t"\
        "mov "BYTE"("cabac"), %%"REG_c"                                 \n\t"\
        "movzwl (%%"REG_c")     , "tmp"                                 \n\t"\
        "bswap  "tmp"                                                   \n\t"\
        "shr    $15         , "tmp"                                     \n\t"\
        "sub    $0xFFFF     , "tmp"                                     \n\t"\
        "add    $2          , %%"REG_c"                                 \n\t"\
        "mov    %%"REG_c"   , "BYTE    "("cabac")                       \n\t"\
        "lea    -1("low")   , %%ecx                                     \n\t"\
        "xor    "low"       , %%ecx                                     \n\t"\
        "shr    $15         , %%ecx                                     \n\t"\
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%ecx), %%ecx            \n\t"\
        "neg    %%ecx                                                   \n\t"\
        "add    $7          , %%ecx                                     \n\t"\
        "shl    %%cl        , "tmp"                                     \n\t"\
        "add    "tmp"       , "low"                                     \n\t"\
        "1:                                                             \n\t"

    __asm__ volatile(
        "movl "RANGE    "(%2), %%esi            \n\t"
        "movl "LOW      "(%2), %%ebx            \n\t"
        BRANCHLESS_GET_CABAC("%0", "%2", "(%1)", "%%ebx", "%%bx", "%%esi", "%%edx", "%%dl")
        "movl %%esi, "RANGE    "(%2)            \n\t"
        "movl %%ebx, "LOW      "(%2)            \n\t"

        :"=&a"(bit)
        :"r"(state), "r"(c)
        : "%"REG_c, "%ebx", "%edx", "%esi", "memory"
    );
    bit&=1;
#endif /* BRANCHLESS_CABAC_DECODER */
#else /* ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE && !defined(BROKEN_RELOCATIONS) */
    int s = *state;
    int RangeLPS= ff_h264_lps_range[2*(c->range&0xC0) + s];
    int bit, lps_mask av_unused;

    c->range -= RangeLPS;
#ifndef BRANCHLESS_CABAC_DECODER
    if(c->low < (c->range<<(CABAC_BITS+1))){
        bit= s&1;
        *state= ff_h264_mps_state[s];
        renorm_cabac_decoder_once(c);
    }else{
        bit= ff_h264_norm_shift[RangeLPS];
        c->low -= (c->range<<(CABAC_BITS+1));
        *state= ff_h264_lps_state[s];
        c->range = RangeLPS<<bit;
        c->low <<= bit;
        bit= (s&1)^1;

        if(!(c->low & CABAC_MASK)){
            refill2(c);
        }
    }
#else /* BRANCHLESS_CABAC_DECODER */
    lps_mask= ((c->range<<(CABAC_BITS+1)) - c->low)>>31;

    c->low -= (c->range<<(CABAC_BITS+1)) & lps_mask;
    c->range += (RangeLPS - c->range) & lps_mask;

    s^=lps_mask;
    *state= (ff_h264_mlps_state+128)[s];
    bit= s&1;

    lps_mask= ff_h264_norm_shift[c->range];
    c->range<<= lps_mask;
    c->low  <<= lps_mask;
    if(!(c->low & CABAC_MASK))
        refill2(c);
#endif /* BRANCHLESS_CABAC_DECODER */
#endif /* ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE && !defined(BROKEN_RELOCATIONS) */
    return bit;
}

static int av_noinline av_unused get_cabac_noinline(CABACContext *c, uint8_t * const state){
    return get_cabac_inline(c,state);
}

static int av_unused get_cabac(CABACContext *c, uint8_t * const state){
    return get_cabac_inline(c,state);
}

static int av_unused get_cabac_bypass(CABACContext *c){
#if 0 //not faster
    int bit;
    __asm__ volatile(
        "movl "RANGE    "(%1), %%ebx            \n\t"
        "movl "LOW      "(%1), %%eax            \n\t"
        "shl $17, %%ebx                         \n\t"
        "add %%eax, %%eax                       \n\t"
        "sub %%ebx, %%eax                       \n\t"
        "cltd                                   \n\t"
        "and %%edx, %%ebx                       \n\t"
        "add %%ebx, %%eax                       \n\t"
        "test %%ax, %%ax                        \n\t"
        " jnz 1f                                \n\t"
        "movl "BYTE     "(%1), %%"REG_b"        \n\t"
        "subl $0xFFFF, %%eax                    \n\t"
        "movzwl (%%"REG_b"), %%ecx              \n\t"
        "bswap %%ecx                            \n\t"
        "shrl $15, %%ecx                        \n\t"
        "addl $2, %%"REG_b"                     \n\t"
        "addl %%ecx, %%eax                      \n\t"
        "movl %%"REG_b", "BYTE     "(%1)        \n\t"
        "1:                                     \n\t"
        "movl %%eax, "LOW      "(%1)            \n\t"

        :"=&d"(bit)
        :"r"(c)
        : "%eax", "%"REG_b, "%ecx", "memory"
    );
    return bit+1;
#else
    int range;
    c->low += c->low;

    if(!(c->low & CABAC_MASK))
        refill(c);

    range= c->range<<(CABAC_BITS+1);
    if(c->low < range){
        return 0;
    }else{
        c->low -= range;
        return 1;
    }
#endif
}


static av_always_inline int get_cabac_bypass_sign(CABACContext *c, int val){
#if ARCH_X86 && HAVE_EBX_AVAILABLE
    __asm__ volatile(
        "movl "RANGE    "(%1), %%ebx            \n\t"
        "movl "LOW      "(%1), %%eax            \n\t"
        "shl $17, %%ebx                         \n\t"
        "add %%eax, %%eax                       \n\t"
        "sub %%ebx, %%eax                       \n\t"
        "cltd                                   \n\t"
        "and %%edx, %%ebx                       \n\t"
        "add %%ebx, %%eax                       \n\t"
        "xor %%edx, %%ecx                       \n\t"
        "sub %%edx, %%ecx                       \n\t"
        "test %%ax, %%ax                        \n\t"
        " jnz 1f                                \n\t"
        "mov  "BYTE     "(%1), %%"REG_b"        \n\t"
        "subl $0xFFFF, %%eax                    \n\t"
        "movzwl (%%"REG_b"), %%edx              \n\t"
        "bswap %%edx                            \n\t"
        "shrl $15, %%edx                        \n\t"
        "add  $2, %%"REG_b"                     \n\t"
        "addl %%edx, %%eax                      \n\t"
        "mov  %%"REG_b", "BYTE     "(%1)        \n\t"
        "1:                                     \n\t"
        "movl %%eax, "LOW      "(%1)            \n\t"

        :"+c"(val)
        :"r"(c)
        : "%eax", "%"REG_b, "%edx", "memory"
    );
    return val;
#else
    int range, mask;
    c->low += c->low;

    if(!(c->low & CABAC_MASK))
        refill(c);

    range= c->range<<(CABAC_BITS+1);
    c->low -= range;
    mask= c->low >> 31;
    range &= mask;
    c->low += range;
    return (val^mask)-mask;
#endif
}

/**
 *
 * @return the number of bytes read or 0 if no end
 */
static int av_unused get_cabac_terminate(CABACContext *c){
    c->range -= 2;
    if(c->low < c->range<<(CABAC_BITS+1)){
        renorm_cabac_decoder_once(c);
        return 0;
    }else{
        return c->bytestream - c->bytestream_start;
    }
}

#if 0
/**
 * Get (truncated) unary binarization.
 */
static int get_cabac_u(CABACContext *c, uint8_t * state, int max, int max_index, int truncated){
    int i;

    for(i=0; i<max; i++){
        if(get_cabac(c, state)==0)
            return i;

        if(i< max_index) state++;
    }

    return truncated ? max : -1;
}

/**
 * get unary exp golomb k-th order binarization.
 */
static int get_cabac_ueg(CABACContext *c, uint8_t * state, int max, int is_signed, int k, int max_index){
    int i, v;
    int m= 1<<k;

    if(get_cabac(c, state)==0)
        return 0;

    if(0 < max_index) state++;

    for(i=1; i<max; i++){
        if(get_cabac(c, state)==0){
            if(is_signed && get_cabac_bypass(c)){
                return -i;
            }else
                return i;
        }

        if(i < max_index) state++;
    }

    while(get_cabac_bypass(c)){
        i+= m;
        m+= m;
    }

    v=0;
    while(m>>=1){
        v+= v + get_cabac_bypass(c);
    }
    i += v;

    if(is_signed && get_cabac_bypass(c)){
        return -i;
    }else
        return i;
}
#endif /* 0 */

#endif /* AVCODEC_CABAC_H */
