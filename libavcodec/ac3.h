/*
 * Common code between AC3 encoder and decoder
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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
 */

#define AC3_MAX_CODED_FRAME_SIZE 3840 /* in bytes */
#define AC3_MAX_CHANNELS 6 /* including LFE channel */

#define NB_BLOCKS 6 /* number of PCM blocks inside an AC3 frame */
#define AC3_FRAME_SIZE (NB_BLOCKS * 256)

/* exponent encoding strategy */
#define EXP_REUSE 0
#define EXP_NEW   1

#define EXP_D15   1
#define EXP_D25   2
#define EXP_D45   3

typedef struct AC3BitAllocParameters {
    int fscod; /* frequency */
    int halfratecod;
    int sgain, sdecay, fdecay, dbknee, floor;
    int cplfleak, cplsleak;
} AC3BitAllocParameters;

extern const UINT16 ac3_freqs[3];
extern const UINT16 ac3_bitratetab[19];
extern const INT16 ac3_window[256];
extern const UINT8 sdecaytab[4];
extern const UINT8 fdecaytab[4];
extern const UINT16 sgaintab[4];
extern const UINT16 dbkneetab[4];
extern const UINT16 floortab[8];
extern const UINT16 fgaintab[8];

void ac3_common_init(void);
void ac3_parametric_bit_allocation(AC3BitAllocParameters *s, UINT8 *bap,
                                   INT8 *exp, int start, int end,
                                   int snroffset, int fgain, int is_lfe,
                                   int deltbae,int deltnseg, 
                                   UINT8 *deltoffst, UINT8 *deltlen, UINT8 *deltba);
