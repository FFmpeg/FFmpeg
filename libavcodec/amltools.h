
#ifndef _AMLTOOLS_H_
#define _AMLTOOLS_H_

#include "avcodec.h"
#include "amcodec/amports/vformat.h"

int amlsysfs_write_string(AVCodecContext *avctx, const char *path, const char *value);
int amlsysfs_write_int(AVCodecContext *avctx, const char *path, int value);
int64_t amlsysfs_read_int(AVCodecContext *avctx, const char *path, int base);

vformat_t aml_get_vformat(AVCodecContext *avctx);
vdec_type_t aml_get_vdec_type(AVCodecContext *avctx);

#endif /* _AMLTOOLS_H_*/
