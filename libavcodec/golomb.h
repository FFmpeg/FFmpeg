/*
 * exp golomb vlc stuff
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/**
 * @file golomb.h
 * @brief 
 *     exp golomb vlc stuff
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#define INVALID_VLC           0x80000000

extern const uint8_t ff_golomb_vlc_len[512];
extern const uint8_t ff_ue_golomb_vlc_code[512];
extern const  int8_t ff_se_golomb_vlc_code[512];
extern const uint8_t ff_ue_golomb_len[256];

 
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

static inline int svq3_get_ue_golomb(GetBitContext *gb){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb)|1;

    if((buf & 0xAAAAAAAA) == 0)
        return INVALID_VLC;

    for(log=31; (buf & 0x80000000) == 0; log--){
        buf = (buf << 2) - ((buf << log) >> (log - 1)) + (buf >> 30);
    }

    LAST_SKIP_BITS(re, gb, 63 - 2*log);
    CLOSE_READER(re, gb);

    return ((buf << log) >> log) - 1;
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
    buf=GET_CACHE(re, gb)|1;

    if((buf & 0xAAAAAAAA) == 0)
        return INVALID_VLC;

    for(log=31; (buf & 0x80000000) == 0; log--){
        buf = (buf << 2) - ((buf << log) >> (log - 1)) + (buf >> 30);
    }

    LAST_SKIP_BITS(re, gb, 63 - 2*log);
    CLOSE_READER(re, gb);

    return (signed) (((((buf << log) >> log) - 1) ^ -(buf & 0x1)) + 1) >> 1;
}

#ifdef TRACE

static inline int get_ue(GetBitContext *s, char *file, char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int i= get_ue_golomb(s);
    int len= get_bits_count(s) - pos;
    int bits= show>>(24-len);
    
    print_bin(bits, len);
    
    printf("%5d %2d %3d ue  @%5d in %s %s:%d\n", bits, len, i, pos, file, func, line);
    
    return i;
}

static inline int get_se(GetBitContext *s, char *file, char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int i= get_se_golomb(s);
    int len= get_bits_count(s) - pos;
    int bits= show>>(24-len);
    
    print_bin(bits, len);
    
    printf("%5d %2d %3d se  @%5d in %s %s:%d\n", bits, len, i, pos, file, func, line);
    
    return i;
}

static inline int get_te(GetBitContext *s, int r, char *file, char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int i= get_te0_golomb(s, r);
    int len= get_bits_count(s) - pos;
    int bits= show>>(24-len);
    
    print_bin(bits, len);
    
    printf("%5d %2d %3d te  @%5d in %s %s:%d\n", bits, len, i, pos, file, func, line);
    
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
 * write signed exp golomb code.
 */
static inline void set_se_golomb(PutBitContext *pb, int i){
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
