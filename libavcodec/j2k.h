/*
 * JPEG2000 tables
 * Copyright (c) 2007 Kamil Nowosad
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

#ifndef AVCODEC_J2K_H
#define AVCODEC_J2K_H

/**
 * JPEG2000 tables
 * @file
 * @author Kamil Nowosad
 */

#include "mqc.h"
#include "j2k_dwt.h"

enum J2kMarkers{
    J2K_SOC = 0xff4f, ///< start of codestream
    J2K_SIZ = 0xff51, ///< image and tile size
    J2K_COD,          ///< coding style default
    J2K_COC,          ///< coding style component
    J2K_TLM = 0xff55, ///< packed packet headers, tile-part header
    J2K_PLM = 0xff57, ///< tile-part lengths
    J2K_PLT,          ///< packet length, main header
    J2K_QCD = 0xff5c, ///< quantization default
    J2K_QCC,          ///< quantization component
    J2K_RGN,          ///< region of interest
    J2K_POC,          ///< progression order change
    J2K_PPM,          ///< packet length, tile-part header
    J2K_PPT,          ///< packed packet headers, main header
    J2K_CRG = 0xff63, ///< component registration
    J2K_COM,          ///< comment
    J2K_SOT = 0xff90, ///< start of tile-part
    J2K_SOP,          ///< start of packet
    J2K_EPH,          ///< end of packet header
    J2K_SOD,          ///< start of data
    J2K_EOC = 0xffd9, ///< end of codestream
};

enum J2kQuantsty{ ///< quantization style
    J2K_QSTY_NONE, ///< no quantization
    J2K_QSTY_SI,   ///< scalar derived
    J2K_QSTY_SE    ///< scalar expoounded
};

#define J2K_MAX_CBLKW 64
#define J2K_MAX_CBLKH 64

// T1 flags
// flags determining significance of neighbour coefficients
#define J2K_T1_SIG_N  0x0001
#define J2K_T1_SIG_E  0x0002
#define J2K_T1_SIG_W  0x0004
#define J2K_T1_SIG_S  0x0008
#define J2K_T1_SIG_NE 0x0010
#define J2K_T1_SIG_NW 0x0020
#define J2K_T1_SIG_SE 0x0040
#define J2K_T1_SIG_SW 0x0080
#define J2K_T1_SIG_NB (J2K_T1_SIG_N | J2K_T1_SIG_E | J2K_T1_SIG_S | J2K_T1_SIG_W \
                      |J2K_T1_SIG_NE | J2K_T1_SIG_NW | J2K_T1_SIG_SE | J2K_T1_SIG_SW)
// flags determining sign bit of neighbour coefficients
#define J2K_T1_SGN_N  0x0100
#define J2K_T1_SGN_S  0x0200
#define J2K_T1_SGN_W  0x0400
#define J2K_T1_SGN_E  0x0800

#define J2K_T1_VIS    0x1000
#define J2K_T1_SIG    0x2000
#define J2K_T1_REF    0x4000

#define J2K_T1_SGN    0x8000

// Codeblock coding styles
#define J2K_CBLK_BYPASS    0x01 // Selective arithmetic coding bypass
#define J2K_CBLK_RESET     0x02 // Reset context probabilities
#define J2K_CBLK_TERMALL   0x04 // Terminate after each coding pass
#define J2K_CBLK_VSC       0x08 // Vertical stripe causal context formation
#define J2K_CBLK_PREDTERM  0x10 // Predictable termination
#define J2K_CBLK_SEGSYM    0x20 // Segmentation symbols present

// Coding styles
#define J2K_CSTY_PREC      0x01 // Precincts defined in coding style
#define J2K_CSTY_SOP       0x02 // SOP marker present
#define J2K_CSTY_EPH       0x04 // EPH marker present

typedef struct {
    int data[J2K_MAX_CBLKW][J2K_MAX_CBLKH];
    int flags[J2K_MAX_CBLKW+2][J2K_MAX_CBLKH+2];
    MqcState mqc;
} J2kT1Context;

typedef struct J2kTgtNode {
    uint8_t val;
    uint8_t vis;
    struct J2kTgtNode *parent;
} J2kTgtNode;

typedef struct {
    uint8_t nreslevels;       ///< number of resolution levels
    uint8_t log2_cblk_width,
            log2_cblk_height; ///< exponent of codeblock size
    uint8_t transform;        ///< DWT type
    uint8_t csty;             ///< coding style
    uint8_t log2_prec_width,
            log2_prec_height; ///< precinct size
    uint8_t nlayers;          ///< number of layers
    uint8_t mct;              ///< multiple component transformation
    uint8_t cblk_style;       ///< codeblock coding style
} J2kCodingStyle;

typedef struct {
    uint8_t  expn[32 * 3]; ///< quantization exponent
    uint16_t mant[32 * 3]; ///< quantization mantissa
    uint8_t  quantsty;     ///< quantization style
    uint8_t  nguardbits;   ///< number of guard bits
} J2kQuantStyle;

typedef struct {
    uint16_t rate;
    int64_t disto;
} J2kPass;

typedef struct {
    uint8_t npasses;
    uint8_t ninclpasses; ///< number coding of passes included in codestream
    uint8_t nonzerobits;
    uint16_t length;
    uint16_t lengthinc;
    uint8_t lblock;
    uint8_t zero;
    uint8_t data[8192];
    J2kPass passes[100];
} J2kCblk; ///< code block

typedef struct {
    uint16_t xi0, xi1, yi0, yi1; ///< codeblock indexes ([xi0, xi1))
    J2kTgtNode *zerobits;
    J2kTgtNode *cblkincl;
} J2kPrec; ///< precinct

typedef struct {
    uint16_t coord[2][2]; ///< border coordinates {{x0, x1}, {y0, y1}}
    uint16_t codeblock_width, codeblock_height;
    uint16_t cblknx, cblkny;
    uint32_t stepsize; ///< quantization stepsize (* 2^13)
    J2kPrec *prec;
    J2kCblk *cblk;
} J2kBand; ///< subband

typedef struct {
    uint8_t nbands;
    uint16_t coord[2][2]; ///< border coordinates {{x0, x1}, {y0, y1}}
    uint16_t num_precincts_x, num_precincts_y; ///< number of precincts in x/y direction
    uint8_t log2_prec_width, log2_prec_height; ///< exponent of precinct size
    J2kBand *band;
} J2kResLevel; ///< resolution level

typedef struct {
   J2kResLevel *reslevel;
   DWTContext dwt;
   int *data;
   uint16_t coord[2][2]; ///< border coordinates {{x0, x1}, {y0, y1}}
} J2kComponent;

/* debug routines */
#if 0
#undef fprintf
#undef printf
void ff_j2k_printv(int *tab, int l);
void ff_j2k_printu(uint8_t *tab, int l);
#endif

/* misc tools */
static inline int ff_j2k_ceildivpow2(int a, int b)
{
    return (a + (1 << b) - 1)>> b;
}

static inline int ff_j2k_ceildiv(int a, int b)
{
    return (a + b - 1) / b;
}

/* tag tree routines */
J2kTgtNode *ff_j2k_tag_tree_init(int w, int h);

/* TIER-1 routines */
void ff_j2k_init_tier1_luts(void);

void ff_j2k_set_significant(J2kT1Context *t1, int x, int y, int negative);

extern uint8_t ff_j2k_nbctxno_lut[256][4];

static inline int ff_j2k_getnbctxno(int flag, int bandno, int vert_causal_ctx_csty_symbol)
{
    return ff_j2k_nbctxno_lut[flag&255][bandno];
}

static inline int ff_j2k_getrefctxno(int flag)
{
    static const uint8_t refctxno_lut[2][2] = {{14, 15}, {16, 16}};
    return refctxno_lut[(flag>>14)&1][(flag & 255) != 0];
}

extern uint8_t ff_j2k_sgnctxno_lut[16][16], ff_j2k_xorbit_lut[16][16];

static inline int ff_j2k_getsgnctxno(int flag, int *xorbit)
{
    *xorbit = ff_j2k_xorbit_lut[flag&15][(flag>>8)&15];
    return  ff_j2k_sgnctxno_lut[flag&15][(flag>>8)&15];
}

int ff_j2k_init_component(J2kComponent *comp, J2kCodingStyle *codsty, J2kQuantStyle *qntsty, int cbps, int dx, int dy);
void ff_j2k_reinit(J2kComponent *comp, J2kCodingStyle *codsty);
void ff_j2k_cleanup(J2kComponent *comp, J2kCodingStyle *codsty);

#endif /* AVCODEC_J2K_H */
