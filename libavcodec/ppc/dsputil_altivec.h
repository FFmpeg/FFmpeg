extern int pix_abs16x16_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_abs8x8_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_sum_altivec(UINT8 * pix, int line_size);

extern int has_altivec(void);
