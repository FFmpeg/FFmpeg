#ifndef FFMPEG_MOV_H
#define FFMPEG_MOV_H

/* mov.c */
extern const CodecTag ff_mov_obj_type[];

typedef struct Time2Sample{
    int count;
    int duration;
}Time2Sample;

#endif /* FFMPEG_MOV_H */
