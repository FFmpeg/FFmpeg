/**
 * @file mpegaudio.h
 * mpeg audio declarations for both encoder and decoder.
 */

/* max frame size, in samples */
#define MPA_FRAME_SIZE 1152 

/* max compressed frame size */
#define MPA_MAX_CODED_FRAME_SIZE 1792

#define MPA_MAX_CHANNELS 2

#define SBLIMIT 32 /* number of subbands */

#define MPA_STEREO  0
#define MPA_JSTEREO 1
#define MPA_DUAL    2
#define MPA_MONO    3

/* header + layer + bitrate + freq + lsf/mpeg25 */
#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (0xf << 12) | (3 << 10) | (3 << 19))

int l2_select_table(int bitrate, int nb_channels, int freq, int lsf);
int mpa_decode_header(AVCodecContext *avctx, uint32_t head);

extern const uint16_t mpa_bitrate_tab[2][3][15];
extern const uint16_t mpa_freq_tab[3];
extern const unsigned char *alloc_tables[5];
extern const double enwindow[512];
extern const int sblimit_table[5];
extern const int quant_steps[17];
extern const int quant_bits[17];
extern const int32_t mpa_enwindow[257];

/* fast header check for resync */
static inline int ff_mpa_check_header(uint32_t header){
    /* header */
    if ((header & 0xffe00000) != 0xffe00000)
        return -1;
    /* layer check */
    if ((header & (3<<17)) == 0)
        return -1;
    /* bit rate */
    if ((header & (0xf<<12)) == 0xf<<12)
        return -1;
    /* frequency */
    if ((header & (3<<10)) == 3<<10)
        return -1;
    return 0;
}
