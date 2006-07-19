#ifndef FFMPEG_GXF_H
#define FFMPEG_GXF_H

/* gxf.c */
typedef enum {
    PKT_MAP = 0xbc,
    PKT_MEDIA = 0xbf,
    PKT_EOS = 0xfb,
    PKT_FLT = 0xfc,
    PKT_UMF = 0xfd
} pkt_type_t;

#endif /* FFMPEG_GXF_H */
