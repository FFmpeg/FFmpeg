include $(SUBDIR)../config.mak

NAME = avfilter
FFLIBS = avutil swscale
FFLIBS-$(CONFIG_ASYNCTS_FILTER) += avresample
FFLIBS-$(CONFIG_RESAMPLE_FILTER) += avresample

FFLIBS-$(CONFIG_ACONVERT_FILTER)             += swresample
FFLIBS-$(CONFIG_AMOVIE_FILTER)               += avformat avcodec
FFLIBS-$(CONFIG_ARESAMPLE_FILTER)            += swresample
FFLIBS-$(CONFIG_MOVIE_FILTER)                += avformat avcodec
FFLIBS-$(CONFIG_PAN_FILTER)                  += swresample
FFLIBS-$(CONFIG_REMOVELOGO_FILTER)           += avformat avcodec
FFLIBS-$(CONFIG_MP_FILTER)                   += avcodec postproc

HEADERS = asrc_abuffer.h                                                \
          avcodec.h                                                     \
          avfilter.h                                                    \
          avfiltergraph.h                                               \
          buffersink.h                                                  \
          buffersrc.h                                                   \
          version.h                                                     \
          vsrc_buffer.h                                                 \

OBJS = allfilters.o                                                     \
       audio.o                                                          \
       avfilter.o                                                       \
       avfiltergraph.o                                                  \
       buffer.o                                                         \
       buffersink.o                                                     \
       buffersrc.o                                                      \
       defaults.o                                                       \
       drawutils.o                                                      \
       formats.o                                                        \
       graphdump.o                                                      \
       graphparser.o                                                    \
       src_buffer.o                                                     \
       transform.o                                                      \
       vf_scale.o                                                       \
       video.o                                                          \


OBJS-$(CONFIG_AVCODEC)                       += avcodec.o
OBJS-$(CONFIG_AVFORMAT)                      += lavfutils.o
OBJS-$(CONFIG_SWSCALE)                       += lswsutils.o

OBJS-$(CONFIG_ACONVERT_FILTER)               += af_aconvert.o
OBJS-$(CONFIG_AFORMAT_FILTER)                += af_aformat.o
OBJS-$(CONFIG_AMERGE_FILTER)                 += af_amerge.o
OBJS-$(CONFIG_AMIX_FILTER)                   += af_amix.o
OBJS-$(CONFIG_ANULL_FILTER)                  += af_anull.o
OBJS-$(CONFIG_ARESAMPLE_FILTER)              += af_aresample.o
OBJS-$(CONFIG_ASHOWINFO_FILTER)              += af_ashowinfo.o
OBJS-$(CONFIG_ASPLIT_FILTER)                 += split.o
OBJS-$(CONFIG_ASTREAMSYNC_FILTER)            += af_astreamsync.o
OBJS-$(CONFIG_ASYNCTS_FILTER)                += af_asyncts.o
OBJS-$(CONFIG_EARWAX_FILTER)                 += af_earwax.o
OBJS-$(CONFIG_PAN_FILTER)                    += af_pan.o
OBJS-$(CONFIG_RESAMPLE_FILTER)               += af_resample.o
OBJS-$(CONFIG_SILENCEDETECT_FILTER)          += af_silencedetect.o
OBJS-$(CONFIG_VOLUME_FILTER)                 += af_volume.o

OBJS-$(CONFIG_AEVALSRC_FILTER)               += asrc_aevalsrc.o
OBJS-$(CONFIG_AMOVIE_FILTER)                 += src_movie.o
OBJS-$(CONFIG_ANULLSRC_FILTER)               += asrc_anullsrc.o

OBJS-$(CONFIG_ABUFFERSINK_FILTER)            += sink_buffer.o
OBJS-$(CONFIG_ANULLSINK_FILTER)              += asink_anullsink.o

OBJS-$(CONFIG_ASS_FILTER)                    += vf_ass.o
OBJS-$(CONFIG_BBOX_FILTER)                   += bbox.o vf_bbox.o
OBJS-$(CONFIG_BLACKDETECT_FILTER)            += vf_blackdetect.o
OBJS-$(CONFIG_BLACKFRAME_FILTER)             += vf_blackframe.o
OBJS-$(CONFIG_BOXBLUR_FILTER)                += vf_boxblur.o
OBJS-$(CONFIG_COLORMATRIX_FILTER)            += vf_colormatrix.o
OBJS-$(CONFIG_COPY_FILTER)                   += vf_copy.o
OBJS-$(CONFIG_CROP_FILTER)                   += vf_crop.o
OBJS-$(CONFIG_CROPDETECT_FILTER)             += vf_cropdetect.o
OBJS-$(CONFIG_DELOGO_FILTER)                 += vf_delogo.o
OBJS-$(CONFIG_DESHAKE_FILTER)                += vf_deshake.o
OBJS-$(CONFIG_DRAWBOX_FILTER)                += vf_drawbox.o
OBJS-$(CONFIG_DRAWTEXT_FILTER)               += vf_drawtext.o
OBJS-$(CONFIG_FADE_FILTER)                   += vf_fade.o
OBJS-$(CONFIG_FIELDORDER_FILTER)             += vf_fieldorder.o
OBJS-$(CONFIG_FIFO_FILTER)                   += vf_fifo.o
OBJS-$(CONFIG_FORMAT_FILTER)                 += vf_format.o
OBJS-$(CONFIG_FPS_FILTER)                    += vf_fps.o
OBJS-$(CONFIG_FREI0R_FILTER)                 += vf_frei0r.o
OBJS-$(CONFIG_GRADFUN_FILTER)                += vf_gradfun.o
OBJS-$(CONFIG_HFLIP_FILTER)                  += vf_hflip.o
OBJS-$(CONFIG_HQDN3D_FILTER)                 += vf_hqdn3d.o
OBJS-$(CONFIG_IDET_FILTER)                   += vf_idet.o
OBJS-$(CONFIG_LUT_FILTER)                    += vf_lut.o
OBJS-$(CONFIG_LUTRGB_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_LUTYUV_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_MP_FILTER)                     += vf_mp.o
OBJS-$(CONFIG_NEGATE_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_NOFORMAT_FILTER)               += vf_format.o
OBJS-$(CONFIG_NULL_FILTER)                   += vf_null.o
OBJS-$(CONFIG_OCV_FILTER)                    += vf_libopencv.o
OBJS-$(CONFIG_OVERLAY_FILTER)                += vf_overlay.o
OBJS-$(CONFIG_PAD_FILTER)                    += vf_pad.o
OBJS-$(CONFIG_PIXDESCTEST_FILTER)            += vf_pixdesctest.o
OBJS-$(CONFIG_REMOVELOGO_FILTER)             += bbox.o lswsutils.o lavfutils.o vf_removelogo.o
OBJS-$(CONFIG_SELECT_FILTER)                 += vf_select.o
OBJS-$(CONFIG_SETDAR_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_SETFIELD_FILTER)               += vf_setfield.o
OBJS-$(CONFIG_SETPTS_FILTER)                 += vf_setpts.o
OBJS-$(CONFIG_SETSAR_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_SETTB_FILTER)                  += vf_settb.o
OBJS-$(CONFIG_SHOWINFO_FILTER)               += vf_showinfo.o
OBJS-$(CONFIG_SLICIFY_FILTER)                += vf_slicify.o
OBJS-$(CONFIG_SPLIT_FILTER)                  += split.o
OBJS-$(CONFIG_SUPER2XSAI_FILTER)             += vf_super2xsai.o
OBJS-$(CONFIG_SWAPUV_FILTER)                 += vf_swapuv.o
OBJS-$(CONFIG_THUMBNAIL_FILTER)              += vf_thumbnail.o
OBJS-$(CONFIG_TILE_FILTER)                   += vf_tile.o
OBJS-$(CONFIG_TINTERLACE_FILTER)             += vf_tinterlace.o
OBJS-$(CONFIG_TRANSPOSE_FILTER)              += vf_transpose.o
OBJS-$(CONFIG_UNSHARP_FILTER)                += vf_unsharp.o
OBJS-$(CONFIG_VFLIP_FILTER)                  += vf_vflip.o
OBJS-$(CONFIG_YADIF_FILTER)                  += vf_yadif.o

OBJS-$(CONFIG_CELLAUTO_FILTER)               += vsrc_cellauto.o
OBJS-$(CONFIG_COLOR_FILTER)                  += vsrc_color.o
OBJS-$(CONFIG_FREI0R_SRC_FILTER)             += vf_frei0r.o
OBJS-$(CONFIG_LIFE_FILTER)                   += vsrc_life.o
OBJS-$(CONFIG_MANDELBROT_FILTER)             += vsrc_mandelbrot.o
OBJS-$(CONFIG_MOVIE_FILTER)                  += src_movie.o
OBJS-$(CONFIG_MPTESTSRC_FILTER)              += vsrc_mptestsrc.o
OBJS-$(CONFIG_NULLSRC_FILTER)                += vsrc_testsrc.o
OBJS-$(CONFIG_RGBTESTSRC_FILTER)             += vsrc_testsrc.o
OBJS-$(CONFIG_TESTSRC_FILTER)                += vsrc_testsrc.o

OBJS-$(CONFIG_BUFFERSINK_FILTER)             += sink_buffer.o
OBJS-$(CONFIG_NULLSINK_FILTER)               += vsink_nullsink.o

OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/mp_image.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/img_format.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_decimate.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_denoise3d.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_detc.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_dint.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_divtc.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_down3dright.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_dsize.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_eq2.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_eq.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_field.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_fil.o
#OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_filmdint.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_fixpts.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_framestep.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_fspp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_geq.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_harddup.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_hqdn3d.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_hue.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_il.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_ilpack.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_ivtc.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_kerndeint.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_mcdeint.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_noise.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_ow.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_palette.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_perspective.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_phase.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_pp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_pp7.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_pullup.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_qp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_rectangle.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_rotate.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_sab.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_smartblur.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_softpulldown.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_softskip.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_spp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_stereo3d.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_telecine.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_tile.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_tinterlace.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_unsharp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_uspp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_yuvcsp.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/vf_yvu9.o
OBJS-$(CONFIG_MP_FILTER) += libmpcodecs/pullup.o


TESTPROGS = drawutils formats

TOOLS = graph2dot                                                       \
        lavfi-showfiltfmts                                              \
