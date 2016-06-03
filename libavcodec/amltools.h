
#ifndef _AMLTOOLS_H_
#define _AMLTOOLS_H_

#include "avcodec.h"

int amlsysfs_write_string(AVCodecContext *avctx, const char *path, const char *value);
int amlsysfs_write_int(AVCodecContext *avctx, const char *path, int value);

#endif /* _AMLTOOLS_H_*/
