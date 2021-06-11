/*
 * This file is part of FFmpeg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *name;
    FILE *input, *output;
    unsigned int length = 0;
    unsigned char data;

    if (argc < 3 || argc > 4)
        return 1;

    input = fopen(argv[1], "rb");
    if (!input)
        return -1;

    output = fopen(argv[2], "wb");
    if (!output)
        return -1;

    if (argc == 4) {
        name = argv[3];
    } else {
        size_t arglen = strlen(argv[1]);
        name = argv[1];

        for (int i = 0; i < arglen; i++) {
            if (argv[1][i] == '.')
                argv[1][i] = '_';
            else if (argv[1][i] == '/')
                name = &argv[1][i+1];
        }
    }

    fprintf(output, "const unsigned char ff_%s_data[] = { ", name);

    while (fread(&data, 1, 1, input) > 0) {
        fprintf(output, "0x%02x, ", data);
        length++;
    }

    fprintf(output, "0x00 };\n");
    fprintf(output, "const unsigned int ff_%s_len = %u;\n", name, length);

    fclose(output);

    if (ferror(input) || !feof(input))
        return -1;

    fclose(input);

    return 0;
}
