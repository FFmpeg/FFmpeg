NAME = avutil
DESC = FFmpeg utility library

HEADERS = adler32.h                                                     \
          aes.h                                                         \
          aes_ctr.h                                                     \
          attributes.h                                                  \
          audio_fifo.h                                                  \
          avassert.h                                                    \
          avstring.h                                                    \
          avutil.h                                                      \
          base64.h                                                      \
          blowfish.h                                                    \
          bprint.h                                                      \
          bswap.h                                                       \
          buffer.h                                                      \
          cast5.h                                                       \
          camellia.h                                                    \
          channel_layout.h                                              \
          common.h                                                      \
          cpu.h                                                         \
          crc.h                                                         \
          des.h                                                         \
          dict.h                                                        \
          display.h                                                     \
          downmix_info.h                                                \
          encryption_info.h                                             \
          error.h                                                       \
          eval.h                                                        \
          fifo.h                                                        \
          file.h                                                        \
          frame.h                                                       \
          hash.h                                                        \
          hmac.h                                                        \
          hwcontext.h                                                   \
          hwcontext_cuda.h                                              \
          hwcontext_d3d11va.h                                           \
          hwcontext_drm.h                                               \
          hwcontext_dxva2.h                                             \
          hwcontext_qsv.h                                               \
          hwcontext_mediacodec.h                                        \
          hwcontext_vaapi.h                                             \
          hwcontext_videotoolbox.h                                      \
          hwcontext_vdpau.h                                             \
          imgutils.h                                                    \
          intfloat.h                                                    \
          intreadwrite.h                                                \
          lfg.h                                                         \
          log.h                                                         \
          macros.h                                                      \
          mathematics.h                                                 \
          mastering_display_metadata.h                                  \
          md5.h                                                         \
          mem.h                                                         \
          motion_vector.h                                               \
          murmur3.h                                                     \
          opt.h                                                         \
          parseutils.h                                                  \
          pixdesc.h                                                     \
          pixelutils.h                                                  \
          pixfmt.h                                                      \
          random_seed.h                                                 \
          rc4.h                                                         \
          rational.h                                                    \
          replaygain.h                                                  \
          ripemd.h                                                      \
          samplefmt.h                                                   \
          sha.h                                                         \
          sha512.h                                                      \
          spherical.h                                                   \
          stereo3d.h                                                    \
          threadmessage.h                                               \
          time.h                                                        \
          timecode.h                                                    \
          timestamp.h                                                   \
          tree.h                                                        \
          twofish.h                                                     \
          version.h                                                     \
          xtea.h                                                        \
          tea.h                                                         \

HEADERS-$(CONFIG_LZO)                   += lzo.h

ARCH_HEADERS = bswap.h                                                  \
               intmath.h                                                \
               intreadwrite.h                                           \
               timer.h                                                  \

BUILT_HEADERS = avconfig.h                                              \
                ffversion.h

OBJS = adler32.o                                                        \
       aes.o                                                            \
       aes_ctr.o                                                        \
       audio_fifo.o                                                     \
       avstring.o                                                       \
       avsscanf.o                                                       \
       base64.o                                                         \
       blowfish.o                                                       \
       bprint.o                                                         \
       buffer.o                                                         \
       cast5.o                                                          \
       camellia.o                                                       \
       channel_layout.o                                                 \
       color_utils.o                                                    \
       cpu.o                                                            \
       crc.o                                                            \
       des.o                                                            \
       dict.o                                                           \
       display.o                                                        \
       downmix_info.o                                                   \
       encryption_info.o                                                \
       error.o                                                          \
       eval.o                                                           \
       fifo.o                                                           \
       file.o                                                           \
       file_open.o                                                      \
       float_dsp.o                                                      \
       fixed_dsp.o                                                      \
       frame.o                                                          \
       hash.o                                                           \
       hmac.o                                                           \
       hwcontext.o                                                      \
       imgutils.o                                                       \
       integer.o                                                        \
       intmath.o                                                        \
       lfg.o                                                            \
       lls.o                                                            \
       log.o                                                            \
       log2_tab.o                                                       \
       mathematics.o                                                    \
       mastering_display_metadata.o                                     \
       md5.o                                                            \
       mem.o                                                            \
       murmur3.o                                                        \
       opt.o                                                            \
       parseutils.o                                                     \
       pixdesc.o                                                        \
       pixelutils.o                                                     \
       random_seed.o                                                    \
       rational.o                                                       \
       reverse.o                                                        \
       rc4.o                                                            \
       ripemd.o                                                         \
       samplefmt.o                                                      \
       sha.o                                                            \
       sha512.o                                                         \
       slicethread.o                                                    \
       spherical.o                                                      \
       stereo3d.o                                                       \
       threadmessage.o                                                  \
       time.o                                                           \
       timecode.o                                                       \
       tree.o                                                           \
       twofish.o                                                        \
       utils.o                                                          \
       xga_font_data.o                                                  \
       xtea.o                                                           \
       tea.o                                                            \

OBJS-$(CONFIG_CUDA)                     += hwcontext_cuda.o cuda_check.o
OBJS-$(CONFIG_D3D11VA)                  += hwcontext_d3d11va.o
OBJS-$(CONFIG_DXVA2)                    += hwcontext_dxva2.o
OBJS-$(CONFIG_LIBDRM)                   += hwcontext_drm.o
OBJS-$(CONFIG_LZO)                      += lzo.o
OBJS-$(CONFIG_MEDIACODEC)               += hwcontext_mediacodec.o
OBJS-$(CONFIG_OPENCL)                   += hwcontext_opencl.o
OBJS-$(CONFIG_QSV)                      += hwcontext_qsv.o
OBJS-$(CONFIG_VAAPI)                    += hwcontext_vaapi.o
OBJS-$(CONFIG_VIDEOTOOLBOX)             += hwcontext_videotoolbox.o
OBJS-$(CONFIG_VDPAU)                    += hwcontext_vdpau.o

OBJS += $(COMPAT_OBJS:%=../compat/%)

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)            += avutilres.o

SKIPHEADERS-$(HAVE_CUDA_H)             += hwcontext_cuda.h
SKIPHEADERS-$(CONFIG_CUDA)             += hwcontext_cuda_internal.h     \
                                          cuda_check.h
SKIPHEADERS-$(CONFIG_D3D11VA)          += hwcontext_d3d11va.h
SKIPHEADERS-$(CONFIG_DXVA2)            += hwcontext_dxva2.h
SKIPHEADERS-$(CONFIG_QSV)              += hwcontext_qsv.h
SKIPHEADERS-$(CONFIG_OPENCL)           += hwcontext_opencl.h
SKIPHEADERS-$(CONFIG_VAAPI)            += hwcontext_vaapi.h
SKIPHEADERS-$(CONFIG_VIDEOTOOLBOX)     += hwcontext_videotoolbox.h
SKIPHEADERS-$(CONFIG_VDPAU)            += hwcontext_vdpau.h

TESTPROGS = adler32                                                     \
            aes                                                         \
            aes_ctr                                                     \
            audio_fifo                                                  \
            avstring                                                    \
            base64                                                      \
            blowfish                                                    \
            bprint                                                      \
            cast5                                                       \
            camellia                                                    \
            color_utils                                                 \
            cpu                                                         \
            crc                                                         \
            des                                                         \
            dict                                                        \
            display                                                     \
            encryption_info                                             \
            error                                                       \
            eval                                                        \
            file                                                        \
            fifo                                                        \
            hash                                                        \
            hmac                                                        \
            hwdevice                                                    \
            integer                                                     \
            imgutils                                                    \
            lfg                                                         \
            lls                                                         \
            log                                                         \
            md5                                                         \
            murmur3                                                     \
            opt                                                         \
            pca                                                         \
            parseutils                                                  \
            pixdesc                                                     \
            pixelutils                                                  \
            pixfmt_best                                                 \
            random_seed                                                 \
            rational                                                    \
            ripemd                                                      \
            sha                                                         \
            sha512                                                      \
            softfloat                                                   \
            tree                                                        \
            twofish                                                     \
            utf8                                                        \
            xtea                                                        \
            tea                                                         \

TESTPROGS-$(HAVE_THREADS)            += cpu_init
TESTPROGS-$(HAVE_LZO1X_999_COMPRESS) += lzo

TOOLS = crypto_bench ffhash ffeval ffescape

tools/crypto_bench$(EXESUF): ELIBS += $(if $(VERSUS),$(subst +, -l,+$(VERSUS)),)
tools/crypto_bench$(EXESUF): CFLAGS += -DUSE_EXT_LIBS=0$(if $(VERSUS),$(subst +,+USE_,+$(VERSUS)),)

$(SUBDIR)tests/lzo$(EXESUF): ELIBS = -llzo2
