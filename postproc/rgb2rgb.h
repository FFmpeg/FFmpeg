/*
 *
 *  rgb2rgb.h, Software RGB to RGB convertor
 *
 */

#ifndef RGB2RGB_INCLUDED
#define RGB2RGB_INCLUDED

extern void rgb24to32(const uint8_t *src,uint8_t *dst,uint32_t src_size);
extern void rgb32to24(const uint8_t *src,uint8_t *dst,uint32_t src_size);
extern void rgb15to16(const uint8_t *src,uint8_t *dst,uint32_t src_size);

void rgb32to16(uint8_t *src, uint8_t *dst, int src_size);
void rgb32to15(uint8_t *src, uint8_t *dst, int src_size);
void palette8torgb32(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette);
void palette8torgb16(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette);
void palette8torgb15(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette);
void palette8torgb24(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette);

void yv12toyuy2(uint8_t *ysrc, uint8_t *usrc, uint8_t *vsrc, uint8_t *dst, int src_size);
void yuy2toyv12(uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst, int src_size);


#endif
