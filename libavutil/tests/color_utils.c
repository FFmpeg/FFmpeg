/*
 * Copyright (c) 2015 Kevin Wheatley <kevin.j.wheatley@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include "libavutil/color_utils.c"
#include "libavutil/macros.h"

int main(int argc, char *argv[])
{
  int i, j;
  static const double test_data[] = {
      -0.1, -0.018053968510807, -0.01, -0.00449, 0.0, 0.00316227760, 0.005,
      0.009, 0.015, 0.1, 1.0, 52.37, 125.098765, 1999.11123, 6945.443,
      15123.4567, 19845.88923, 98678.4231, 99999.899998
  };

  for(i = 0; i < AVCOL_TRC_NB; i++) {
      avpriv_trc_function func = avpriv_get_trc_function_from_trc(i);
      for(j = 0; j < FF_ARRAY_ELEMS(test_data); j++) {
          if(func != NULL) {
              double result = func(test_data[j]);
              printf("AVColorTransferCharacteristic=%d calling func(%f) expected=%f\n",
                     i, test_data[j], result);
          }
      }
  }

}
