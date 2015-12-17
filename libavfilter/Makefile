include $(SUBDIR)../config.mak

NAME = avfilter

HEADERS = avfilter.h                                                    \
          avfiltergraph.h                                               \
          buffersink.h                                                  \
          buffersrc.h                                                   \
          version.h                                                     \

OBJS = allfilters.o                                                     \
       audio.o                                                          \
       avfilter.o                                                       \
       avfiltergraph.o                                                  \
       buffersink.o                                                     \
       buffersrc.o                                                      \
       drawutils.o                                                      \
       fifo.o                                                           \
       formats.o                                                        \
       framepool.o                                                      \
       graphdump.o                                                      \
       graphparser.o                                                    \
       opencl_allkernels.o                                              \
       transform.o                                                      \
       video.o                                                          \

OBJS-$(CONFIG_ACOMPRESSOR_FILTER)            += af_sidechaincompress.o
OBJS-$(CONFIG_ACROSSFADE_FILTER)             += af_afade.o
OBJS-$(CONFIG_ADELAY_FILTER)                 += af_adelay.o
OBJS-$(CONFIG_AECHO_FILTER)                  += af_aecho.o
OBJS-$(CONFIG_AEMPHASIS_FILTER)              += af_aemphasis.o
OBJS-$(CONFIG_AEVAL_FILTER)                  += aeval.o
OBJS-$(CONFIG_AFADE_FILTER)                  += af_afade.o
OBJS-$(CONFIG_AFORMAT_FILTER)                += af_aformat.o
OBJS-$(CONFIG_AGATE_FILTER)                  += af_agate.o
OBJS-$(CONFIG_AINTERLEAVE_FILTER)            += f_interleave.o
OBJS-$(CONFIG_ALIMITER_FILTER)               += af_alimiter.o
OBJS-$(CONFIG_ALLPASS_FILTER)                += af_biquads.o
OBJS-$(CONFIG_AMERGE_FILTER)                 += af_amerge.o
OBJS-$(CONFIG_AMIX_FILTER)                   += af_amix.o
OBJS-$(CONFIG_ANULL_FILTER)                  += af_anull.o
OBJS-$(CONFIG_APAD_FILTER)                   += af_apad.o
OBJS-$(CONFIG_APERMS_FILTER)                 += f_perms.o
OBJS-$(CONFIG_APHASER_FILTER)                += af_aphaser.o generate_wave_table.o
OBJS-$(CONFIG_APULSATOR_FILTER)              += af_apulsator.o
OBJS-$(CONFIG_AREALTIME_FILTER)              += f_realtime.o
OBJS-$(CONFIG_ARESAMPLE_FILTER)              += af_aresample.o
OBJS-$(CONFIG_AREVERSE_FILTER)               += f_reverse.o
OBJS-$(CONFIG_ASELECT_FILTER)                += f_select.o
OBJS-$(CONFIG_ASENDCMD_FILTER)               += f_sendcmd.o
OBJS-$(CONFIG_ASETNSAMPLES_FILTER)           += af_asetnsamples.o
OBJS-$(CONFIG_ASETPTS_FILTER)                += setpts.o
OBJS-$(CONFIG_ASETRATE_FILTER)               += af_asetrate.o
OBJS-$(CONFIG_ASETTB_FILTER)                 += settb.o
OBJS-$(CONFIG_ASHOWINFO_FILTER)              += af_ashowinfo.o
OBJS-$(CONFIG_ASPLIT_FILTER)                 += split.o
OBJS-$(CONFIG_ASTATS_FILTER)                 += af_astats.o
OBJS-$(CONFIG_ASYNCTS_FILTER)                += af_asyncts.o
OBJS-$(CONFIG_ATEMPO_FILTER)                 += af_atempo.o
OBJS-$(CONFIG_ATRIM_FILTER)                  += trim.o
OBJS-$(CONFIG_AZMQ_FILTER)                   += f_zmq.o
OBJS-$(CONFIG_BANDPASS_FILTER)               += af_biquads.o
OBJS-$(CONFIG_BANDREJECT_FILTER)             += af_biquads.o
OBJS-$(CONFIG_BASS_FILTER)                   += af_biquads.o
OBJS-$(CONFIG_BIQUAD_FILTER)                 += af_biquads.o
OBJS-$(CONFIG_BS2B_FILTER)                   += af_bs2b.o
OBJS-$(CONFIG_CHANNELMAP_FILTER)             += af_channelmap.o
OBJS-$(CONFIG_CHANNELSPLIT_FILTER)           += af_channelsplit.o
OBJS-$(CONFIG_CHORUS_FILTER)                 += af_chorus.o generate_wave_table.o
OBJS-$(CONFIG_COMPAND_FILTER)                += af_compand.o
OBJS-$(CONFIG_COMPENSATIONDELAY_FILTER)      += af_compensationdelay.o
OBJS-$(CONFIG_DCSHIFT_FILTER)                += af_dcshift.o
OBJS-$(CONFIG_DYNAUDNORM_FILTER)             += af_dynaudnorm.o
OBJS-$(CONFIG_EARWAX_FILTER)                 += af_earwax.o
OBJS-$(CONFIG_EBUR128_FILTER)                += f_ebur128.o
OBJS-$(CONFIG_EQUALIZER_FILTER)              += af_biquads.o
OBJS-$(CONFIG_EXTRASTEREO_FILTER)            += af_extrastereo.o
OBJS-$(CONFIG_FLANGER_FILTER)                += af_flanger.o generate_wave_table.o
OBJS-$(CONFIG_HIGHPASS_FILTER)               += af_biquads.o
OBJS-$(CONFIG_JOIN_FILTER)                   += af_join.o
OBJS-$(CONFIG_LADSPA_FILTER)                 += af_ladspa.o
OBJS-$(CONFIG_LOWPASS_FILTER)                += af_biquads.o
OBJS-$(CONFIG_PAN_FILTER)                    += af_pan.o
OBJS-$(CONFIG_REPLAYGAIN_FILTER)             += af_replaygain.o
OBJS-$(CONFIG_RESAMPLE_FILTER)               += af_resample.o
OBJS-$(CONFIG_RUBBERBAND_FILTER)             += af_rubberband.o
OBJS-$(CONFIG_SIDECHAINCOMPRESS_FILTER)      += af_sidechaincompress.o
OBJS-$(CONFIG_SIDECHAINGATE_FILTER)          += af_agate.o
OBJS-$(CONFIG_SILENCEDETECT_FILTER)          += af_silencedetect.o
OBJS-$(CONFIG_SILENCEREMOVE_FILTER)          += af_silenceremove.o
OBJS-$(CONFIG_SOFALIZER_FILTER)              += af_sofalizer.o
OBJS-$(CONFIG_STEREOTOOLS_FILTER)            += af_stereotools.o
OBJS-$(CONFIG_STEREOWIDEN_FILTER)            += af_stereowiden.o
OBJS-$(CONFIG_TREBLE_FILTER)                 += af_biquads.o
OBJS-$(CONFIG_TREMOLO_FILTER)                += af_tremolo.o
OBJS-$(CONFIG_VIBRATO_FILTER)                += af_vibrato.o generate_wave_table.o
OBJS-$(CONFIG_VOLUME_FILTER)                 += af_volume.o
OBJS-$(CONFIG_VOLUMEDETECT_FILTER)           += af_volumedetect.o

OBJS-$(CONFIG_AEVALSRC_FILTER)               += aeval.o
OBJS-$(CONFIG_ANOISESRC_FILTER)              += asrc_anoisesrc.o
OBJS-$(CONFIG_ANULLSRC_FILTER)               += asrc_anullsrc.o
OBJS-$(CONFIG_FLITE_FILTER)                  += asrc_flite.o
OBJS-$(CONFIG_SINE_FILTER)                   += asrc_sine.o

OBJS-$(CONFIG_ANULLSINK_FILTER)              += asink_anullsink.o

OBJS-$(CONFIG_ASS_FILTER)                    += vf_subtitles.o
OBJS-$(CONFIG_ALPHAEXTRACT_FILTER)           += vf_extractplanes.o
OBJS-$(CONFIG_ALPHAMERGE_FILTER)             += vf_alphamerge.o
OBJS-$(CONFIG_ATADENOISE_FILTER)             += vf_atadenoise.o
OBJS-$(CONFIG_BBOX_FILTER)                   += bbox.o vf_bbox.o
OBJS-$(CONFIG_BLACKDETECT_FILTER)            += vf_blackdetect.o
OBJS-$(CONFIG_BLACKFRAME_FILTER)             += vf_blackframe.o
OBJS-$(CONFIG_BLEND_FILTER)                  += vf_blend.o dualinput.o framesync.o
OBJS-$(CONFIG_BOXBLUR_FILTER)                += vf_boxblur.o
OBJS-$(CONFIG_CHROMAKEY_FILTER)              += vf_chromakey.o
OBJS-$(CONFIG_CODECVIEW_FILTER)              += vf_codecview.o
OBJS-$(CONFIG_COLORBALANCE_FILTER)           += vf_colorbalance.o
OBJS-$(CONFIG_COLORCHANNELMIXER_FILTER)      += vf_colorchannelmixer.o
OBJS-$(CONFIG_COLORKEY_FILTER)               += vf_colorkey.o
OBJS-$(CONFIG_COLORLEVELS_FILTER)            += vf_colorlevels.o
OBJS-$(CONFIG_COLORMATRIX_FILTER)            += vf_colormatrix.o
OBJS-$(CONFIG_COPY_FILTER)                   += vf_copy.o
OBJS-$(CONFIG_COVER_RECT_FILTER)             += vf_cover_rect.o lavfutils.o
OBJS-$(CONFIG_CROP_FILTER)                   += vf_crop.o
OBJS-$(CONFIG_CROPDETECT_FILTER)             += vf_cropdetect.o
OBJS-$(CONFIG_CURVES_FILTER)                 += vf_curves.o
OBJS-$(CONFIG_DCTDNOIZ_FILTER)               += vf_dctdnoiz.o
OBJS-$(CONFIG_DEBAND_FILTER)                 += vf_deband.o
OBJS-$(CONFIG_DECIMATE_FILTER)               += vf_decimate.o
OBJS-$(CONFIG_DEFLATE_FILTER)                += vf_neighbor.o
OBJS-$(CONFIG_DEJUDDER_FILTER)               += vf_dejudder.o
OBJS-$(CONFIG_DELOGO_FILTER)                 += vf_delogo.o
OBJS-$(CONFIG_DESHAKE_FILTER)                += vf_deshake.o
OBJS-$(CONFIG_DETELECINE_FILTER)             += vf_detelecine.o
OBJS-$(CONFIG_DILATION_FILTER)               += vf_neighbor.o
OBJS-$(CONFIG_DISPLACE_FILTER)               += vf_displace.o framesync.o
OBJS-$(CONFIG_DRAWBOX_FILTER)                += vf_drawbox.o
OBJS-$(CONFIG_DRAWGRAPH_FILTER)              += f_drawgraph.o
OBJS-$(CONFIG_DRAWGRID_FILTER)               += vf_drawbox.o
OBJS-$(CONFIG_DRAWTEXT_FILTER)               += vf_drawtext.o
OBJS-$(CONFIG_ELBG_FILTER)                   += vf_elbg.o
OBJS-$(CONFIG_EDGEDETECT_FILTER)             += vf_edgedetect.o
OBJS-$(CONFIG_EQ_FILTER)                     += vf_eq.o
OBJS-$(CONFIG_EROSION_FILTER)                += vf_neighbor.o
OBJS-$(CONFIG_EXTRACTPLANES_FILTER)          += vf_extractplanes.o
OBJS-$(CONFIG_FADE_FILTER)                   += vf_fade.o
OBJS-$(CONFIG_FFTFILT_FILTER)                += vf_fftfilt.o
OBJS-$(CONFIG_FIELD_FILTER)                  += vf_field.o
OBJS-$(CONFIG_FIELDMATCH_FILTER)             += vf_fieldmatch.o
OBJS-$(CONFIG_FIELDORDER_FILTER)             += vf_fieldorder.o
OBJS-$(CONFIG_FIND_RECT_FILTER)              += vf_find_rect.o lavfutils.o
OBJS-$(CONFIG_FORMAT_FILTER)                 += vf_format.o
OBJS-$(CONFIG_FPS_FILTER)                    += vf_fps.o
OBJS-$(CONFIG_FRAMEPACK_FILTER)              += vf_framepack.o
OBJS-$(CONFIG_FRAMERATE_FILTER)              += vf_framerate.o
OBJS-$(CONFIG_FRAMESTEP_FILTER)              += vf_framestep.o
OBJS-$(CONFIG_FREI0R_FILTER)                 += vf_frei0r.o
OBJS-$(CONFIG_FSPP_FILTER)                   += vf_fspp.o
OBJS-$(CONFIG_GEQ_FILTER)                    += vf_geq.o
OBJS-$(CONFIG_GRADFUN_FILTER)                += vf_gradfun.o
OBJS-$(CONFIG_HALDCLUT_FILTER)               += vf_lut3d.o dualinput.o framesync.o
OBJS-$(CONFIG_HFLIP_FILTER)                  += vf_hflip.o
OBJS-$(CONFIG_HISTEQ_FILTER)                 += vf_histeq.o
OBJS-$(CONFIG_HISTOGRAM_FILTER)              += vf_histogram.o
OBJS-$(CONFIG_HQDN3D_FILTER)                 += vf_hqdn3d.o
OBJS-$(CONFIG_HQX_FILTER)                    += vf_hqx.o
OBJS-$(CONFIG_HSTACK_FILTER)                 += vf_stack.o framesync.o
OBJS-$(CONFIG_HUE_FILTER)                    += vf_hue.o
OBJS-$(CONFIG_IDET_FILTER)                   += vf_idet.o
OBJS-$(CONFIG_IL_FILTER)                     += vf_il.o
OBJS-$(CONFIG_INFLATE_FILTER)                += vf_neighbor.o
OBJS-$(CONFIG_INTERLACE_FILTER)              += vf_interlace.o
OBJS-$(CONFIG_INTERLEAVE_FILTER)             += f_interleave.o
OBJS-$(CONFIG_KERNDEINT_FILTER)              += vf_kerndeint.o
OBJS-$(CONFIG_LENSCORRECTION_FILTER)         += vf_lenscorrection.o
OBJS-$(CONFIG_LUT3D_FILTER)                  += vf_lut3d.o
OBJS-$(CONFIG_LUT_FILTER)                    += vf_lut.o
OBJS-$(CONFIG_LUTRGB_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_LUTYUV_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_MASKEDMERGE_FILTER)            += vf_maskedmerge.o framesync.o
OBJS-$(CONFIG_MCDEINT_FILTER)                += vf_mcdeint.o
OBJS-$(CONFIG_MERGEPLANES_FILTER)            += vf_mergeplanes.o framesync.o
OBJS-$(CONFIG_MPDECIMATE_FILTER)             += vf_mpdecimate.o
OBJS-$(CONFIG_NEGATE_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_NOFORMAT_FILTER)               += vf_format.o
OBJS-$(CONFIG_NOISE_FILTER)                  += vf_noise.o
OBJS-$(CONFIG_NULL_FILTER)                   += vf_null.o
OBJS-$(CONFIG_OCR_FILTER)                    += vf_ocr.o
OBJS-$(CONFIG_OCV_FILTER)                    += vf_libopencv.o
OBJS-$(CONFIG_OPENCL)                        += deshake_opencl.o unsharp_opencl.o
OBJS-$(CONFIG_OVERLAY_FILTER)                += vf_overlay.o dualinput.o framesync.o
OBJS-$(CONFIG_OWDENOISE_FILTER)              += vf_owdenoise.o
OBJS-$(CONFIG_PAD_FILTER)                    += vf_pad.o
OBJS-$(CONFIG_PALETTEGEN_FILTER)             += vf_palettegen.o
OBJS-$(CONFIG_PALETTEUSE_FILTER)             += vf_paletteuse.o dualinput.o framesync.o
OBJS-$(CONFIG_PERMS_FILTER)                  += f_perms.o
OBJS-$(CONFIG_PERSPECTIVE_FILTER)            += vf_perspective.o
OBJS-$(CONFIG_PHASE_FILTER)                  += vf_phase.o
OBJS-$(CONFIG_PIXDESCTEST_FILTER)            += vf_pixdesctest.o
OBJS-$(CONFIG_PP_FILTER)                     += vf_pp.o
OBJS-$(CONFIG_PP7_FILTER)                    += vf_pp7.o
OBJS-$(CONFIG_PSNR_FILTER)                   += vf_psnr.o dualinput.o framesync.o
OBJS-$(CONFIG_PULLUP_FILTER)                 += vf_pullup.o
OBJS-$(CONFIG_QP_FILTER)                     += vf_qp.o
OBJS-$(CONFIG_RANDOM_FILTER)                 += vf_random.o
OBJS-$(CONFIG_REALTIME_FILTER)               += f_realtime.o
OBJS-$(CONFIG_REMOVEGRAIN_FILTER)            += vf_removegrain.o
OBJS-$(CONFIG_REMOVELOGO_FILTER)             += bbox.o lswsutils.o lavfutils.o vf_removelogo.o
OBJS-$(CONFIG_REPEATFIELDS_FILTER)           += vf_repeatfields.o
OBJS-$(CONFIG_REVERSE_FILTER)                += f_reverse.o
OBJS-$(CONFIG_ROTATE_FILTER)                 += vf_rotate.o
OBJS-$(CONFIG_SEPARATEFIELDS_FILTER)         += vf_separatefields.o
OBJS-$(CONFIG_SAB_FILTER)                    += vf_sab.o
OBJS-$(CONFIG_SCALE_FILTER)                  += vf_scale.o
OBJS-$(CONFIG_SCALE2REF_FILTER)              += vf_scale.o
OBJS-$(CONFIG_SELECT_FILTER)                 += f_select.o
OBJS-$(CONFIG_SELECTIVECOLOR_FILTER)         += vf_selectivecolor.o
OBJS-$(CONFIG_SENDCMD_FILTER)                += f_sendcmd.o
OBJS-$(CONFIG_SETDAR_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_SETFIELD_FILTER)               += vf_setfield.o
OBJS-$(CONFIG_SETPTS_FILTER)                 += setpts.o
OBJS-$(CONFIG_SETSAR_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_SETTB_FILTER)                  += settb.o
OBJS-$(CONFIG_SHOWINFO_FILTER)               += vf_showinfo.o
OBJS-$(CONFIG_SHOWPALETTE_FILTER)            += vf_showpalette.o
OBJS-$(CONFIG_SHUFFLEFRAMES_FILTER)          += vf_shuffleframes.o
OBJS-$(CONFIG_SHUFFLEPLANES_FILTER)          += vf_shuffleplanes.o
OBJS-$(CONFIG_SIGNALSTATS_FILTER)            += vf_signalstats.o
OBJS-$(CONFIG_SMARTBLUR_FILTER)              += vf_smartblur.o
OBJS-$(CONFIG_SPLIT_FILTER)                  += split.o
OBJS-$(CONFIG_SPP_FILTER)                    += vf_spp.o
OBJS-$(CONFIG_SSIM_FILTER)                   += vf_ssim.o dualinput.o framesync.o
OBJS-$(CONFIG_STEREO3D_FILTER)               += vf_stereo3d.o
OBJS-$(CONFIG_SUBTITLES_FILTER)              += vf_subtitles.o
OBJS-$(CONFIG_SUPER2XSAI_FILTER)             += vf_super2xsai.o
OBJS-$(CONFIG_SWAPUV_FILTER)                 += vf_swapuv.o
OBJS-$(CONFIG_TBLEND_FILTER)                 += vf_blend.o dualinput.o framesync.o
OBJS-$(CONFIG_TELECINE_FILTER)               += vf_telecine.o
OBJS-$(CONFIG_THUMBNAIL_FILTER)              += vf_thumbnail.o
OBJS-$(CONFIG_TILE_FILTER)                   += vf_tile.o
OBJS-$(CONFIG_TINTERLACE_FILTER)             += vf_tinterlace.o
OBJS-$(CONFIG_TRANSPOSE_FILTER)              += vf_transpose.o
OBJS-$(CONFIG_TRIM_FILTER)                   += trim.o
OBJS-$(CONFIG_UNSHARP_FILTER)                += vf_unsharp.o
OBJS-$(CONFIG_USPP_FILTER)                   += vf_uspp.o
OBJS-$(CONFIG_VECTORSCOPE_FILTER)            += vf_vectorscope.o
OBJS-$(CONFIG_VFLIP_FILTER)                  += vf_vflip.o
OBJS-$(CONFIG_VIDSTABDETECT_FILTER)          += vidstabutils.o vf_vidstabdetect.o
OBJS-$(CONFIG_VIDSTABTRANSFORM_FILTER)       += vidstabutils.o vf_vidstabtransform.o
OBJS-$(CONFIG_VIGNETTE_FILTER)               += vf_vignette.o
OBJS-$(CONFIG_VSTACK_FILTER)                 += vf_stack.o framesync.o
OBJS-$(CONFIG_W3FDIF_FILTER)                 += vf_w3fdif.o
OBJS-$(CONFIG_WAVEFORM_FILTER)               += vf_waveform.o
OBJS-$(CONFIG_XBR_FILTER)                    += vf_xbr.o
OBJS-$(CONFIG_YADIF_FILTER)                  += vf_yadif.o
OBJS-$(CONFIG_ZMQ_FILTER)                    += f_zmq.o
OBJS-$(CONFIG_ZOOMPAN_FILTER)                += vf_zoompan.o
OBJS-$(CONFIG_ZSCALE_FILTER)                 += vf_zscale.o

OBJS-$(CONFIG_ALLRGB_FILTER)                 += vsrc_testsrc.o
OBJS-$(CONFIG_ALLYUV_FILTER)                 += vsrc_testsrc.o
OBJS-$(CONFIG_CELLAUTO_FILTER)               += vsrc_cellauto.o
OBJS-$(CONFIG_COLOR_FILTER)                  += vsrc_testsrc.o
OBJS-$(CONFIG_FREI0R_SRC_FILTER)             += vf_frei0r.o
OBJS-$(CONFIG_HALDCLUTSRC_FILTER)            += vsrc_testsrc.o
OBJS-$(CONFIG_LIFE_FILTER)                   += vsrc_life.o
OBJS-$(CONFIG_MANDELBROT_FILTER)             += vsrc_mandelbrot.o
OBJS-$(CONFIG_MPTESTSRC_FILTER)              += vsrc_mptestsrc.o
OBJS-$(CONFIG_NULLSRC_FILTER)                += vsrc_testsrc.o
OBJS-$(CONFIG_RGBTESTSRC_FILTER)             += vsrc_testsrc.o
OBJS-$(CONFIG_SMPTEBARS_FILTER)              += vsrc_testsrc.o
OBJS-$(CONFIG_SMPTEHDBARS_FILTER)            += vsrc_testsrc.o
OBJS-$(CONFIG_TESTSRC_FILTER)                += vsrc_testsrc.o
OBJS-$(CONFIG_TESTSRC2_FILTER)               += vsrc_testsrc.o

OBJS-$(CONFIG_NULLSINK_FILTER)               += vsink_nullsink.o

# multimedia filters
OBJS-$(CONFIG_ADRAWGRAPH_FILTER)             += f_drawgraph.o
OBJS-$(CONFIG_APHASEMETER_FILTER)            += avf_aphasemeter.o
OBJS-$(CONFIG_AVECTORSCOPE_FILTER)           += avf_avectorscope.o
OBJS-$(CONFIG_CONCAT_FILTER)                 += avf_concat.o
OBJS-$(CONFIG_SHOWCQT_FILTER)                += avf_showcqt.o lswsutils.o lavfutils.o
OBJS-$(CONFIG_SHOWFREQS_FILTER)              += avf_showfreqs.o
OBJS-$(CONFIG_SHOWSPECTRUM_FILTER)           += avf_showspectrum.o
OBJS-$(CONFIG_SHOWVOLUME_FILTER)             += avf_showvolume.o
OBJS-$(CONFIG_SHOWWAVES_FILTER)              += avf_showwaves.o
OBJS-$(CONFIG_SHOWWAVESPIC_FILTER)           += avf_showwaves.o

# multimedia sources
OBJS-$(CONFIG_AMOVIE_FILTER)                 += src_movie.o
OBJS-$(CONFIG_MOVIE_FILTER)                  += src_movie.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)                 += avfilterres.o

SKIPHEADERS-$(CONFIG_LIBVIDSTAB)             += vidstabutils.h
SKIPHEADERS-$(CONFIG_OPENCL)                 += opencl_internal.h deshake_opencl_kernel.h unsharp_opencl_kernel.h

OBJS-$(HAVE_THREADS)                         += pthread.o
OBJS-$(CONFIG_SHARED)                        += log2_tab.o

TOOLS     = graph2dot
TESTPROGS = drawutils filtfmts formats

TOOLS-$(CONFIG_LIBZMQ) += zmqsend

clean::
	$(RM) $(CLEANSUFFIXES:%=libavfilter/libmpcodecs/%)
