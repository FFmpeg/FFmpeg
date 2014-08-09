LIBAVCODEC_$MAJOR {
        global: av*;
                #deprecated, remove after next bump
                audio_resample;
                audio_resample_close;
                ff_raw_pix_fmt_tags;
                ff_fft*;
                ff_mdct*;
                ff_dct*;
                ff_rdft*;
                ff_prores_idct_put_10_sse2;
                ff_simple_idct*;
                ff_aanscales;
                ff_faan*;
                ff_fdct*;
                ff_idct_xvid*;
                ff_jpeg_fdct*;
                ff_dnxhd_get_cid_table;
                ff_dnxhd_cid_table;
                ff_idctdsp_init;
                ff_fdctdsp_init;
                ff_pixblockdsp_init;
                ff_me_cmp_init;
        local:  *;
};
