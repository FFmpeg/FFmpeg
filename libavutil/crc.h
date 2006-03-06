#ifndef CRC_H
#define CRC_H

typedef uint32_t AVCRC;

extern AVCRC *av_crcEDB88320;
extern AVCRC *av_crc04C11DB7;
extern AVCRC *av_crc8005    ;
extern AVCRC *av_crc07      ;

int av_crc_init(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size);
uint32_t av_crc(const AVCRC *ctx, uint32_t start_crc, const uint8_t *buffer, size_t length);

#endif /* CRC_H */

