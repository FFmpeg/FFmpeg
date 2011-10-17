LIBAVCODEC_$MAJOR {
        global: av*;
                audio_resample;
                audio_resample_close;
                #deprecated, remove after next bump
                img_get_alpha_info;
        local:  *;
};
