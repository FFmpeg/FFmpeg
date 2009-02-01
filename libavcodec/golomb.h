/*
 * exp golomb vlc stuff
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Alex Beregszaszi
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
 * @file libavcodec/golomb.h
 * @brief
 *     exp golomb vlc stuff
 * @author Michael Niedermayer <michaelni@gmx.at> and Alex Beregszaszi
 */

#ifndef AVCODEC_GOLOMB_H
#define AVCODEC_GOLOMB_H

#include <stdint.h>
#include "bitstream.h"

#define INVALID_VLC           0x80000000

extern const uint8_t ff_golomb_vlc_len[512];
extern const uint8_t ff_ue_golomb_vlc_code[512];
extern const  int8_t ff_se_golomb_vlc_code[512];
extern const uint8_t ff_ue_golomb_len[256];

extern const uint8_t ff_interleaved_golomb_vlc_len[256];
extern const uint8_t ff_interleaved_ue_golomb_vlc_code[256];
extern const  int8_t ff_interleaved_se_golomb_vlc_code[256];
extern const uint8_t ff_interleaved_dirac_golomb_vlc_code[256];


 /**
 * read unsigned exp golomb code.
 */
static inline int get_ue_golomb(GetBitContext *gb){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    if(buf >= (1<<27)){
        buf >>= 32 - 9;
        LAST_SKIP_BITS(re, gb, ff_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_ue_golomb_vlc_code[buf];
    }else{
        log= 2*av_log2(buf) - 31;
        buf>>= log;
        buf--;
        LAST_SKIP_BITS(re, gb, 32 - log);
        CLOSE_READER(re, gb);

        return buf;
    }
}

 /**
 * read unsigned exp golomb code, constraint to a max of 31
 */
static inline int get_ue_golomb_31(GetBitContext *gb){
    unsigned int buf;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    buf >>= 32 - 9;
    LAST_SKIP_BITS(re, gb, ff_golomb_vlc_len[buf]);
    CLOSE_READER(re, gb);

    return ff_ue_golomb_vlc_code[buf];
}

static inline int svq3_get_ue_golomb(GetBitContext *gb){
    uint32_t buf;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    if(buf&0xAA800000){
        buf >>= 32 - 8;
        LAST_SKIP_BITS(re, gb, ff_interleaved_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_interleaved_ue_golomb_vlc_code[buf];
    }else{
        int ret = 1;

        while (1) {
            buf >>= 32 - 8;
            LAST_SKIP_BITS(re, gb, FFMIN(ff_interleaved_golomb_vlc_len[buf], 8));

            if (ff_interleaved_golomb_vlc_len[buf] != 9){
                ret <<= (ff_interleaved_golomb_vlc_len[buf] - 1) >> 1;
                ret |= ff_interleaved_dirac_golomb_vlc_code[buf];
                break;
            }
            ret = (ret << 4) | ff_interleaved_dirac_golomb_vlc_code[buf];
            UPDATE_CACHE(re, gb);
            buf = GET_CACHE(re, gb);
        }

        CLOSE_READER(re, gb);
        return ret - 1;
    }
}

/**
 * read unsigned truncated exp golomb code.
 */
static inline int get_te0_golomb(GetBitContext *gb, int range){
    assert(range >= 1);

    if(range==1)      return 0;
    else if(range==2) return get_bits1(gb)^1;
    else              return get_ue_golomb(gb);
}

/**
 * read unsigned truncated exp golomb code.
 */
static inline int get_te_golomb(GetBitContext *gb, int range){
    assert(range >= 1);

    if(range==2) return get_bits1(gb)^1;
    else         return get_ue_golomb(gb);
}


/**
 * read signed exp golomb code.
 */
static inline int get_se_golomb(GetBitContext *gb){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    if(buf >= (1<<27)){
        buf >>= 32 - 9;
        LAST_SKIP_BITS(re, gb, ff_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_se_golomb_vlc_code[buf];
    }else{
        log= 2*av_log2(buf) - 31;
        buf>>= log;

        LAST_SKIP_BITS(re, gb, 32 - log);
        CLOSE_READER(re, gb);

        if(buf&1) buf= -(buf>>1);
        else      buf=  (buf>>1);

        return buf;
    }
}

static inline int svq3_get_se_golomb(GetBitContext *gb){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    if(buf&0xAA800000){
        buf >>= 32 - 8;
        LAST_SKIP_BITS(re, gb, ff_interleaved_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_interleaved_se_golomb_vlc_code[buf];
    }else{
        LAST_SKIP_BITS(re, gb, 8);
        UPDATE_CACHE(re, gb);
        buf |= 1 | (GET_CACHE(re, gb) >> 8);

        if((buf & 0xAAAAAAAA) == 0)
            return INVALID_VLC;

        for(log=31; (buf & 0x80000000) == 0; log--){
            buf = (buf << 2) - ((buf << log) >> (log - 1)) + (buf >> 30);
        }

        LAST_SKIP_BITS(re, gb, 63 - 2*log - 8);
        CLOSE_READER(re, gb);

        return (signed) (((((buf << log) >> log) - 1) ^ -(buf & 0x1)) + 1) >> 1;
    }
}

static inline int dirac_get_se_golomb(GetBitContext *gb){
    uint32_t buf;
    uint32_t ret;

    ret = svq3_get_ue_golomb(gb);

    if (ret) {
        OPEN_READER(re, gb);
        UPDATE_CACHE(re, gb);
        buf = SHOW_SBITS(re, gb, 1);
        LAST_SKIP_BITS(re, gb, 1);
        ret = (ret ^ buf) - buf;
        CLOSE_READER(re, gb);
    }

    return ret;
}

/**
 * read unsigned golomb rice code (ffv1).
 */
static inline int get_ur_golomb(GetBitContext *gb, int k, int limit, int esc_len){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    log= av_log2(buf);

    if(log > 31-limit){
        buf >>= log - k;
        buf += (30-log)<<k;
        LAST_SKIP_BITS(re, gb, 32 + k - log);
        CLOSE_READER(re, gb);

        return buf;
    }else{
        buf >>= 32 - limit - esc_len;
        LAST_SKIP_BITS(re, gb, esc_len + limit);
        CLOSE_READER(re, gb);

        return buf + limit - 1;
    }
}

/**
 * read unsigned golomb rice code (jpegls).
 */
static inline int get_ur_golomb_jpegls(GetBitContext *gb, int k, int limit, int esc_len){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    log= av_log2(buf);

    if(log - k >= 32-MIN_CACHE_BITS && 32-log < limit){
        buf >>= log - k;
        buf += (30-log)<<k;
        LAST_SKIP_BITS(re, gb, 32 + k - log);
        CLOSE_READER(re, gb);

        return buf;
    }else{
        int i;
        for(i=0; SHOW_UBITS(re, gb, 1) == 0; i++){
            LAST_SKIP_BITS(re, gb, 1);
            UPDATE_CACHE(re, gb);
        }
        SKIP_BITS(re, gb, 1);

        if(i < limit - 1){
            if(k){
                buf = SHOW_UBITS(re, gb, k);
                LAST_SKIP_BITS(re, gb, k);
            }else{
                buf=0;
            }

            CLOSE_READER(re, gb);
            return buf + (i<<k);
        }else if(i == limit - 1){
            buf = SHOW_UBITS(re, gb, esc_len);
            LAST_SKIP_BITS(re, gb, esc_len);
            CLOSE_READER(re, gb);

            return buf + 1;
        }else
            return -1;
    }
}

/**
 * read signed golomb rice code (ffv1).
 */
static inline int get_sr_golomb(GetBitContext *gb, int k, int limit, int esc_len){
    int v= get_ur_golomb(gb, k, limit, esc_len);

    v++;
    if (v&1) return v>>1;
    else return -(v>>1);

//    return (v>>1) ^ -(v&1);
}

/**
 * read signed golomb rice code (flac).
 */
static inline int get_sr_golomb_flac(GetBitContext *gb, int k, int limit, int esc_len){
    int v= get_ur_golomb_jpegls(gb, k, limit, esc_len);
    return (v>>1) ^ -(v&1);
}

/**
 * read unsigned golomb rice code (shorten).
 */
static inline unsigned int get_ur_golomb_shorten(GetBitContext *gb, int k){
        return get_ur_golomb_jpegls(gb, k, INT_MAX, 0);
}

/**
 * read signed golomb rice code (shorten).
 */
static inline int get_sr_golomb_shorten(GetBitContext* gb, int k)
{
    int uvar = get_ur_golomb_jpegls(gb, k + 1, INT_MAX, 0);
    if (uvar & 1)
        return ~(uvar >> 1);
    else
        return uvar >> 1;
}



#ifdef TRACE

static inline int get_ue(GetBitContext *s, char *file, const char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int i= get_ue_golomb(s);
    int len= get_bits_count(s) - pos;
    int bits= show>>(24-len);

    print_bin(bits, len);

    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d ue  @%5d in %s %s:%d\n", bits, len, i, pos, file, func, line);

    return i;
}

static inline int get_se(GetBitContext *s, char *file, const char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int i= get_se_golomb(s);
    int len= get_bits_count(s) - pos;
    int bits= show>>(24-len);

    print_bin(bits, len);

    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d se  @%5d in %s %s:%d\n", bits, len, i, pos, file, func, line);

    return i;
}

static inline int get_te(GetBitContext *s, int r, char *file, const char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int i= get_te0_golomb(s, r);
    int len= get_bits_count(s) - pos;
    int bits= show>>(24-len);

    print_bin(bits, len);

    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d te  @%5d in %s %s:%d\n", bits, len, i, pos, file, func, line);

    return i;
}

#define get_ue_golomb(a) get_ue(a, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_se_golomb(a) get_se(a, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_te_golomb(a, r) get_te(a, r, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_te0_golomb(a, r) get_te(a, r, __FILE__, __PRETTY_FUNCTION__, __LINE__)

#endif

/**
 * write unsigned exp golomb code.
 */
static inline void set_ue_golomb(PutBitContext *pb, int i){
    int e;

    assert(i>=0);

#if 0
    if(i=0){
        put_bits(pb, 1, 1);
        return;
    }
#endif
    if(i<256)
        put_bits(pb, ff_ue_golomb_len[i], i+1);
    else{
        e= av_log2(i+1);

        put_bits(pb, 2*e+1, i+1);
    }
}

/**
 * write truncated unsigned exp golomb code.
 */
static inline void set_te_golomb(PutBitContext *pb, int i, int range){
    assert(range >= 1);
    assert(i<=range);

    if(range==2) put_bits(pb, 1, i^1);
    else         set_ue_golomb(pb, i);
}

/**
 * write signed exp golomb code. 16 bits at most.
 */
static inline void set_se_golomb(PutBitContext *pb, int i){
//    if (i>32767 || i<-32767)
//        av_log(NULL,AV_LOG_ERROR,"value out of range %d\n", i);
#if 0
    if(i<=0) i= -2*i;
    else     i=  2*i-1;
#elif 1
    i= 2*i-1;
    if(i<0) i^= -1; //FIXME check if gcc does the right thing
#else
    i= 2*i-1;
    i^= (i>>31);
#endif
    set_ue_golomb(pb, i);
}

/**
 * write unsigned golomb rice code (ffv1).
 */
static inline void set_ur_golomb(PutBitContext *pb, int i, int k, int limit, int esc_len){
    int e;

    assert(i>=0);

    e= i>>k;
    if(e<limit){
        put_bits(pb, e + k + 1, (1<<k) + (i&((1<<k)-1)));
    }else{
        put_bits(pb, limit + esc_len, i - limit + 1);
    }
}

/**
 * write unsigned golomb rice code (jpegls).
 */
static inline void set_ur_golomb_jpegls(PutBitContext *pb, int i, int k, int limit, int esc_len){
    int e;

    assert(i>=0);

    e= (i>>k) + 1;
    if(e<limit){
        while(e > 31) {
            put_bits(pb, 31, 0);
            e -= 31;
        }
        put_bits(pb, e, 1);
        if(k)
            put_sbits(pb, k, i);
    }else{
        while(limit > 31) {
            put_bits(pb, 31, 0);
            limit -= 31;
        }
        put_bits(pb, limit  , 1);
        put_bits(pb, esc_len, i - 1);
    }
}

/**
 * write signed golomb rice code (ffv1).
 */
static inline void set_sr_golomb(PutBitContext *pb, int i, int k, int limit, int esc_len){
    int v;

    v = -2*i-1;
    v ^= (v>>31);

    set_ur_golomb(pb, v, k, limit, esc_len);
}

/**
 * write signed golomb rice code (flac).
 */
static inline void set_sr_golomb_flac(PutBitContext *pb, int i, int k, int limit, int esc_len){
    int v;

    v = -2*i-1;
    v ^= (v>>31);

    set_ur_golomb_jpegls(pb, v, k, limit, esc_len);
}

#endif /* AVCODEC_GOLOMB_H */
