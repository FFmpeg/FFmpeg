/*
 *
 *  rgb2rgb.h, Software RGB to RGB convertor
 *  pluralize by Software PAL8 to RGB convertor
 *               Software YUV to YUV convertor
 *               Software YUV to RGB convertor
 */

#ifndef RGB2RGB_INCLUDED
#define RGB2RGB_INCLUDED

extern void rgb24to32(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32to24(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb15to16(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32to16(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32to15(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb24to16(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb24to15(const uint8_t *src,uint8_t *dst,unsigned src_size);
extern void rgb32tobgr32(const uint8_t *src, uint8_t *dst, unsigned src_size);


extern void palette8torgb32(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb16(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb15(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb24(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);

extern void yv12toyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int dstStride);
extern void yuy2toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int srcStride);
extern void rgb24toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int srcStride);

extern void interleaveBytes(uint8_t *src1, uint8_t *src2, uint8_t *dst,
			    int width, int height, int src1Stride, int src2Stride, int dstStride);
	

#define MODE_RGB  0x1
#define MODE_BGR  0x2

typedef void (* yuv2rgb_fun) (uint8_t * image, uint8_t * py,
			      uint8_t * pu, uint8_t * pv,
			      int h_size, int v_size,
			      int rgb_stride, int y_stride, int uv_stride);

extern yuv2rgb_fun yuv2rgb;

void yuv2rgb_init (int bpp, int mode);

#endif
