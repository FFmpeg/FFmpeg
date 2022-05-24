NAME = avfilter
DESC = FFmpeg audio/video filtering library

HEADERS = avfilter.h                                                    \
          buffersink.h                                                  \
          buffersrc.h                                                   \
          version.h                                                     \
          version_major.h                                               \

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
       framequeue.o                                                     \
       graphdump.o                                                      \
       graphparser.o                                                    \
       version.o                                                        \
       video.o                                                          \

OBJS-$(HAVE_LIBC_MSVCRT)                     += file_open.o
OBJS-$(HAVE_THREADS)                         += pthread.o

# subsystems
OBJS-$(CONFIG_QSVVPP)                        += qsvvpp.o
OBJS-$(CONFIG_SCENE_SAD)                     += scene_sad.o
OBJS-$(CONFIG_DNN)                           += dnn_filter_common.o
include $(SRC_PATH)/libavfilter/dnn/Makefile

# audio filters
OBJS-$(CONFIG_ABENCH_FILTER)                 += f_bench.o
OBJS-$(CONFIG_ACOMPRESSOR_FILTER)            += af_sidechaincompress.o
OBJS-$(CONFIG_ACONTRAST_FILTER)              += af_acontrast.o
OBJS-$(CONFIG_ACOPY_FILTER)                  += af_acopy.o
OBJS-$(CONFIG_ACROSSFADE_FILTER)             += af_afade.o
OBJS-$(CONFIG_ACROSSOVER_FILTER)             += af_acrossover.o
OBJS-$(CONFIG_ACRUSHER_FILTER)               += af_acrusher.o
OBJS-$(CONFIG_ACUE_FILTER)                   += f_cue.o
OBJS-$(CONFIG_ADECLICK_FILTER)               += af_adeclick.o
OBJS-$(CONFIG_ADECLIP_FILTER)                += af_adeclick.o
OBJS-$(CONFIG_ADECORRELATE_FILTER)           += af_adecorrelate.o
OBJS-$(CONFIG_ADELAY_FILTER)                 += af_adelay.o
OBJS-$(CONFIG_ADENORM_FILTER)                += af_adenorm.o
OBJS-$(CONFIG_ADERIVATIVE_FILTER)            += af_aderivative.o
OBJS-$(CONFIG_ADYNAMICEQUALIZER_FILTER)      += af_adynamicequalizer.o
OBJS-$(CONFIG_ADYNAMICSMOOTH_FILTER)         += af_adynamicsmooth.o
OBJS-$(CONFIG_AECHO_FILTER)                  += af_aecho.o
OBJS-$(CONFIG_AEMPHASIS_FILTER)              += af_aemphasis.o
OBJS-$(CONFIG_AEVAL_FILTER)                  += aeval.o
OBJS-$(CONFIG_AEXCITER_FILTER)               += af_aexciter.o
OBJS-$(CONFIG_AFADE_FILTER)                  += af_afade.o
OBJS-$(CONFIG_AFFTDN_FILTER)                 += af_afftdn.o
OBJS-$(CONFIG_AFFTFILT_FILTER)               += af_afftfilt.o
OBJS-$(CONFIG_AFIR_FILTER)                   += af_afir.o
OBJS-$(CONFIG_AFORMAT_FILTER)                += af_aformat.o
OBJS-$(CONFIG_AFREQSHIFT_FILTER)             += af_afreqshift.o
OBJS-$(CONFIG_AFWTDN_FILTER)                 += af_afwtdn.o
OBJS-$(CONFIG_AGATE_FILTER)                  += af_agate.o
OBJS-$(CONFIG_AIIR_FILTER)                   += af_aiir.o
OBJS-$(CONFIG_AINTEGRAL_FILTER)              += af_aderivative.o
OBJS-$(CONFIG_AINTERLEAVE_FILTER)            += f_interleave.o
OBJS-$(CONFIG_ALATENCY_FILTER)               += f_latency.o
OBJS-$(CONFIG_ALIMITER_FILTER)               += af_alimiter.o
OBJS-$(CONFIG_ALLPASS_FILTER)                += af_biquads.o
OBJS-$(CONFIG_ALOOP_FILTER)                  += f_loop.o
OBJS-$(CONFIG_AMERGE_FILTER)                 += af_amerge.o
OBJS-$(CONFIG_AMETADATA_FILTER)              += f_metadata.o
OBJS-$(CONFIG_AMIX_FILTER)                   += af_amix.o
OBJS-$(CONFIG_AMULTIPLY_FILTER)              += af_amultiply.o
OBJS-$(CONFIG_ANEQUALIZER_FILTER)            += af_anequalizer.o
OBJS-$(CONFIG_ANLMDN_FILTER)                 += af_anlmdn.o
OBJS-$(CONFIG_ANLMF_FILTER)                  += af_anlms.o
OBJS-$(CONFIG_ANLMS_FILTER)                  += af_anlms.o
OBJS-$(CONFIG_ANULL_FILTER)                  += af_anull.o
OBJS-$(CONFIG_APAD_FILTER)                   += af_apad.o
OBJS-$(CONFIG_APERMS_FILTER)                 += f_perms.o
OBJS-$(CONFIG_APHASER_FILTER)                += af_aphaser.o generate_wave_table.o
OBJS-$(CONFIG_APHASESHIFT_FILTER)            += af_afreqshift.o
OBJS-$(CONFIG_APSYCLIP_FILTER)               += af_apsyclip.o
OBJS-$(CONFIG_APULSATOR_FILTER)              += af_apulsator.o
OBJS-$(CONFIG_AREALTIME_FILTER)              += f_realtime.o
OBJS-$(CONFIG_ARESAMPLE_FILTER)              += af_aresample.o
OBJS-$(CONFIG_AREVERSE_FILTER)               += f_reverse.o
OBJS-$(CONFIG_ARNNDN_FILTER)                 += af_arnndn.o
OBJS-$(CONFIG_ASDR_FILTER)                   += af_asdr.o
OBJS-$(CONFIG_ASEGMENT_FILTER)               += f_segment.o
OBJS-$(CONFIG_ASELECT_FILTER)                += f_select.o
OBJS-$(CONFIG_ASENDCMD_FILTER)               += f_sendcmd.o
OBJS-$(CONFIG_ASETNSAMPLES_FILTER)           += af_asetnsamples.o
OBJS-$(CONFIG_ASETPTS_FILTER)                += setpts.o
OBJS-$(CONFIG_ASETRATE_FILTER)               += af_asetrate.o
OBJS-$(CONFIG_ASETTB_FILTER)                 += settb.o
OBJS-$(CONFIG_ASHOWINFO_FILTER)              += af_ashowinfo.o
OBJS-$(CONFIG_ASIDEDATA_FILTER)              += f_sidedata.o
OBJS-$(CONFIG_ASOFTCLIP_FILTER)              += af_asoftclip.o
OBJS-$(CONFIG_ASPECTRALSTATS_FILTER)         += af_aspectralstats.o
OBJS-$(CONFIG_ASPLIT_FILTER)                 += split.o
OBJS-$(CONFIG_ASR_FILTER)                    += af_asr.o
OBJS-$(CONFIG_ASTATS_FILTER)                 += af_astats.o
OBJS-$(CONFIG_ASTREAMSELECT_FILTER)          += f_streamselect.o framesync.o
OBJS-$(CONFIG_ASUBBOOST_FILTER)              += af_asubboost.o
OBJS-$(CONFIG_ASUBCUT_FILTER)                += af_asupercut.o
OBJS-$(CONFIG_ASUPERCUT_FILTER)              += af_asupercut.o
OBJS-$(CONFIG_ASUPERPASS_FILTER)             += af_asupercut.o
OBJS-$(CONFIG_ASUPERSTOP_FILTER)             += af_asupercut.o
OBJS-$(CONFIG_ATEMPO_FILTER)                 += af_atempo.o
OBJS-$(CONFIG_ATILT_FILTER)                  += af_atilt.o
OBJS-$(CONFIG_ATRIM_FILTER)                  += trim.o
OBJS-$(CONFIG_AXCORRELATE_FILTER)            += af_axcorrelate.o
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
OBJS-$(CONFIG_CROSSFEED_FILTER)              += af_crossfeed.o
OBJS-$(CONFIG_CRYSTALIZER_FILTER)            += af_crystalizer.o
OBJS-$(CONFIG_DCSHIFT_FILTER)                += af_dcshift.o
OBJS-$(CONFIG_DEESSER_FILTER)                += af_deesser.o
OBJS-$(CONFIG_DIALOGUENHANCE_FILTER)         += af_dialoguenhance.o
OBJS-$(CONFIG_DRMETER_FILTER)                += af_drmeter.o
OBJS-$(CONFIG_DYNAUDNORM_FILTER)             += af_dynaudnorm.o
OBJS-$(CONFIG_EARWAX_FILTER)                 += af_earwax.o
OBJS-$(CONFIG_EBUR128_FILTER)                += f_ebur128.o
OBJS-$(CONFIG_EQUALIZER_FILTER)              += af_biquads.o
OBJS-$(CONFIG_EXTRASTEREO_FILTER)            += af_extrastereo.o
OBJS-$(CONFIG_FIREQUALIZER_FILTER)           += af_firequalizer.o
OBJS-$(CONFIG_FLANGER_FILTER)                += af_flanger.o generate_wave_table.o
OBJS-$(CONFIG_HAAS_FILTER)                   += af_haas.o
OBJS-$(CONFIG_HDCD_FILTER)                   += af_hdcd.o
OBJS-$(CONFIG_HEADPHONE_FILTER)              += af_headphone.o
OBJS-$(CONFIG_HIGHPASS_FILTER)               += af_biquads.o
OBJS-$(CONFIG_HIGHSHELF_FILTER)              += af_biquads.o
OBJS-$(CONFIG_JOIN_FILTER)                   += af_join.o
OBJS-$(CONFIG_LADSPA_FILTER)                 += af_ladspa.o
OBJS-$(CONFIG_LOUDNORM_FILTER)               += af_loudnorm.o ebur128.o
OBJS-$(CONFIG_LOWPASS_FILTER)                += af_biquads.o
OBJS-$(CONFIG_LOWSHELF_FILTER)               += af_biquads.o
OBJS-$(CONFIG_LV2_FILTER)                    += af_lv2.o
OBJS-$(CONFIG_MCOMPAND_FILTER)               += af_mcompand.o
OBJS-$(CONFIG_PAN_FILTER)                    += af_pan.o
OBJS-$(CONFIG_REPLAYGAIN_FILTER)             += af_replaygain.o
OBJS-$(CONFIG_RUBBERBAND_FILTER)             += af_rubberband.o
OBJS-$(CONFIG_SIDECHAINCOMPRESS_FILTER)      += af_sidechaincompress.o
OBJS-$(CONFIG_SIDECHAINGATE_FILTER)          += af_agate.o
OBJS-$(CONFIG_SILENCEDETECT_FILTER)          += af_silencedetect.o
OBJS-$(CONFIG_SILENCEREMOVE_FILTER)          += af_silenceremove.o
OBJS-$(CONFIG_SOFALIZER_FILTER)              += af_sofalizer.o
OBJS-$(CONFIG_SPEECHNORM_FILTER)             += af_speechnorm.o
OBJS-$(CONFIG_STEREOTOOLS_FILTER)            += af_stereotools.o
OBJS-$(CONFIG_STEREOWIDEN_FILTER)            += af_stereowiden.o
OBJS-$(CONFIG_SUPEREQUALIZER_FILTER)         += af_superequalizer.o
OBJS-$(CONFIG_SURROUND_FILTER)               += af_surround.o
OBJS-$(CONFIG_TREBLE_FILTER)                 += af_biquads.o
OBJS-$(CONFIG_TREMOLO_FILTER)                += af_tremolo.o
OBJS-$(CONFIG_VIBRATO_FILTER)                += af_vibrato.o generate_wave_table.o
OBJS-$(CONFIG_VOLUME_FILTER)                 += af_volume.o
OBJS-$(CONFIG_VOLUMEDETECT_FILTER)           += af_volumedetect.o

OBJS-$(CONFIG_AEVALSRC_FILTER)               += aeval.o
OBJS-$(CONFIG_AFIRSRC_FILTER)                += asrc_afirsrc.o
OBJS-$(CONFIG_ANOISESRC_FILTER)              += asrc_anoisesrc.o
OBJS-$(CONFIG_ANULLSRC_FILTER)               += asrc_anullsrc.o
OBJS-$(CONFIG_FLITE_FILTER)                  += asrc_flite.o
OBJS-$(CONFIG_HILBERT_FILTER)                += asrc_hilbert.o
OBJS-$(CONFIG_SINC_FILTER)                   += asrc_sinc.o
OBJS-$(CONFIG_SINE_FILTER)                   += asrc_sine.o

OBJS-$(CONFIG_ANULLSINK_FILTER)              += asink_anullsink.o

# video filters
OBJS-$(CONFIG_ADDROI_FILTER)                 += vf_addroi.o
OBJS-$(CONFIG_ALPHAEXTRACT_FILTER)           += vf_extractplanes.o
OBJS-$(CONFIG_ALPHAMERGE_FILTER)             += vf_alphamerge.o framesync.o
OBJS-$(CONFIG_AMPLIFY_FILTER)                += vf_amplify.o
OBJS-$(CONFIG_ASS_FILTER)                    += vf_subtitles.o
OBJS-$(CONFIG_ATADENOISE_FILTER)             += vf_atadenoise.o
OBJS-$(CONFIG_AVGBLUR_FILTER)                += vf_avgblur.o
OBJS-$(CONFIG_AVGBLUR_OPENCL_FILTER)         += vf_avgblur_opencl.o opencl.o \
                                                opencl/avgblur.o boxblur.o
OBJS-$(CONFIG_AVGBLUR_VULKAN_FILTER)         += vf_avgblur_vulkan.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_BBOX_FILTER)                   += bbox.o vf_bbox.o
OBJS-$(CONFIG_BENCH_FILTER)                  += f_bench.o
OBJS-$(CONFIG_BILATERAL_FILTER)              += vf_bilateral.o
OBJS-$(CONFIG_BITPLANENOISE_FILTER)          += vf_bitplanenoise.o
OBJS-$(CONFIG_BLACKDETECT_FILTER)            += vf_blackdetect.o
OBJS-$(CONFIG_BLACKFRAME_FILTER)             += vf_blackframe.o
OBJS-$(CONFIG_BLEND_FILTER)                  += vf_blend.o framesync.o
OBJS-$(CONFIG_BLEND_VULKAN_FILTER)           += vf_blend_vulkan.o framesync.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_BLOCKDETECT_FILTER)            += vf_blockdetect.o
OBJS-$(CONFIG_BLURDETECT_FILTER)             += vf_blurdetect.o edge_common.o
OBJS-$(CONFIG_BM3D_FILTER)                   += vf_bm3d.o framesync.o
OBJS-$(CONFIG_BOXBLUR_FILTER)                += vf_boxblur.o boxblur.o
OBJS-$(CONFIG_BOXBLUR_OPENCL_FILTER)         += vf_avgblur_opencl.o opencl.o \
                                                opencl/avgblur.o boxblur.o
OBJS-$(CONFIG_BWDIF_FILTER)                  += vf_bwdif.o yadif_common.o
OBJS-$(CONFIG_CAS_FILTER)                    += vf_cas.o
OBJS-$(CONFIG_CHROMABER_VULKAN_FILTER)       += vf_chromaber_vulkan.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_CHROMAHOLD_FILTER)             += vf_chromakey.o
OBJS-$(CONFIG_CHROMAKEY_FILTER)              += vf_chromakey.o
OBJS-$(CONFIG_CHROMANR_FILTER)               += vf_chromanr.o
OBJS-$(CONFIG_CHROMASHIFT_FILTER)            += vf_chromashift.o
OBJS-$(CONFIG_CIESCOPE_FILTER)               += vf_ciescope.o
OBJS-$(CONFIG_CODECVIEW_FILTER)              += vf_codecview.o qp_table.o
OBJS-$(CONFIG_COLORBALANCE_FILTER)           += vf_colorbalance.o
OBJS-$(CONFIG_COLORCHANNELMIXER_FILTER)      += vf_colorchannelmixer.o
OBJS-$(CONFIG_COLORCONTRAST_FILTER)          += vf_colorcontrast.o
OBJS-$(CONFIG_COLORCORRECT_FILTER)           += vf_colorcorrect.o
OBJS-$(CONFIG_COLORIZE_FILTER)               += vf_colorize.o
OBJS-$(CONFIG_COLORKEY_FILTER)               += vf_colorkey.o
OBJS-$(CONFIG_COLORKEY_OPENCL_FILTER)        += vf_colorkey_opencl.o opencl.o \
                                                opencl/colorkey.o
OBJS-$(CONFIG_COLORHOLD_FILTER)              += vf_colorkey.o
OBJS-$(CONFIG_COLORLEVELS_FILTER)            += vf_colorlevels.o
OBJS-$(CONFIG_COLORMAP_FILTER)               += vf_colormap.o
OBJS-$(CONFIG_COLORMATRIX_FILTER)            += vf_colormatrix.o
OBJS-$(CONFIG_COLORSPACE_FILTER)             += vf_colorspace.o colorspace.o colorspacedsp.o
OBJS-$(CONFIG_COLORTEMPERATURE_FILTER)       += vf_colortemperature.o
OBJS-$(CONFIG_CONVOLUTION_FILTER)            += vf_convolution.o
OBJS-$(CONFIG_CONVOLUTION_OPENCL_FILTER)     += vf_convolution_opencl.o opencl.o \
                                                opencl/convolution.o
OBJS-$(CONFIG_CONVOLVE_FILTER)               += vf_convolve.o framesync.o
OBJS-$(CONFIG_COPY_FILTER)                   += vf_copy.o
OBJS-$(CONFIG_COREIMAGE_FILTER)              += vf_coreimage.o
OBJS-$(CONFIG_COVER_RECT_FILTER)             += vf_cover_rect.o lavfutils.o
OBJS-$(CONFIG_CROP_FILTER)                   += vf_crop.o
OBJS-$(CONFIG_CROPDETECT_FILTER)             += vf_cropdetect.o
OBJS-$(CONFIG_CUE_FILTER)                    += f_cue.o
OBJS-$(CONFIG_CURVES_FILTER)                 += vf_curves.o
OBJS-$(CONFIG_DATASCOPE_FILTER)              += vf_datascope.o
OBJS-$(CONFIG_DBLUR_FILTER)                  += vf_dblur.o
OBJS-$(CONFIG_DCTDNOIZ_FILTER)               += vf_dctdnoiz.o
OBJS-$(CONFIG_DEBAND_FILTER)                 += vf_deband.o
OBJS-$(CONFIG_DEBLOCK_FILTER)                += vf_deblock.o
OBJS-$(CONFIG_DECIMATE_FILTER)               += vf_decimate.o
OBJS-$(CONFIG_DERAIN_FILTER)                 += vf_derain.o
OBJS-$(CONFIG_DECONVOLVE_FILTER)             += vf_convolve.o framesync.o
OBJS-$(CONFIG_DEDOT_FILTER)                  += vf_dedot.o
OBJS-$(CONFIG_DEFLATE_FILTER)                += vf_neighbor.o
OBJS-$(CONFIG_DEFLICKER_FILTER)              += vf_deflicker.o
OBJS-$(CONFIG_DEINTERLACE_QSV_FILTER)        += vf_deinterlace_qsv.o
OBJS-$(CONFIG_DEINTERLACE_VAAPI_FILTER)      += vf_deinterlace_vaapi.o vaapi_vpp.o
OBJS-$(CONFIG_DEJUDDER_FILTER)               += vf_dejudder.o
OBJS-$(CONFIG_DELOGO_FILTER)                 += vf_delogo.o
OBJS-$(CONFIG_DENOISE_VAAPI_FILTER)          += vf_misc_vaapi.o vaapi_vpp.o
OBJS-$(CONFIG_DESHAKE_OPENCL_FILTER)        += vf_deshake_opencl.o opencl.o \
                                                opencl/deshake.o transform.o
OBJS-$(CONFIG_DESHAKE_FILTER)                += vf_deshake.o transform.o
OBJS-$(CONFIG_DESPILL_FILTER)                += vf_despill.o
OBJS-$(CONFIG_DETELECINE_FILTER)             += vf_detelecine.o
OBJS-$(CONFIG_DILATION_FILTER)               += vf_neighbor.o
OBJS-$(CONFIG_DILATION_OPENCL_FILTER)        += vf_neighbor_opencl.o opencl.o \
                                                opencl/neighbor.o
OBJS-$(CONFIG_DISPLACE_FILTER)               += vf_displace.o framesync.o
OBJS-$(CONFIG_DNN_CLASSIFY_FILTER)           += vf_dnn_classify.o
OBJS-$(CONFIG_DNN_DETECT_FILTER)             += vf_dnn_detect.o
OBJS-$(CONFIG_DNN_PROCESSING_FILTER)         += vf_dnn_processing.o
OBJS-$(CONFIG_DOUBLEWEAVE_FILTER)            += vf_weave.o
OBJS-$(CONFIG_DRAWBOX_FILTER)                += vf_drawbox.o
OBJS-$(CONFIG_DRAWGRAPH_FILTER)              += f_drawgraph.o
OBJS-$(CONFIG_DRAWGRID_FILTER)               += vf_drawbox.o
OBJS-$(CONFIG_DRAWTEXT_FILTER)               += vf_drawtext.o
OBJS-$(CONFIG_EDGEDETECT_FILTER)             += vf_edgedetect.o edge_common.o
OBJS-$(CONFIG_ELBG_FILTER)                   += vf_elbg.o
OBJS-$(CONFIG_ENTROPY_FILTER)                += vf_entropy.o
OBJS-$(CONFIG_EPX_FILTER)                    += vf_epx.o
OBJS-$(CONFIG_EQ_FILTER)                     += vf_eq.o
OBJS-$(CONFIG_EROSION_FILTER)                += vf_neighbor.o
OBJS-$(CONFIG_EROSION_OPENCL_FILTER)         += vf_neighbor_opencl.o opencl.o \
                                                opencl/neighbor.o
OBJS-$(CONFIG_ESTDIF_FILTER)                 += vf_estdif.o
OBJS-$(CONFIG_EXPOSURE_FILTER)               += vf_exposure.o
OBJS-$(CONFIG_EXTRACTPLANES_FILTER)          += vf_extractplanes.o
OBJS-$(CONFIG_FADE_FILTER)                   += vf_fade.o
OBJS-$(CONFIG_FEEDBACK_FILTER)               += vf_feedback.o
OBJS-$(CONFIG_FFTDNOIZ_FILTER)               += vf_fftdnoiz.o
OBJS-$(CONFIG_FFTFILT_FILTER)                += vf_fftfilt.o
OBJS-$(CONFIG_FIELD_FILTER)                  += vf_field.o
OBJS-$(CONFIG_FIELDHINT_FILTER)              += vf_fieldhint.o
OBJS-$(CONFIG_FIELDMATCH_FILTER)             += vf_fieldmatch.o
OBJS-$(CONFIG_FIELDORDER_FILTER)             += vf_fieldorder.o
OBJS-$(CONFIG_FILLBORDERS_FILTER)            += vf_fillborders.o
OBJS-$(CONFIG_FIND_RECT_FILTER)              += vf_find_rect.o lavfutils.o
OBJS-$(CONFIG_FLOODFILL_FILTER)              += vf_floodfill.o
OBJS-$(CONFIG_FORMAT_FILTER)                 += vf_format.o
OBJS-$(CONFIG_FPS_FILTER)                    += vf_fps.o
OBJS-$(CONFIG_FRAMEPACK_FILTER)              += vf_framepack.o
OBJS-$(CONFIG_FRAMERATE_FILTER)              += vf_framerate.o
OBJS-$(CONFIG_FRAMESTEP_FILTER)              += vf_framestep.o
OBJS-$(CONFIG_FREEZEDETECT_FILTER)           += vf_freezedetect.o
OBJS-$(CONFIG_FREEZEFRAMES_FILTER)           += vf_freezeframes.o
OBJS-$(CONFIG_FREI0R_FILTER)                 += vf_frei0r.o
OBJS-$(CONFIG_FSPP_FILTER)                   += vf_fspp.o qp_table.o
OBJS-$(CONFIG_GBLUR_FILTER)                  += vf_gblur.o
OBJS-$(CONFIG_GBLUR_VULKAN_FILTER)           += vf_gblur_vulkan.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_GEQ_FILTER)                    += vf_geq.o
OBJS-$(CONFIG_GRADFUN_FILTER)                += vf_gradfun.o
OBJS-$(CONFIG_GRAPHMONITOR_FILTER)           += f_graphmonitor.o
OBJS-$(CONFIG_GRAYWORLD_FILTER)              += vf_grayworld.o
OBJS-$(CONFIG_GREYEDGE_FILTER)               += vf_colorconstancy.o
OBJS-$(CONFIG_GUIDED_FILTER)                 += vf_guided.o
OBJS-$(CONFIG_HALDCLUT_FILTER)               += vf_lut3d.o framesync.o
OBJS-$(CONFIG_HFLIP_FILTER)                  += vf_hflip.o
OBJS-$(CONFIG_HFLIP_VULKAN_FILTER)           += vf_flip_vulkan.o vulkan.o
OBJS-$(CONFIG_HISTEQ_FILTER)                 += vf_histeq.o
OBJS-$(CONFIG_HISTOGRAM_FILTER)              += vf_histogram.o
OBJS-$(CONFIG_HQDN3D_FILTER)                 += vf_hqdn3d.o
OBJS-$(CONFIG_HQX_FILTER)                    += vf_hqx.o
OBJS-$(CONFIG_HSTACK_FILTER)                 += vf_stack.o framesync.o
OBJS-$(CONFIG_HSVHOLD_FILTER)                += vf_hsvkey.o
OBJS-$(CONFIG_HSVKEY_FILTER)                 += vf_hsvkey.o
OBJS-$(CONFIG_HUE_FILTER)                    += vf_hue.o
OBJS-$(CONFIG_HUESATURATION_FILTER)          += vf_huesaturation.o
OBJS-$(CONFIG_HWDOWNLOAD_FILTER)             += vf_hwdownload.o
OBJS-$(CONFIG_HWMAP_FILTER)                  += vf_hwmap.o
OBJS-$(CONFIG_HWUPLOAD_CUDA_FILTER)          += vf_hwupload_cuda.o
OBJS-$(CONFIG_HWUPLOAD_FILTER)               += vf_hwupload.o
OBJS-$(CONFIG_HYSTERESIS_FILTER)             += vf_hysteresis.o framesync.o
OBJS-$(CONFIG_ICCDETECT_FILTER)              += vf_iccdetect.o fflcms2.o colorspace.o
OBJS-$(CONFIG_ICCGEN_FILTER)                 += vf_iccgen.o fflcms2.o colorspace.o
OBJS-$(CONFIG_IDENTITY_FILTER)               += vf_identity.o
OBJS-$(CONFIG_IDET_FILTER)                   += vf_idet.o
OBJS-$(CONFIG_IL_FILTER)                     += vf_il.o
OBJS-$(CONFIG_INFLATE_FILTER)                += vf_neighbor.o
OBJS-$(CONFIG_INTERLACE_FILTER)              += vf_tinterlace.o
OBJS-$(CONFIG_INTERLEAVE_FILTER)             += f_interleave.o
OBJS-$(CONFIG_KERNDEINT_FILTER)              += vf_kerndeint.o
OBJS-$(CONFIG_KIRSCH_FILTER)                 += vf_convolution.o
OBJS-$(CONFIG_LAGFUN_FILTER)                 += vf_lagfun.o
OBJS-$(CONFIG_LATENCY_FILTER)                += f_latency.o
OBJS-$(CONFIG_LENSCORRECTION_FILTER)         += vf_lenscorrection.o
OBJS-$(CONFIG_LENSFUN_FILTER)                += vf_lensfun.o
OBJS-$(CONFIG_LIBPLACEBO_FILTER)             += vf_libplacebo.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_LIBVMAF_FILTER)                += vf_libvmaf.o framesync.o
OBJS-$(CONFIG_LIMITDIFF_FILTER)              += vf_limitdiff.o framesync.o
OBJS-$(CONFIG_LIMITER_FILTER)                += vf_limiter.o
OBJS-$(CONFIG_LOOP_FILTER)                   += f_loop.o
OBJS-$(CONFIG_LUMAKEY_FILTER)                += vf_lumakey.o
OBJS-$(CONFIG_LUT1D_FILTER)                  += vf_lut3d.o
OBJS-$(CONFIG_LUT_FILTER)                    += vf_lut.o
OBJS-$(CONFIG_LUT2_FILTER)                   += vf_lut2.o framesync.o
OBJS-$(CONFIG_LUT3D_FILTER)                  += vf_lut3d.o framesync.o
OBJS-$(CONFIG_LUTRGB_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_LUTYUV_FILTER)                 += vf_lut.o
OBJS-$(CONFIG_MASKEDCLAMP_FILTER)            += vf_maskedclamp.o framesync.o
OBJS-$(CONFIG_MASKEDMAX_FILTER)              += vf_maskedminmax.o framesync.o
OBJS-$(CONFIG_MASKEDMERGE_FILTER)            += vf_maskedmerge.o framesync.o
OBJS-$(CONFIG_MASKEDMIN_FILTER)              += vf_maskedminmax.o framesync.o
OBJS-$(CONFIG_MASKEDTHRESHOLD_FILTER)        += vf_maskedthreshold.o framesync.o
OBJS-$(CONFIG_MASKFUN_FILTER)                += vf_maskfun.o
OBJS-$(CONFIG_MCDEINT_FILTER)                += vf_mcdeint.o
OBJS-$(CONFIG_MEDIAN_FILTER)                 += vf_median.o
OBJS-$(CONFIG_MERGEPLANES_FILTER)            += vf_mergeplanes.o framesync.o
OBJS-$(CONFIG_MESTIMATE_FILTER)              += vf_mestimate.o motion_estimation.o
OBJS-$(CONFIG_METADATA_FILTER)               += f_metadata.o
OBJS-$(CONFIG_MIDEQUALIZER_FILTER)           += vf_midequalizer.o framesync.o
OBJS-$(CONFIG_MINTERPOLATE_FILTER)           += vf_minterpolate.o motion_estimation.o
OBJS-$(CONFIG_MIX_FILTER)                    += vf_mix.o framesync.o
OBJS-$(CONFIG_MONOCHROME_FILTER)             += vf_monochrome.o
OBJS-$(CONFIG_MORPHO_FILTER)                 += vf_morpho.o
OBJS-$(CONFIG_MPDECIMATE_FILTER)             += vf_mpdecimate.o
OBJS-$(CONFIG_MULTIPLY_FILTER)               += vf_multiply.o
OBJS-$(CONFIG_NEGATE_FILTER)                 += vf_negate.o
OBJS-$(CONFIG_NLMEANS_FILTER)                += vf_nlmeans.o
OBJS-$(CONFIG_NLMEANS_OPENCL_FILTER)         += vf_nlmeans_opencl.o opencl.o opencl/nlmeans.o
OBJS-$(CONFIG_NNEDI_FILTER)                  += vf_nnedi.o
OBJS-$(CONFIG_NOFORMAT_FILTER)               += vf_format.o
OBJS-$(CONFIG_NOISE_FILTER)                  += vf_noise.o
OBJS-$(CONFIG_NORMALIZE_FILTER)              += vf_normalize.o
OBJS-$(CONFIG_NULL_FILTER)                   += vf_null.o
OBJS-$(CONFIG_OCR_FILTER)                    += vf_ocr.o
OBJS-$(CONFIG_OCV_FILTER)                    += vf_libopencv.o
OBJS-$(CONFIG_OSCILLOSCOPE_FILTER)           += vf_datascope.o
OBJS-$(CONFIG_OVERLAY_FILTER)                += vf_overlay.o framesync.o
OBJS-$(CONFIG_OVERLAY_CUDA_FILTER)           += vf_overlay_cuda.o framesync.o vf_overlay_cuda.ptx.o \
                                                cuda/load_helper.o
OBJS-$(CONFIG_OVERLAY_OPENCL_FILTER)         += vf_overlay_opencl.o opencl.o \
                                                opencl/overlay.o framesync.o
OBJS-$(CONFIG_OVERLAY_QSV_FILTER)            += vf_overlay_qsv.o framesync.o
OBJS-$(CONFIG_OVERLAY_VAAPI_FILTER)          += vf_overlay_vaapi.o framesync.o vaapi_vpp.o
OBJS-$(CONFIG_OVERLAY_VULKAN_FILTER)         += vf_overlay_vulkan.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_OWDENOISE_FILTER)              += vf_owdenoise.o
OBJS-$(CONFIG_PAD_FILTER)                    += vf_pad.o
OBJS-$(CONFIG_PAD_OPENCL_FILTER)             += vf_pad_opencl.o opencl.o opencl/pad.o
OBJS-$(CONFIG_PALETTEGEN_FILTER)             += vf_palettegen.o
OBJS-$(CONFIG_PALETTEUSE_FILTER)             += vf_paletteuse.o framesync.o
OBJS-$(CONFIG_PERMS_FILTER)                  += f_perms.o
OBJS-$(CONFIG_PERSPECTIVE_FILTER)            += vf_perspective.o
OBJS-$(CONFIG_PHASE_FILTER)                  += vf_phase.o
OBJS-$(CONFIG_PHOTOSENSITIVITY_FILTER)       += vf_photosensitivity.o
OBJS-$(CONFIG_PIXDESCTEST_FILTER)            += vf_pixdesctest.o
OBJS-$(CONFIG_PIXELIZE_FILTER)               += vf_pixelize.o
OBJS-$(CONFIG_PIXSCOPE_FILTER)               += vf_datascope.o
OBJS-$(CONFIG_PP_FILTER)                     += vf_pp.o qp_table.o
OBJS-$(CONFIG_PP7_FILTER)                    += vf_pp7.o qp_table.o
OBJS-$(CONFIG_PREMULTIPLY_FILTER)            += vf_premultiply.o framesync.o
OBJS-$(CONFIG_PREWITT_FILTER)                += vf_convolution.o
OBJS-$(CONFIG_PREWITT_OPENCL_FILTER)         += vf_convolution_opencl.o opencl.o \
                                                opencl/convolution.o
OBJS-$(CONFIG_PROCAMP_VAAPI_FILTER)          += vf_procamp_vaapi.o vaapi_vpp.o
OBJS-$(CONFIG_PROGRAM_OPENCL_FILTER)         += vf_program_opencl.o opencl.o framesync.o
OBJS-$(CONFIG_PSEUDOCOLOR_FILTER)            += vf_pseudocolor.o
OBJS-$(CONFIG_PSNR_FILTER)                   += vf_psnr.o framesync.o
OBJS-$(CONFIG_PULLUP_FILTER)                 += vf_pullup.o
OBJS-$(CONFIG_QP_FILTER)                     += vf_qp.o
OBJS-$(CONFIG_RANDOM_FILTER)                 += vf_random.o
OBJS-$(CONFIG_READEIA608_FILTER)             += vf_readeia608.o
OBJS-$(CONFIG_READVITC_FILTER)               += vf_readvitc.o
OBJS-$(CONFIG_REALTIME_FILTER)               += f_realtime.o
OBJS-$(CONFIG_REMAP_FILTER)                  += vf_remap.o framesync.o
OBJS-$(CONFIG_REMOVEGRAIN_FILTER)            += vf_removegrain.o
OBJS-$(CONFIG_REMOVELOGO_FILTER)             += bbox.o lswsutils.o lavfutils.o vf_removelogo.o
OBJS-$(CONFIG_REPEATFIELDS_FILTER)           += vf_repeatfields.o
OBJS-$(CONFIG_REVERSE_FILTER)                += f_reverse.o
OBJS-$(CONFIG_RGBASHIFT_FILTER)              += vf_chromashift.o
OBJS-$(CONFIG_ROBERTS_FILTER)                += vf_convolution.o
OBJS-$(CONFIG_ROBERTS_OPENCL_FILTER)         += vf_convolution_opencl.o opencl.o \
                                                opencl/convolution.o
OBJS-$(CONFIG_ROTATE_FILTER)                 += vf_rotate.o
OBJS-$(CONFIG_SAB_FILTER)                    += vf_sab.o
OBJS-$(CONFIG_SCALE_FILTER)                  += vf_scale.o scale_eval.o
OBJS-$(CONFIG_SCALE_CUDA_FILTER)             += vf_scale_cuda.o scale_eval.o \
                                                vf_scale_cuda.ptx.o cuda/load_helper.o
OBJS-$(CONFIG_SCALE_NPP_FILTER)              += vf_scale_npp.o scale_eval.o
OBJS-$(CONFIG_SCALE_QSV_FILTER)              += vf_scale_qsv.o
OBJS-$(CONFIG_SCALE_VAAPI_FILTER)            += vf_scale_vaapi.o scale_eval.o vaapi_vpp.o
OBJS-$(CONFIG_SCALE_VULKAN_FILTER)           += vf_scale_vulkan.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_SCALE2REF_FILTER)              += vf_scale.o scale_eval.o
OBJS-$(CONFIG_SCALE2REF_NPP_FILTER)          += vf_scale_npp.o scale_eval.o
OBJS-$(CONFIG_SCDET_FILTER)                  += vf_scdet.o
OBJS-$(CONFIG_SCHARR_FILTER)                 += vf_convolution.o
OBJS-$(CONFIG_SCROLL_FILTER)                 += vf_scroll.o
OBJS-$(CONFIG_SEGMENT_FILTER)                += f_segment.o
OBJS-$(CONFIG_SELECT_FILTER)                 += f_select.o
OBJS-$(CONFIG_SELECTIVECOLOR_FILTER)         += vf_selectivecolor.o
OBJS-$(CONFIG_SENDCMD_FILTER)                += f_sendcmd.o
OBJS-$(CONFIG_SEPARATEFIELDS_FILTER)         += vf_separatefields.o
OBJS-$(CONFIG_SETDAR_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_SETFIELD_FILTER)               += vf_setparams.o
OBJS-$(CONFIG_SETPARAMS_FILTER)              += vf_setparams.o
OBJS-$(CONFIG_SETPTS_FILTER)                 += setpts.o
OBJS-$(CONFIG_SETRANGE_FILTER)               += vf_setparams.o
OBJS-$(CONFIG_SETSAR_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_SETTB_FILTER)                  += settb.o
OBJS-$(CONFIG_SHARPEN_NPP_FILTER)            += vf_sharpen_npp.o
OBJS-$(CONFIG_SHARPNESS_VAAPI_FILTER)        += vf_misc_vaapi.o vaapi_vpp.o
OBJS-$(CONFIG_SHEAR_FILTER)                  += vf_shear.o
OBJS-$(CONFIG_SHOWINFO_FILTER)               += vf_showinfo.o
OBJS-$(CONFIG_SHOWPALETTE_FILTER)            += vf_showpalette.o
OBJS-$(CONFIG_SHUFFLEFRAMES_FILTER)          += vf_shuffleframes.o
OBJS-$(CONFIG_SHUFFLEPIXELS_FILTER)          += vf_shufflepixels.o
OBJS-$(CONFIG_SHUFFLEPLANES_FILTER)          += vf_shuffleplanes.o
OBJS-$(CONFIG_SIDEDATA_FILTER)               += f_sidedata.o
OBJS-$(CONFIG_SIGNALSTATS_FILTER)            += vf_signalstats.o
OBJS-$(CONFIG_SIGNATURE_FILTER)              += vf_signature.o
OBJS-$(CONFIG_SMARTBLUR_FILTER)              += vf_smartblur.o
OBJS-$(CONFIG_SOBEL_FILTER)                  += vf_convolution.o
OBJS-$(CONFIG_SOBEL_OPENCL_FILTER)           += vf_convolution_opencl.o opencl.o \
                                                opencl/convolution.o
OBJS-$(CONFIG_SITI_FILTER)                   += vf_siti.o
OBJS-$(CONFIG_SPLIT_FILTER)                  += split.o
OBJS-$(CONFIG_SPP_FILTER)                    += vf_spp.o qp_table.o
OBJS-$(CONFIG_SR_FILTER)                     += vf_sr.o
OBJS-$(CONFIG_SSIM_FILTER)                   += vf_ssim.o framesync.o
OBJS-$(CONFIG_STEREO3D_FILTER)               += vf_stereo3d.o
OBJS-$(CONFIG_STREAMSELECT_FILTER)           += f_streamselect.o framesync.o
OBJS-$(CONFIG_SUBTITLES_FILTER)              += vf_subtitles.o
OBJS-$(CONFIG_SUPER2XSAI_FILTER)             += vf_super2xsai.o
OBJS-$(CONFIG_SWAPRECT_FILTER)               += vf_swaprect.o
OBJS-$(CONFIG_SWAPUV_FILTER)                 += vf_swapuv.o
OBJS-$(CONFIG_TBLEND_FILTER)                 += vf_blend.o framesync.o
OBJS-$(CONFIG_TELECINE_FILTER)               += vf_telecine.o
OBJS-$(CONFIG_THISTOGRAM_FILTER)             += vf_histogram.o
OBJS-$(CONFIG_THRESHOLD_FILTER)              += vf_threshold.o framesync.o
OBJS-$(CONFIG_THUMBNAIL_FILTER)              += vf_thumbnail.o
OBJS-$(CONFIG_THUMBNAIL_CUDA_FILTER)         += vf_thumbnail_cuda.o vf_thumbnail_cuda.ptx.o \
                                                cuda/load_helper.o
OBJS-$(CONFIG_TILE_FILTER)                   += vf_tile.o
OBJS-$(CONFIG_TINTERLACE_FILTER)             += vf_tinterlace.o
OBJS-$(CONFIG_TLUT2_FILTER)                  += vf_lut2.o framesync.o
OBJS-$(CONFIG_TMEDIAN_FILTER)                += vf_xmedian.o framesync.o
OBJS-$(CONFIG_TMIDEQUALIZER_FILTER)          += vf_tmidequalizer.o
OBJS-$(CONFIG_TMIX_FILTER)                   += vf_mix.o framesync.o
OBJS-$(CONFIG_TONEMAP_FILTER)                += vf_tonemap.o colorspace.o
OBJS-$(CONFIG_TONEMAP_OPENCL_FILTER)         += vf_tonemap_opencl.o colorspace.o opencl.o \
                                                opencl/tonemap.o opencl/colorspace_common.o
OBJS-$(CONFIG_TONEMAP_VAAPI_FILTER)          += vf_tonemap_vaapi.o vaapi_vpp.o
OBJS-$(CONFIG_TPAD_FILTER)                   += vf_tpad.o
OBJS-$(CONFIG_TRANSPOSE_FILTER)              += vf_transpose.o
OBJS-$(CONFIG_TRANSPOSE_NPP_FILTER)          += vf_transpose_npp.o
OBJS-$(CONFIG_TRANSPOSE_OPENCL_FILTER)       += vf_transpose_opencl.o opencl.o opencl/transpose.o
OBJS-$(CONFIG_TRANSPOSE_VAAPI_FILTER)        += vf_transpose_vaapi.o vaapi_vpp.o
OBJS-$(CONFIG_TRANSPOSE_VULKAN_FILTER)       += vf_transpose_vulkan.o vulkan.o vulkan_filter.o
OBJS-$(CONFIG_TRIM_FILTER)                   += trim.o
OBJS-$(CONFIG_UNPREMULTIPLY_FILTER)          += vf_premultiply.o framesync.o
OBJS-$(CONFIG_UNSHARP_FILTER)                += vf_unsharp.o
OBJS-$(CONFIG_UNSHARP_OPENCL_FILTER)         += vf_unsharp_opencl.o opencl.o \
                                                opencl/unsharp.o
OBJS-$(CONFIG_UNTILE_FILTER)                 += vf_untile.o
OBJS-$(CONFIG_USPP_FILTER)                   += vf_uspp.o qp_table.o
OBJS-$(CONFIG_V360_FILTER)                   += vf_v360.o
OBJS-$(CONFIG_VAGUEDENOISER_FILTER)          += vf_vaguedenoiser.o
OBJS-$(CONFIG_VARBLUR_FILTER)                += vf_varblur.o framesync.o
OBJS-$(CONFIG_VECTORSCOPE_FILTER)            += vf_vectorscope.o
OBJS-$(CONFIG_VFLIP_FILTER)                  += vf_vflip.o
OBJS-$(CONFIG_VFLIP_VULKAN_FILTER)           += vf_flip_vulkan.o vulkan.o
OBJS-$(CONFIG_VFRDET_FILTER)                 += vf_vfrdet.o
OBJS-$(CONFIG_VIBRANCE_FILTER)               += vf_vibrance.o
OBJS-$(CONFIG_VIDSTABDETECT_FILTER)          += vidstabutils.o vf_vidstabdetect.o
OBJS-$(CONFIG_VIDSTABTRANSFORM_FILTER)       += vidstabutils.o vf_vidstabtransform.o
OBJS-$(CONFIG_VIF_FILTER)                    += vf_vif.o framesync.o
OBJS-$(CONFIG_VIGNETTE_FILTER)               += vf_vignette.o
OBJS-$(CONFIG_VMAFMOTION_FILTER)             += vf_vmafmotion.o framesync.o
OBJS-$(CONFIG_VPP_QSV_FILTER)                += vf_vpp_qsv.o
OBJS-$(CONFIG_VSTACK_FILTER)                 += vf_stack.o framesync.o
OBJS-$(CONFIG_W3FDIF_FILTER)                 += vf_w3fdif.o
OBJS-$(CONFIG_WAVEFORM_FILTER)               += vf_waveform.o
OBJS-$(CONFIG_WEAVE_FILTER)                  += vf_weave.o
OBJS-$(CONFIG_XBR_FILTER)                    += vf_xbr.o
OBJS-$(CONFIG_XCORRELATE_FILTER)             += vf_convolve.o framesync.o
OBJS-$(CONFIG_XFADE_FILTER)                  += vf_xfade.o
OBJS-$(CONFIG_XFADE_OPENCL_FILTER)           += vf_xfade_opencl.o opencl.o opencl/xfade.o
OBJS-$(CONFIG_XMEDIAN_FILTER)                += vf_xmedian.o framesync.o
OBJS-$(CONFIG_XSTACK_FILTER)                 += vf_stack.o framesync.o
OBJS-$(CONFIG_YADIF_FILTER)                  += vf_yadif.o yadif_common.o
OBJS-$(CONFIG_YADIF_CUDA_FILTER)             += vf_yadif_cuda.o vf_yadif_cuda.ptx.o \
                                                yadif_common.o cuda/load_helper.o
OBJS-$(CONFIG_YADIF_VIDEOTOOLBOX_FILTER)     += vf_yadif_videotoolbox.o \
                                                metal/vf_yadif_videotoolbox.metallib.o \
                                                metal/utils.o \
                                                yadif_common.o
OBJS-$(CONFIG_YAEPBLUR_FILTER)               += vf_yaepblur.o
OBJS-$(CONFIG_ZMQ_FILTER)                    += f_zmq.o
OBJS-$(CONFIG_ZOOMPAN_FILTER)                += vf_zoompan.o
OBJS-$(CONFIG_ZSCALE_FILTER)                 += vf_zscale.o

OBJS-$(CONFIG_ALLRGB_FILTER)                 += vsrc_testsrc.o
OBJS-$(CONFIG_ALLYUV_FILTER)                 += vsrc_testsrc.o
OBJS-$(CONFIG_CELLAUTO_FILTER)               += vsrc_cellauto.o
OBJS-$(CONFIG_COLOR_FILTER)                  += vsrc_testsrc.o
OBJS-$(CONFIG_COLORCHART_FILTER)             += vsrc_testsrc.o
OBJS-$(CONFIG_COLORSPECTRUM_FILTER)          += vsrc_testsrc.o
OBJS-$(CONFIG_COREIMAGESRC_FILTER)           += vf_coreimage.o
OBJS-$(CONFIG_FREI0R_SRC_FILTER)             += vf_frei0r.o
OBJS-$(CONFIG_GRADIENTS_FILTER)              += vsrc_gradients.o
OBJS-$(CONFIG_HALDCLUTSRC_FILTER)            += vsrc_testsrc.o
OBJS-$(CONFIG_LIFE_FILTER)                   += vsrc_life.o
OBJS-$(CONFIG_MANDELBROT_FILTER)             += vsrc_mandelbrot.o
OBJS-$(CONFIG_MPTESTSRC_FILTER)              += vsrc_mptestsrc.o
OBJS-$(CONFIG_NULLSRC_FILTER)                += vsrc_testsrc.o
OBJS-$(CONFIG_OPENCLSRC_FILTER)              += vf_program_opencl.o opencl.o
OBJS-$(CONFIG_PAL75BARS_FILTER)              += vsrc_testsrc.o
OBJS-$(CONFIG_PAL100BARS_FILTER)             += vsrc_testsrc.o
OBJS-$(CONFIG_RGBTESTSRC_FILTER)             += vsrc_testsrc.o
OBJS-$(CONFIG_SIERPINSKI_FILTER)             += vsrc_sierpinski.o
OBJS-$(CONFIG_SMPTEBARS_FILTER)              += vsrc_testsrc.o
OBJS-$(CONFIG_SMPTEHDBARS_FILTER)            += vsrc_testsrc.o
OBJS-$(CONFIG_TESTSRC_FILTER)                += vsrc_testsrc.o
OBJS-$(CONFIG_TESTSRC2_FILTER)               += vsrc_testsrc.o
OBJS-$(CONFIG_YUVTESTSRC_FILTER)             += vsrc_testsrc.o

OBJS-$(CONFIG_NULLSINK_FILTER)               += vsink_nullsink.o

# multimedia filters
OBJS-$(CONFIG_ABITSCOPE_FILTER)              += avf_abitscope.o
OBJS-$(CONFIG_ADRAWGRAPH_FILTER)             += f_drawgraph.o
OBJS-$(CONFIG_AGRAPHMONITOR_FILTER)          += f_graphmonitor.o
OBJS-$(CONFIG_AHISTOGRAM_FILTER)             += avf_ahistogram.o
OBJS-$(CONFIG_APHASEMETER_FILTER)            += avf_aphasemeter.o
OBJS-$(CONFIG_AVECTORSCOPE_FILTER)           += avf_avectorscope.o
OBJS-$(CONFIG_CONCAT_FILTER)                 += avf_concat.o
OBJS-$(CONFIG_SHOWCQT_FILTER)                += avf_showcqt.o lswsutils.o lavfutils.o
OBJS-$(CONFIG_SHOWFREQS_FILTER)              += avf_showfreqs.o
OBJS-$(CONFIG_SHOWSPATIAL_FILTER)            += avf_showspatial.o
OBJS-$(CONFIG_SHOWSPECTRUM_FILTER)           += avf_showspectrum.o
OBJS-$(CONFIG_SHOWSPECTRUMPIC_FILTER)        += avf_showspectrum.o
OBJS-$(CONFIG_SHOWVOLUME_FILTER)             += avf_showvolume.o
OBJS-$(CONFIG_SHOWWAVES_FILTER)              += avf_showwaves.o
OBJS-$(CONFIG_SHOWWAVESPIC_FILTER)           += avf_showwaves.o
OBJS-$(CONFIG_SPECTRUMSYNTH_FILTER)          += vaf_spectrumsynth.o

# multimedia sources
OBJS-$(CONFIG_AVSYNCTEST_FILTER)             += src_avsynctest.o
OBJS-$(CONFIG_AMOVIE_FILTER)                 += src_movie.o
OBJS-$(CONFIG_MOVIE_FILTER)                  += src_movie.o

# Objects duplicated from other libraries for shared builds
SHLIBOBJS                                    += log2_tab.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)                 += avfilterres.o

SKIPHEADERS-$(CONFIG_LCMS2)                  += fflcms2.h
SKIPHEADERS-$(CONFIG_LIBVIDSTAB)             += vidstabutils.h

SKIPHEADERS-$(CONFIG_QSVVPP)                 += qsvvpp.h
SKIPHEADERS-$(CONFIG_OPENCL)                 += opencl.h
SKIPHEADERS-$(CONFIG_VAAPI)                  += vaapi_vpp.h
SKIPHEADERS-$(CONFIG_VULKAN)                 += vulkan.h vulkan_filter.h

TOOLS     = graph2dot
TESTPROGS = drawutils filtfmts formats integral
TESTPROGS-$(CONFIG_DNN) += dnn-layer-avgpool dnn-layer-conv2d dnn-layer-dense  \
                           dnn-layer-depth2space dnn-layer-mathbinary          \
                           dnn-layer-mathunary dnn-layer-maximum dnn-layer-pad \

TOOLS-$(CONFIG_LIBZMQ) += zmqsend

clean::
	$(RM) $(CLEANSUFFIXES:%=libavfilter/dnn/%) $(CLEANSUFFIXES:%=libavfilter/opencl/%)

OPENCL = $(subst $(SRC_PATH)/,,$(wildcard $(SRC_PATH)/libavfilter/opencl/*.cl))
.SECONDARY: $(OPENCL:.cl=.c)
libavfilter/opencl/%.c: TAG = OPENCL
libavfilter/opencl/%.c: $(SRC_PATH)/libavfilter/opencl/%.cl
	$(M)$(SRC_PATH)/tools/cl2c $< $@
