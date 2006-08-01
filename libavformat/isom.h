#ifndef FFMPEG_ISOM_H
#define FFMPEG_ISOM_H

/* isom.c */
extern const CodecTag ff_mov_obj_type[];

int ff_mov_iso639_to_lang(const char *lang, int mp4);
int ff_mov_lang_to_iso639(int code, char *to);

typedef struct Time2Sample{
    int count;
    int duration;
}Time2Sample;

#endif /* FFMPEG_ISOM_H */
