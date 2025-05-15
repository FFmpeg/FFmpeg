/*
 * Copyright (c) 2025 - softworkz
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
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <sys/time.h>
#  include <time.h>
#endif
#include "graphprint.h"

int ff_open_html_in_browser(const char *html_path)
{
    if (!html_path || !*html_path)
        return -1;

#if defined(_WIN32)

    // --- Windows ---------------------------------
    {
        HINSTANCE rc = ShellExecuteA(NULL, "open", html_path, NULL, NULL, SW_SHOWNORMAL);
        if ((UINT_PTR)rc <= 32) {
            // Fallback: system("start ...")
            char cmd[1024];
            _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "start \"\" \"%s\"", html_path);
            if (system(cmd) != 0)
                return -1;
        }
        return 0;
    }

#elif defined(__APPLE__)

    // --- macOS -----------------------------------
    {
        // "open" is the macOS command to open a file/URL with the default application
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "open '%s' 1>/dev/null 2>&1 &", html_path);
        if (system(cmd) != 0)
            return -1;
        return 0;
    }

#else

    // --- Linux / Unix-like -----------------------
    // We'll try xdg-open, then gnome-open, then kfmclient
    {
        // Helper macro to try one browser command
        // Returns 0 on success, -1 on failure
        #define TRY_CMD(prog) do {                                   \
            char buf[1024];                                          \
            snprintf(buf, sizeof(buf), "%s '%s' 1>/dev/null 2>&1 &", \
                     (prog), html_path);                              \
            int ret = system(buf);                                    \
            /* On Unix: system() returns -1 if the shell can't run. */\
            /* Otherwise, check exit code in lower 8 bits.           */\
            if (ret != -1 && WIFEXITED(ret) && WEXITSTATUS(ret) == 0) \
                return 0;                                             \
        } while (0)

        TRY_CMD("xdg-open");
        TRY_CMD("gnome-open");
        TRY_CMD("kfmclient exec");

        fprintf(stderr, "Could not open '%s' in a browser.\n", html_path);
        return -1;
    }

#endif
}


int ff_get_temp_dir(char *buf, size_t size)
{
#if defined(_WIN32)

    // --- Windows ------------------------------------
    {
        // GetTempPathA returns length of the string (including trailing backslash).
        // If the return value is greater than buffer size, it's an error.
        DWORD len = GetTempPathA((DWORD)size, buf);
        if (len == 0 || len > size) {
            // Could not retrieve or buffer is too small
            return -1;
        }
        return 0;
    }

#else

    // --- macOS / Linux / Unix -----------------------
    // Follow typical POSIX convention: check common env variables
    // and fallback to /tmp if not found.
    {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || !*tmp) tmp = getenv("TMP");
        if (!tmp || !*tmp) tmp = getenv("TEMP");
        if (!tmp || !*tmp) tmp = "/tmp";

        // Copy into buf, ensure there's a trailing slash
        size_t len = strlen(tmp);
        if (len + 2 > size) {
            // Need up to len + 1 for slash + 1 for null terminator
            return -1;
        }

        strcpy(buf, tmp);
        // Append slash if necessary
        if (buf[len - 1] != '/' && buf[len - 1] != '\\') {
#if defined(__APPLE__)
            // On macOS/Unix, use forward slash
            buf[len] = '/';
            buf[len + 1] = '\0';
#else
            // Technically on Unix it's always '/', but here's how you'd do if needed:
            buf[len] = '/';
            buf[len + 1] = '\0';
#endif
        }
        return 0;
    }

#endif
}

int ff_make_timestamped_html_name(char *buf, size_t size)
{
#if defined(_WIN32)

    /*----------- Windows version -----------*/
    SYSTEMTIME st;
    GetLocalTime(&st);
    /*
      st.wYear, st.wMonth, st.wDay,
      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds
    */
    int written = _snprintf_s(buf, size, _TRUNCATE,
                              "ffmpeg_graph_%04d-%02d-%02d_%02d-%02d-%02d_%03d.html",
                              st.wYear,
                              st.wMonth,
                              st.wDay,
                              st.wHour,
                              st.wMinute,
                              st.wSecond,
                              st.wMilliseconds);
    if (written < 0)
        return -1; /* Could not write into buffer */
    return 0;

#else

    /*----------- macOS / Linux / Unix version -----------*/
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return -1; /* gettimeofday failed */
    }

    struct tm local_tm;
    localtime_r(&tv.tv_sec, &local_tm);

    int ms = (int)(tv.tv_usec / 1000); /* convert microseconds to milliseconds */

    /*
       local_tm.tm_year is years since 1900,
       local_tm.tm_mon  is 0-based (0=Jan, 11=Dec)
    */
    int written = snprintf(buf, size,
                           "ffmpeg_graph_%04d-%02d-%02d_%02d-%02d-%02d_%03d.html",
                           local_tm.tm_year + 1900,
                           local_tm.tm_mon + 1,
                           local_tm.tm_mday,
                           local_tm.tm_hour,
                           local_tm.tm_min,
                           local_tm.tm_sec,
                           ms);
    if (written < 0 || (size_t)written >= size) {
        return -1; /* Buffer too small or formatting error */
    }
    return 0;

#endif
}
