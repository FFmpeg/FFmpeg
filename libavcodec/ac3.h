/*
 * Common code between AC3 encoder and decoder
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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
 * @file ac3.h
 * Common code between AC3 encoder and decoder.
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

/**
 * @struct AC3HeaderInfo
 * Coded AC-3 header values up to the lfeon element, plus derived values.
 */
typedef struct {
    /** @defgroup coded Coded elements
     * @{
     */
    uint16_t sync_word;
    uint16_t crc1;
    uint8_t fscod;
    uint8_t frmsizecod;
    uint8_t bsid;
    uint8_t bsmod;
    uint8_t acmod;
    uint8_t cmixlev;
    uint8_t surmixlev;
    uint8_t dsurmod;
    uint8_t lfeon;
    /** @} */

    /** @defgroup derived Derived values
     * @{
     */
    uint8_t halfratecod;
    uint16_t sample_rate;
    uint32_t bit_rate;
    uint8_t channels;
    uint16_t frame_size;
    /** @} */
} AC3HeaderInfo;

/**
 * Parses AC-3 frame header.
 * Parses the header up to the lfeon element, which is the first 52 or 54 bits
 * depending on the audio coding mode.
 * @param buf[in] Array containing the first 7 bytes of the frame.
 * @param hdr[out] Pointer to struct where header info is written.
 * @return Returns 0 on success, -1 if there is a sync word mismatch,
 * -2 if the bsid (version) element is invalid, -3 if the fscod (sample rate)
 * element is invalid, or -4 if the frmsizecod (bit rate) element is invalid.
 */
int ff_ac3_parse_header(const uint8_t buf[7], AC3HeaderInfo *hdr);

extern const uint16_t ff_ac3_frame_sizes[38][3];
extern const uint8_t ff_ac3_channels[8];
extern const uint16_t ff_ac3_freqs[3];
extern const uint16_t ff_ac3_bitratetab[19];
extern const int16_t ff_ac3_window[256];
extern const uint8_t ff_sdecaytab[4];
extern const uint8_t ff_fdecaytab[4];
extern const uint16_t ff_sgaintab[4];
extern const uint16_t ff_dbkneetab[4];
extern const int16_t ff_floortab[8];
extern const uint16_t ff_fgaintab[8];

void ac3_common_init(void);

/**
 * Calculates the log power-spectral density of the input signal.
 * This gives a rough estimate of signal power in the frequency domain by using
 * the spectral envelope (exponents).  The psd is also separately grouped
 * into critical bands for use in the calculating the masking curve.
 * 128 units in psd = -6 dB.  The dbknee parameter in AC3BitAllocParameters
 * determines the reference level.
 *
 * @param[in]  exp        frequency coefficient exponents
 * @param[in]  start      starting bin location
 * @param[in]  end        ending bin location
 * @param[out] psd        signal power for each frequency bin
 * @param[out] bndpsd     signal power for each critical band
 */
void ff_ac3_bit_alloc_calc_psd(int8_t *exp, int start, int end, int16_t *psd,
                               int16_t *bndpsd);

/**
 * Calculates the masking curve.
 * First, the excitation is calculated using parameters in \p s and the signal
 * power in each critical band.  The excitation is compared with a predefined
 * hearing threshold table to produce the masking curve.  If delta bit
 * allocation information is provided, it is used for adjusting the masking
 * curve, usually to give a closer match to a better psychoacoustic model.
 *
 * @param[in]  s          adjustable bit allocation parameters
 * @param[in]  bndpsd     signal power for each critical band
 * @param[in]  start      starting bin location
 * @param[in]  end        ending bin location
 * @param[in]  fgain      fast gain (estimated signal-to-mask ratio)
 * @param[in]  is_lfe     whether or not the channel being processed is the LFE
 * @param[in]  deltbae    delta bit allocation exists (none, reuse, or new)
 * @param[in]  deltnseg   number of delta segments
 * @param[in]  deltoffst  location offsets for each segment
 * @param[in]  deltlen    length of each segment
 * @param[in]  deltba     delta bit allocation for each segment
 * @param[out] mask       calculated masking curve
 */
void ff_ac3_bit_alloc_calc_mask(AC3BitAllocParameters *s, int16_t *bndpsd,
                                int start, int end, int fgain, int is_lfe,
                                int deltbae, int deltnseg, uint8_t *deltoffst,
                                uint8_t *deltlen, uint8_t *deltba,
                                int16_t *mask);

/**
 * Calculates bit allocation pointers.
 * The SNR is the difference between the masking curve and the signal.  AC-3
 * uses this value for each frequency bin to allocate bits.  The \p snroffset
 * parameter is a global adjustment to the SNR for all bins.
 *
 * @param[in]  mask       masking curve
 * @param[in]  psd        signal power for each frequency bin
 * @param[in]  start      starting bin location
 * @param[in]  end        ending bin location
 * @param[in]  snroffset  SNR adjustment
 * @param[in]  floor      noise floor
 * @param[out] bap        bit allocation pointers
 */
void ff_ac3_bit_alloc_calc_bap(int16_t *mask, int16_t *psd, int start, int end,
                               int snroffset, int floor, uint8_t *bap);

void ac3_parametric_bit_allocation(AC3BitAllocParameters *s, uint8_t *bap,
                                   int8_t *exp, int start, int end,
                                   int snroffset, int fgain, int is_lfe,
                                   int deltbae,int deltnseg,
                                   uint8_t *deltoffst, uint8_t *deltlen, uint8_t *deltba);
