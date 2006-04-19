#ifndef FFMPEG_MOV_H
#define FFMPEG_MOV_H

/* mov.c */
extern const CodecTag ff_mov_obj_type[];
extern int ff_mov_iso639_to_lang(const char *lang, int mp4);

typedef struct Time2Sample{
    int count;
    int duration;
}Time2Sample;

#endif /* FFMPEG_MOV_H */
