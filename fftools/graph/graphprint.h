/*
 * Copyright (c) 2018-2025 - softworkz
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

#ifndef FFTOOLS_GRAPH_GRAPHPRINT_H
#define FFTOOLS_GRAPH_GRAPHPRINT_H

#include "fftools/ffmpeg.h"

int print_filtergraphs(FilterGraph **graphs, int nb_graphs, InputFile **ifiles, int nb_ifiles, OutputFile **ofiles, int nb_ofiles);

int print_filtergraph(FilterGraph *fg, AVFilterGraph *graph);

/**
 * Open an HTML file in the default browser (Windows, macOS, Linux/Unix).
 *
 * @param html_path Absolute or relative path to the HTML file.
 * @return 0 on success, -1 on failure.
 *
 * NOTE: This uses system() calls for non-Windows, and ShellExecute on Windows.
 *       Exercise caution if 'html_path' is untrusted (possible command injection).
 */
int ff_open_html_in_browser(const char *html_path);

/**
 * Retrieve the system's temporary directory.
 *
 * @param buf  Output buffer to store the temp directory path (including trailing slash)
 * @param size Size of the output buffer in bytes
 * @return 0 on success, -1 on failure (buffer too small or other errors)
 *
 * Note: On most platforms, the path will include a trailing slash (e.g. "C:\\Users\\...\\Temp\\" on Windows, "/tmp/" on Unix).
 */
int ff_get_temp_dir(char *buf, size_t size);

/**
 * Create a timestamped HTML filename, e.g.:
 *   ffmpeg_graph_2024-01-01_22-12-59_123.html
 *
 * @param buf  Pointer to buffer where the result is stored
 * @param size Size of the buffer in bytes
 * @return 0 on success, -1 on error (e.g. buffer too small)
 */
int ff_make_timestamped_html_name(char *buf, size_t size);

#endif /* FFTOOLS_GRAPH_GRAPHPRINT_H */
