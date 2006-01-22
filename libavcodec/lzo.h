#ifndef _LZO_H
#define LZO_H

#define LZO_INPUT_DEPLETED 1
#define LZO_OUTPUT_FULL 2
#define LZO_INVALID_BACKPTR 4
#define LZO_ERROR 8

#define LZO_INPUT_PADDING 4
#define LZO_OUTPUT_PADDING 12

int lzo1x_decode(void *out, int *outlen, void *in, int *inlen);

#endif
