
#define AC3_FRAME_SIZE (6*256)
#define AC3_MAX_CODED_FRAME_SIZE 3840 /* in bytes */
#define AC3_MAX_CHANNELS 6

typedef struct AC3EncodeContext {
    PutBitContext pb;
    int nb_channels;
    int nb_all_channels;
    int lfe_channel;
    int bit_rate;
    int sample_rate;
    int bsid;
    int frame_size_min; /* minimum frame size in case rounding is necessary */
    int frame_size; /* current frame size in words */
    int halfratecod;
    int frmsizecod;
    int fscod; /* frequency */
    int acmod;
    int lfe;
    int bsmod;
    short last_samples[AC3_MAX_CHANNELS][256];
    int chbwcod[AC3_MAX_CHANNELS];
    int nb_coefs[AC3_MAX_CHANNELS];
    
    /* bitrate allocation control */
    int sgaincod, sdecaycod, fdecaycod, dbkneecod, floorcod; 
    int sgain, sdecay, fdecay, dbknee, floor;
    int csnroffst;
    int fgaincod[AC3_MAX_CHANNELS];
    int fsnroffst[AC3_MAX_CHANNELS];
    /* mantissa encoding */
    int mant1_cnt, mant2_cnt, mant4_cnt;
} AC3EncodeContext;
