/*
 *
 *  rgb2rgb.h, Software RGB to RGB convertor
 *  pluralize by Software PAL8 to RGB convertor
 *               Software YUV to YUV convertor
 *               Software YUV to RGB convertor
 */

#ifndef RGB2RGB_INCLUDED
#define RGB2RGB_INCLUDED

/* A full collection of rgb to rgb(bgr) convertors */
extern void rgb24to32(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb24to16(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb24to15(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32to24(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32to16(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32to15(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb15to16(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb15to24(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb15to32(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb16to15(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb16to24(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb16to32(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb24tobgr32(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb24tobgr24(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb24tobgr16(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb24tobgr15(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb32tobgr32(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb32tobgr24(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb32tobgr16(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb32tobgr15(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb16tobgr32(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb16tobgr24(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb16tobgr16(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb16tobgr15(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb15tobgr32(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb15tobgr24(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb15tobgr16(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb15tobgr15(const uint8_t *src, uint8_t *dst, unsigned src_size);
extern void rgb8tobgr8(const uint8_t *src, uint8_t *dst, unsigned src_size);


extern void palette8torgb32(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8tobgr32(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb24(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8tobgr24(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb16(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8tobgr16(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb15(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8tobgr15(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);

extern void yv12toyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	unsigned int width, unsigned int height,
	int lumStride, int chromStride, int dstStride);
extern void yuv422ptoyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	unsigned int width, unsigned int height,
	int lumStride, int chromStride, int dstStride);
extern void yuy2toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	int lumStride, int chromStride, int srcStride);
extern void rgb24toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	int lumStride, int chromStride, int srcStride);
extern void planar2x(const uint8_t *src, uint8_t *dst, int width, int height, int srcStride, int dstStride);

extern void interleaveBytes(uint8_t *src1, uint8_t *src2, uint8_t *dst,
			    unsigned width, unsigned height, int src1Stride,
			    int src2Stride, int dstStride);

extern void vu9_to_vu12(const uint8_t *src1, const uint8_t *src2,
			uint8_t *dst1, uint8_t *dst2,
			unsigned width, unsigned height,
			int srcStride1, int srcStride2,
			int dstStride1, int dstStride2);

extern void yvu9_to_yuy2(const uint8_t *src1, const uint8_t *src2, const uint8_t *src3,
			uint8_t *dst,
			unsigned width, unsigned height,
			int srcStride1, int srcStride2,
			int srcStride3, int dstStride);
	

#define MODE_RGB  0x1
#define MODE_BGR  0x2

static void yuv2rgb(uint8_t * image, uint8_t * py,
			      uint8_t * pu, uint8_t * pv,
			      unsigned h_size, unsigned v_size,
			      int rgb_stride, int y_stride, int uv_stride){
printf("broken, this should use the swscaler\n");
}

static void yuv2rgb_init (unsigned bpp, int mode){
printf("broken, this should use the swscaler\n");
}

#endif
