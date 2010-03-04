#! /bin/sh
FFMPEG=../ffmpeg

input_pix_fmts=$($FFMPEG -pix_fmts | sed -ne '9,$p' | grep '^I' | cut -d" " -f2)
output_pix_fmts=$($FFMPEG -pix_fmts | sed -ne '9,$p' | grep '^.O' | cut -d" " -f2)

for input_pix_fmt in $input_pix_fmts; do
    for output_pix_fmt in $output_pix_fmts; do
        swscale-test $input_pix_fmt $output_pix_fmt
    done
done
