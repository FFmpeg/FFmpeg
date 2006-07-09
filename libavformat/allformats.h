#ifndef ALLFORMATS_H
#define ALLFORMATS_H

/* mpeg.c */
extern AVInputFormat mpegps_demuxer;
int mpegps_init(void);

/* mpegts.c */
extern AVInputFormat mpegts_demuxer;
int mpegts_init(void);

/* rm.c */
int rm_init(void);

/* crc.c */
int crc_init(void);

/* img.c */
int img_init(void);

/* img2.c */
int img2_init(void);

/* asf.c */
int asf_init(void);

/* avienc.c */
int avienc_init(void);

/* avidec.c */
int avidec_init(void);

/* swf.c */
int swf_init(void);

/* mov.c */
int mov_init(void);

/* movenc.c */
int movenc_init(void);

/* flvenc.c */
int flvenc_init(void);

/* flvdec.c */
int flvdec_init(void);

/* jpeg.c */
int jpeg_init(void);

/* gif.c */
int gif_init(void);

/* au.c */
int au_init(void);

/* amr.c */
int amr_init(void);

/* wav.c */
int ff_wav_init(void);

/* mmf.c */
int ff_mmf_init(void);

/* raw.c */
int pcm_read_seek(AVFormatContext *s,
                  int stream_index, int64_t timestamp, int flags);
int raw_init(void);

/* mp3.c */
int mp3_init(void);

/* yuv4mpeg.c */
int yuv4mpeg_init(void);

/* ogg2.c */
int ogg_init(void);

/* ogg.c */
int libogg_init(void);

/* dv.c */
int ff_dv_init(void);

/* ffm.c */
int ffm_init(void);

/* rtsp.c */
extern AVInputFormat redir_demuxer;
int redir_open(AVFormatContext **ic_ptr, ByteIOContext *f);

/* 4xm.c */
int fourxm_init(void);

/* psxstr.c */
int str_init(void);

/* idroq.c */
int roq_init(void);

/* ipmovie.c */
int ipmovie_init(void);

/* nut.c */
int nut_init(void);

/* wc3movie.c */
int wc3_init(void);

/* westwood.c */
int westwood_init(void);

/* segafilm.c */
int film_init(void);

/* idcin.c */
int idcin_init(void);

/* flic.c */
int flic_init(void);

/* sierravmd.c */
int vmd_init(void);

/* matroska.c */
int matroska_init(void);

/* sol.c */
int sol_init(void);

/* electronicarts.c */
int ea_init(void);

/* nsvdec.c */
int nsvdec_init(void);

/* daud.c */
int daud_init(void);

/* nuv.c */
int nuv_init(void);

/* gxf.c */
int gxf_init(void);

/* aiff.c */
int ff_aiff_init(void);

/* voc.c */
int voc_init(void);

/* tta.c */
int tta_init(void);

/* adts.c */
int ff_adts_init(void);

/* mm.c */
int mm_init(void);

/* avs.c */
int avs_init(void);

/* smacker.c */
int smacker_init(void);

/* v4l2.c */
int v4l2_init(void);

#if 0
extern AVImageFormat pnm_image_format;
extern AVImageFormat pbm_image_format;
extern AVImageFormat pgm_image_format;
extern AVImageFormat ppm_image_format;
extern AVImageFormat pam_image_format;
extern AVImageFormat pgmyuv_image_format;
extern AVImageFormat yuv_image_format;
#ifdef CONFIG_ZLIB
extern AVImageFormat png_image_format;
#endif
extern AVImageFormat jpeg_image_format;
#endif
extern AVImageFormat gif_image_format;
//extern AVImageFormat sgi_image_format; //broken in itself

#endif
