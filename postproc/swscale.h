
/* values for the flags, the stuff on the command line is different */
#define SWS_FAST_BILINEAR 1
#define SWS_BILINEAR 2
#define SWS_BICUBIC  4
#define SWS_X        8
#define SWS_FULL_UV_IPOL 0x100
#define SWS_PRINT_INFO 0x1000

#define SWS_MAX_SIZE 2000

/* this struct should be aligned on at least 32-byte boundary */
typedef struct{
	int srcW, srcH, dstW, dstH;
	int chrDstW, chrDstH;
	int lumXInc, chrXInc;
	int lumYInc, chrYInc;
	int dstFormat, srcFormat;
	int16_t __attribute__((aligned(8))) *lumPixBuf[SWS_MAX_SIZE];
	int16_t __attribute__((aligned(8))) *chrPixBuf[SWS_MAX_SIZE];
	int16_t __attribute__((aligned(8))) hLumFilter[SWS_MAX_SIZE*5];
	int16_t __attribute__((aligned(8))) hLumFilterPos[SWS_MAX_SIZE];
	int16_t __attribute__((aligned(8))) hChrFilter[SWS_MAX_SIZE*5];
	int16_t __attribute__((aligned(8))) hChrFilterPos[SWS_MAX_SIZE];
	int16_t __attribute__((aligned(8))) vLumFilter[SWS_MAX_SIZE*5];
	int16_t __attribute__((aligned(8))) vLumFilterPos[SWS_MAX_SIZE];
	int16_t __attribute__((aligned(8))) vChrFilter[SWS_MAX_SIZE*5];
	int16_t __attribute__((aligned(8))) vChrFilterPos[SWS_MAX_SIZE];

// Contain simply the values from v(Lum|Chr)Filter just nicely packed for mmx
	int16_t __attribute__((aligned(8))) lumMmxFilter[SWS_MAX_SIZE*20];
	int16_t __attribute__((aligned(8))) chrMmxFilter[SWS_MAX_SIZE*20];

	int hLumFilterSize;
	int hChrFilterSize;
	int vLumFilterSize;
	int vChrFilterSize;
	int vLumBufSize;
	int vChrBufSize;

	uint8_t __attribute__((aligned(32))) funnyYCode[10000];
	uint8_t __attribute__((aligned(32))) funnyUVCode[10000];

	int canMMX2BeUsed;

	int lastInLumBuf;
	int lastInChrBuf;
	int lumBufIndex;
	int chrBufIndex;
	int dstY;
	int flags;
} SwsContext;
//FIXME check init (where 0)

typedef struct {
	double *lumH;
	double *lumV;
	double *chrH;
	double *chrV;
	int length;
} SwsFilter;


// *** bilinear scaling and yuv->rgb & yuv->yuv conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// dstbpp == 12 -> yv12 output
// will use sws_flags
void SwScale_YV12slice(unsigned char* src[],int srcStride[], int srcSliceY,
			     int srcSliceH, uint8_t* dst[], int dstStride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH);

// Obsolete, will be removed soon
void SwScale_Init();



void freeSwsContext(SwsContext swsContext);

SwsContext *getSwsContext(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat, int flags,
			 SwsFilter *srcFilter, SwsFilter *dstFilter);

extern void (*swScale)(SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]);

double *getGaussian(double variance, double quality);

void normalize(double *coeff, int length, double height);

double *conv(double *a, int aLength, double *b, int bLength);

