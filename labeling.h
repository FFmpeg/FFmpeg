#ifndef LABELING_H
#define LABELING_H

#include "libavformat/avformat.h"

void timeline_update(AVFrame *frame, double pts, double duration);
void timeline_keydown(int label);
void timeline_keyup(int label);
void timeline_write_output(FILE *out);

#endif
