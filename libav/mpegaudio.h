
/* max compressed frame size */
#define MPA_MAX_CODED_FRAME_SIZE 1200

#define MPA_FRAME_SIZE 1152 

#define SAMPLES_BUF_SIZE 4096
#define SBLIMIT 32 /* number of subbands */
#define DCT_BITS 14 /* number of bits for the DCT */
#define MUL(a,b) (((a) * (b)) >> DCT_BITS)
#define FIX(a)   ((int)((a) * (1 << DCT_BITS)))

typedef struct MpegAudioContext {
    PutBitContext pb;
    int freq, bit_rate;
    int lsf;           /* 1 if mpeg2 low bitrate selected */
    int bitrate_index; /* bit rate */
    int freq_index;
    int frame_size; /* frame size, in bits, without padding */
    long long nb_samples; /* total number of samples encoded */
    /* padding computation */
    int frame_frac, frame_frac_incr, do_padding;
    short samples_buf[SAMPLES_BUF_SIZE]; /* buffer for filter */
    int samples_offset;       /* offset in samples_buf */
    int sb_samples[3][12][SBLIMIT];
    unsigned char scale_factors[SBLIMIT][3]; /* scale factors */
    unsigned char scale_code[SBLIMIT];       /* code to group 3 scale factors */
    int sblimit; /* number of used subbands */
    const unsigned char *alloc_table;
} MpegAudioContext;

