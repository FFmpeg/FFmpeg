
/* values for the flags, the stuff on the command line is different */
#define SWS_FAST_BILINEAR 1
#define SWS_BILINEAR 2
#define SWS_BICUBIC  4
#define SWS_X        8
#define SWS_FULL_UV_IPOL 0x100
#define SWS_PRINT_INFO 0x1000

#define SWS_MAX_REDUCE_CUTOFF 0.002

/* this struct should be aligned on at least 32-byte boundary */
typedef struct{
	int srcW, srcH, dstW, dstH;
	int chrDstW, chrDstH;
	int lumXInc, chrXInc;
	int lumYInc, chrYInc;
	int dstFormat, srcFormat;

	int16_t **lumPixBuf;
	int16_t **chrPixBuf;
	int16_t *hLumFilter;
	int16_t *hLumFilterPos;
	int16_t *hChrFilter;
	int16_t *hChrFilterPos;
	int16_t *vLumFilter;
	int16_t *vLumFilterPos;
	int16_t *vChrFilter;
	int16_t *vChrFilterPos;

// Contain simply the values from v(Lum|Chr)Filter just nicely packed for mmx
	int16_t  *lumMmxFilter;
	int16_t  *chrMmxFilter;

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

// when used for filters they must have an odd number of elements
// coeffs cannot be shared between vectors
typedef struct {
	double *coeff;
	int length;
} SwsVector;

// vectors can be shared
typedef struct {
	SwsVector *lumH;
	SwsVector *lumV;
	SwsVector *chrH;
	SwsVector *chrV;
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



void freeSwsContext(SwsContext *swsContext);

SwsContext *getSwsContext(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat, int flags,
			 SwsFilter *srcFilter, SwsFilter *dstFilter);

extern void (*swScale)(SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]);

SwsVector *getGaussianVec(double variance, double quality);
SwsVector *getIdentityVec(void);
void scaleVec(SwsVector *a, double scalar);
void normalizeVec(SwsVector *a, double height);
void convVec(SwsVector *a, SwsVector *b);
void addVec(SwsVector *a, SwsVector *b);
void subVec(SwsVector *a, SwsVector *b);
void shiftVec(SwsVector *a, int shift);
SwsVector *cloneVec(SwsVector *a);

void printVec(SwsVector *a);
void freeVec(SwsVector *a);

