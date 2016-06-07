#include "libavformat/avformat.h"

#if !CONFIG_ALSA_OUTDEV
AVOutputFormat ff_alsa_muxer;
#endif

#if !CONFIG_ALSA_INDEV
AVInputFormat ff_alsa_demuxer;
#endif

#if !CONFIG_AVFOUNDATION_INDEV
AVInputFormat ff_avfoundation_demuxer;
#endif

#if !CONFIG_BKTR_INDEV
AVInputFormat ff_bktr_demuxer;
#endif

#if !CONFIG_CACA_OUTDEV
AVOutputFormat ff_caca_muxer;
#endif

#if !CONFIG_DECKLINK_OUTDEV
AVOutputFormat ff_decklink_muxer;
#endif

#if !CONFIG_DECKLINK_INDEV
AVInputFormat ff_decklink_demuxer;
#endif

#if !CONFIG_DV1394_INDEV
AVInputFormat ff_dv1394_demuxer;
#endif

#if !CONFIG_FBDEV_OUTDEV
AVOutputFormat ff_fbdev_muxer;
#endif

#if !CONFIG_FBDEV_INDEV
AVInputFormat ff_fbdev_demuxer;
#endif

#if !CONFIG_IEC61883_INDEV
AVInputFormat ff_iec61883_demuxer;
#endif

#if !CONFIG_JACK_INDEV
AVInputFormat ff_jack_demuxer;
#endif

#if !CONFIG_LAVFI_INDEV
AVInputFormat ff_lavfi_demuxer;
#endif

#if !CONFIG_OPENAL_INDEV
AVInputFormat ff_openal_demuxer;
#endif

#if !CONFIG_OPENGL_OUTDEV
AVOutputFormat ff_opengl_muxer;
#endif

#if !CONFIG_OSS_OUTDEV
AVOutputFormat ff_oss_muxer;
#endif

#if !CONFIG_OSS_INDEV
AVInputFormat ff_oss_demuxer;
#endif

#if !CONFIG_PULSE_OUTDEV
AVOutputFormat ff_pulse_muxer;
#endif

#if !CONFIG_PULSE_INDEV
AVInputFormat ff_pulse_demuxer;
#endif

#if !CONFIG_QTKIT_INDEV
AVInputFormat ff_qtkit_demuxer;
#endif

#if !CONFIG_SDL_OUTDEV
AVOutputFormat ff_sdl_muxer;
#endif

#if !CONFIG_SNDIO_OUTDEV
AVOutputFormat ff_sndio_muxer;
#endif

#if !CONFIG_SNDIO_INDEV
AVInputFormat ff_sndio_demuxer;
#endif

#if !CONFIG_V4L2_OUTDEV
AVOutputFormat ff_v4l2_muxer;
#endif

#if !CONFIG_V4L2_INDEV
AVInputFormat ff_v4l2_demuxer;
#endif

#if !CONFIG_X11GRAB_INDEV
AVInputFormat ff_x11grab_demuxer;
#endif

#if !CONFIG_X11GRAB_XCB_INDEV
AVInputFormat ff_x11grab_xcb_demuxer;
#endif

#if !CONFIG_XV_OUTDEV
AVOutputFormat ff_xv_muxer;
#endif

#if !CONFIG_LIBCDIO_INDEV
AVInputFormat ff_libcdio_demuxer;
#endif

#if !CONFIG_LIBDC1394_INDEV
AVInputFormat ff_libdc1394_demuxer;
#endif