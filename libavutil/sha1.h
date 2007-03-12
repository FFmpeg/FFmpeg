#ifndef AV_SHA1_H
#define AV_SHA1_H

extern const int av_sha1_size;

struct AVSHA1;

void av_sha1_init(struct AVSHA1* context);
void av_sha1_update(struct AVSHA1* context, uint8_t* data, unsigned int len);
void av_sha1_final(struct AVSHA1* context, uint8_t digest[20]);
#endif
