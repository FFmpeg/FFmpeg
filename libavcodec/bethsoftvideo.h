#ifndef AVCODEC_BETHSOFTVIDEO_H
#define AVCODEC_BETHSOFTVIDEO_H

enum BethsoftVidBlockType
{
    PALETTE_BLOCK       = 0x02,
    FIRST_AUDIO_BLOCK   = 0x7c,
    AUDIO_BLOCK         = 0x7d,
    VIDEO_I_FRAME       = 0x03,
    VIDEO_P_FRAME       = 0x01,
    VIDEO_YOFF_P_FRAME  = 0x04,
    EOF_BLOCK           = 0x14,
};

#endif // AVCODEC_BETHSOFTVIDEO_H
