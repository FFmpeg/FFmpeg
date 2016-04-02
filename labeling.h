#ifndef LABELING_H
#define LABELING_H

#include "libavformat/avformat.h"

void draw_timeline(AVFrame *frame, double pts, double duration);

#endif
