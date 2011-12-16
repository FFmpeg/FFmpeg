LIBAVCODEC_$MAJOR {
        global: av*;
                audio_resample;
                audio_resample_close;
                #deprecated, remove after next bump
                img_get_alpha_info;
                dsputil_init;
                ff_find_pix_fmt;
                ff_framenum_to_drop_timecode;
                ff_framenum_to_smtpe_timecode;
                ff_raw_pix_fmt_tags;
                ff_init_smtpe_timecode;
                ff_fft*;
                ff_mdct*;
                ff_dct*;
                ff_rdft*;
                ff_prores_idct_put_10_sse2;
                ff_simple_idct*;
                ff_aanscales;
                ff_faan*;
                ff_mmx_idct;
                ff_fdct*;
                fdct_ifast;
                j_rev_dct;
                ff_mmxext_idct;
                ff_idct_xvid*;
                ff_jpeg_fdct*;
                #XBMC's configure checks for ff_vdpau_vc1_decode_picture()
                ff_vdpau_vc1_decode_picture;
        local:  *;
};
