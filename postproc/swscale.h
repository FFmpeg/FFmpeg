
// *** bilinear scaling and yuv->rgb & yuv->yuv conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// *** Designed to upscale, but may work for downscale too.
// dstbpp == 12 -> yv12 output
void SwScale_YV12slice(unsigned char* srcptr[],int stride[], int srcSliceY,
			     int srcSliceH, uint8_t* dstptr[], int dststride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH);
// generating tables
void SwScale_Init();
