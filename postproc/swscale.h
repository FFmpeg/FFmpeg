
// *** bilinear scaling and yuv->rgb & yuv->yuv conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// *** Designed to upscale, but may work for downscale too.
// s_xinc = (src_width << 8) / dst_width
// s_yinc = (src_height << 16) / dst_height
// dstbpp == 12 -> yv12 output
void SwScale_YV12slice(unsigned char* srcptr[],int stride[], int y, int h,
		       uint8_t* dstptr[], int dststride, int dstw, int dstbpp,
		       unsigned int s_xinc,unsigned int s_yinc);

// generating tables
void SwScale_Init();
