/*
 *
 *  rgb2rgb.h, Software RGB to RGB convertor
 *
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


extern void palette8torgb32(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb16(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb15(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);
extern void palette8torgb24(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette);

extern void yv12toyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	int width, int height, int lumStride, int chromStride, int dstStride);
extern void yuy2toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst, unsigned num_pixels);

#endif
