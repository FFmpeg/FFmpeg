/*
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

/*
 * msvc compiler wrapper, used as: mscl <compiler> <arguments...>
 *
 * cl.exe cannot write a GNU style dependency file directly. When
 * -showIncludes is present on the command line, this wrapper converts the
 * include records from the compiler's stdout into a .d file, written next to
 * the object file as a byproduct of compilation.
 *
 * Dependency paths below the current directory are written relative to it,
 * which keeps the .d files independent of how the environment maps Windows
 * drives (MSYS, Cygwin and WSL all differ). Everything else stays absolute,
 * in forward slash form. Spaces, '#' and '$' are escaped the same way gcc
 * escapes them in -MD output.
 *
 * On a POSIX host driving a Windows compiler (cl.exe through WSL interop, for
 * example) the records carry Windows paths. Each distinct drive or share root
 * is translated once with wslpath or cygpath and the result is reused for
 * every path below it.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/macros.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define INC_PREFIX "Note: including file:"

static const char *obj;
static const char *src;

static char *cwd;

static char **deps;
static size_t nb_deps, max_deps;

static char *linebuf;
static size_t line_len, line_max;

static void die(const char *msg)
{
    fprintf(stderr, "mscl: %s\n", msg);
    exit(1);
}

static void *xrealloc(void *ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if (!ptr)
        die("out of memory");
    return ptr;
}

static char *xstrdup(const char *s)
{
    size_t size = strlen(s) + 1;
    return memcpy(xrealloc(NULL, size), s, size);
}

#ifdef _WIN32

/* code page the compiler writes its output in */
static UINT out_cp;

static wchar_t *mb_to_wide(const char *s, UINT cp)
{
    int n = MultiByteToWideChar(cp, 0, s, -1, NULL, 0);
    wchar_t *w = xrealloc(NULL, (n ? n : 1) * sizeof(*w));
    if (!n)
        *w = 0;
    else
        MultiByteToWideChar(cp, 0, s, -1, w, n);
    return w;
}

static char *wide_to_mb(const wchar_t *w, UINT cp)
{
    int n = WideCharToMultiByte(cp, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = xrealloc(NULL, n ? n : 1);
    if (!n)
        *s = 0;
    else
        WideCharToMultiByte(cp, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static char *recode(const char *s, UINT from, UINT to)
{
    wchar_t *w = mb_to_wide(s, from);
    char *out = wide_to_mb(w, to);
    free(w);
    return out;
}

static FILE *open_wb(const char *path)
{
    wchar_t *w = mb_to_wide(path, CP_UTF8);
    FILE *f = _wfopen(w, L"wb");
    free(w);
    return f;
}

static int unlink_file(const char *path)
{
    wchar_t *w = mb_to_wide(path, CP_UTF8);
    int ret = _wremove(w);
    free(w);
    return ret;
}

static int replace_file(const char *from, const char *to)
{
    wchar_t *wfrom = mb_to_wide(from, CP_UTF8), *wto = mb_to_wide(to, CP_UTF8);
    int ret = !MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING);
    free(wfrom);
    free(wto);
    return ret;
}

#else

static FILE *open_wb(const char *path)
{
    return fopen(path, "wb");
}

static int unlink_file(const char *path)
{
    return remove(path);
}

static int replace_file(const char *from, const char *to)
{
    return rename(from, to);
}

#endif

static const char *file_basename(const char *p)
{
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\')
            p = q + 1;
    return p;
}

static int is_source(const char *arg)
{
    static const char *const exts[] = {
        ".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".s", ".S", ".asm"
    };
    const char *dot = strrchr(file_basename(arg), '.');
    if (!dot)
        return 0;
    for (size_t i = 0; i < FF_ARRAY_ELEMS(exts); i++)
        if (!strcmp(dot, exts[i]))
            return 1;
    return 0;
}

static int chr_eq(char a, char b)
{
#ifdef _WIN32
    return tolower(a) == tolower(b);
#else
    return a == b;
#endif
}

/* Relative form of a path below the current directory, NULL for any other. */
static char *make_relative(const char *path)
{
    size_t i;

    for (i = 0; cwd[i]; i++)
        if (!path[i] || !chr_eq(path[i], cwd[i]))
            return NULL;
    if (path[i] != '/' || !path[i + 1])
        return NULL;
    return xstrdup(path + i + 1);
}

static void add_dep(char *path)
{
    for (size_t i = 0; i < nb_deps; i++)
        if (!strcmp(deps[i], path)) {
            free(path);
            return;
        }
    if (nb_deps >= max_deps) {
        max_deps = max_deps ? 2 * max_deps : 64;
        deps = xrealloc(deps, max_deps * sizeof(*deps));
    }
    deps[nb_deps++] = path;
}

static const char *unmapped;

#ifndef _WIN32

/* Length of the root that "../" cannot climb out of, "//server/share" for
 * UNC paths and "X:" for drive letter paths, 0 otherwise. */
static size_t root_len(const char *p)
{
    size_t i = 0;

    if (p[0] == '/' && p[1] == '/') {
        for (i = 2; p[i] && p[i] != '/'; i++)
            ;
        if (p[i])
            for (i++; p[i] && p[i] != '/'; i++)
                ;
    } else if (isalpha(p[0]) && p[1] == ':') {
        i = 2;
    }
    return i;
}

static struct {
    char *root;
    char *host;
} *roots;
static size_t nb_roots;

static char *path_helper(const char *prog, const char *root)
{
    char *args[] = { (char *)prog, "-u", (char *)root, NULL };
    char buf[4096];
    size_t len = 0;
    int fds[2], st;
    pid_t pid;

    if (pipe(fds) < 0)
        die("pipe failed");
    pid = fork();
    if (pid < 0)
        die("fork failed");
    if (!pid) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp(prog, args);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        char tmp[512];
        ssize_t n = read(fds[0], tmp, sizeof(tmp));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        if (len + n < sizeof(buf)) {
            memcpy(buf + len, tmp, n);
            len += n;
        }
    }
    close(fds[0]);
    if (waitpid(pid, &st, 0) < 0)
        die("waitpid failed");
    if (!WIFEXITED(st) || WEXITSTATUS(st) || !len)
        return NULL;
    buf[len] = 0;
    len = strcspn(buf, "\r\n");
    while (len && buf[len - 1] == '/')
        len--;
    buf[len] = 0;
    return xstrdup(buf);
}

/* Translate a Windows path to the host namespace, NULL if impossible. */
static char *host_path(const char *path)
{
    size_t r = root_len(path), i;
    char *root, *host = NULL, *out;

    if (path[r] == '/')
        r++;
    root = xstrdup(path);
    root[r] = 0;
    for (char *q = root; *q; q++)
        *q = tolower(*q);

    for (i = 0; i < nb_roots; i++)
        if (!strcmp(roots[i].root, root))
            break;
    if (i == nb_roots) {
        host = path_helper("wslpath", root);
        if (!host)
            host = path_helper("cygpath", root);
        roots = xrealloc(roots, (nb_roots + 1) * sizeof(*roots));
        roots[nb_roots].root = root;
        roots[nb_roots].host = host;
        nb_roots++;
    } else {
        host = roots[i].host;
        free(root);
    }
    if (!host)
        return NULL;

    /* the root keeps its slash, the host form lost it */
    out = xrealloc(NULL, strlen(host) + strlen(path + r) + 2);
    strcpy(out, host);
    strcat(out, "/");
    strcat(out, path + r);
    return out;
}

#endif

static void add_include(char *p)
{
    char *own = NULL, *rel;

#ifdef _WIN32
    p = own = recode(p, out_cp, CP_UTF8);
#endif

    for (char *q = p; *q; q++)
        if (*q == '\\')
            *q = '/';

#ifndef _WIN32
    if (root_len(p)) {
        own = host_path(p);
        if (!own) {
            if (!unmapped)
                unmapped = xstrdup(p);
            return;
        }
        p = own;
    }
#endif

    rel = make_relative(p);
    if (rel) {
        free(own);
        add_dep(rel);
    } else {
        add_dep(own ? own : xstrdup(p));
    }
}

static void process_line(char *line)
{
    size_t len = strlen(line);
    while (len && line[len - 1] == '\r')
        line[--len] = 0;

    if (!strncmp(line, INC_PREFIX, sizeof(INC_PREFIX) - 1)) {
        char *p = line + sizeof(INC_PREFIX) - 1;
        while (*p == ' ')
            p++;
        if (*p)
            add_include(p);
        return;
    }
    /* cl.exe echoes the source file name; drop it and empty lines */
    if (*line && !(src && !strcmp(line, src)))
        puts(line);
}

static void feed(const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            linebuf[line_len] = 0;
            process_line(linebuf);
            line_len = 0;
        } else {
            if (line_len + 2 > line_max) {
                line_max = line_max ? 2 * line_max : 4096;
                linebuf = xrealloc(linebuf, line_max);
            }
            linebuf[line_len++] = buf[i];
        }
    }
}

static void feed_flush(void)
{
    if (line_len) {
        linebuf[line_len] = 0;
        process_line(linebuf);
        line_len = 0;
    }
}

static void init_deps(void)
{
    size_t len;

#ifdef _WIN32
    DWORD size = GetCurrentDirectoryW(0, NULL);
    wchar_t *wcwd = xrealloc(NULL, size * sizeof(*wcwd));
    if (!GetCurrentDirectoryW(size, wcwd))
        die("GetCurrentDirectory failed");
    cwd = wide_to_mb(wcwd, CP_UTF8);
    free(wcwd);
#else
    cwd = getcwd(NULL, 0);
    if (!cwd)
        die("getcwd failed");
#endif
    for (char *q = cwd; *q; q++)
        if (*q == '\\')
            *q = '/';
    len = strlen(cwd);
    while (len && cwd[len - 1] == '/')
        cwd[--len] = 0;

    linebuf = xrealloc(NULL, line_max = 4096);
}

#ifdef _WIN32

/* Return the command line as received, minus our own program name, so the
 * compiler gets its arguments verbatim without a requoting round-trip. */
static wchar_t *cmdline_tail(void)
{
    wchar_t *p = GetCommandLineW();
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"')
            p++;
        if (*p)
            p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t')
            p++;
    }
    while (*p == L' ' || *p == L'\t')
        p++;
    return _wcsdup(p);
}

/* Split the command line into UTF-8 arguments. CommandLineToArgvW would do the
 * same, but we want to avoid pulling in shell32.dll. */
static char **split_cmdline(const wchar_t *p, int *argc)
{
    wchar_t *buf = xrealloc(NULL, (wcslen(p) + 1) * sizeof(*buf));
    char **argv = NULL;
    int n = 0;

    for (;;) {
        wchar_t *q = buf;
        int quoted = 0;

        while (*p == L' ' || *p == L'\t')
            p++;
        if (!*p)
            break;
        for (;;) {
            size_t bs = 0;
            while (*p == L'\\') {
                bs++;
                p++;
            }
            if (*p == L'"') {
                for (size_t i = 0; i < bs / 2; i++)
                    *q++ = L'\\';
                if (bs & 1) {
                    *q++ = L'"';
                } else if (quoted && p[1] == L'"') {
                    *q++ = L'"';
                    p++;
                } else {
                    quoted = !quoted;
                }
                p++;
                continue;
            }
            for (size_t i = 0; i < bs; i++)
                *q++ = L'\\';
            if (!*p || (!quoted && (*p == L' ' || *p == L'\t')))
                break;
            *q++ = *p++;
        }
        *q = 0;
        argv = xrealloc(argv, (n + 2) * sizeof(*argv));
        argv[n++] = wide_to_mb(buf, CP_UTF8);
    }
    if (argv)
        argv[n] = NULL;
    free(buf);
    *argc = n;
    return argv;
}

static int run(wchar_t *cmdline, int want_pipe)
{
    HANDLE rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdInput,  HANDLE_FLAG_INHERIT, 1);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, 1);
    SetHandleInformation(si.hStdError,  HANDLE_FLAG_INHERIT, 1);

    if (want_pipe) {
        if (!CreatePipe(&rd, &wr, &sa, 0))
            die("CreatePipe failed");
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = wr;
        /* English include records are required for the parsing above */
        SetEnvironmentVariableA("VSLANG", "1033");
    }

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL,
                        &si, &pi))
        die("cannot execute compiler");
    CloseHandle(pi.hThread);

    if (want_pipe) {
        char buf[65536];
        DWORD n;
        CloseHandle(wr);
        while (ReadFile(rd, buf, sizeof(buf), &n, NULL) && n > 0)
            feed(buf, n);
        CloseHandle(rd);
        feed_flush();
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}

#else

static int run(char **args)
{
    const char *wslenv;
    int fds[2], st;
    pid_t pid;

    if (pipe(fds) < 0)
        die("pipe failed");
    setenv("VSLANG", "1033", 1);
    /* WSL hands the environment to Windows programs only for the variables
     * listed in WSLENV */
    wslenv = getenv("WSLENV");
    if (wslenv && *wslenv) {
        char *e = xrealloc(NULL, strlen(wslenv) + sizeof(":VSLANG"));
        strcpy(e, wslenv);
        strcat(e, ":VSLANG");
        setenv("WSLENV", e, 1);
        free(e);
    } else {
        setenv("WSLENV", "VSLANG", 1);
    }
    pid = fork();
    if (pid < 0)
        die("fork failed");
    if (!pid) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp(args[0], args);
        perror(args[0]);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        char buf[65536];
        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        feed(buf, n);
    }
    close(fds[0]);
    feed_flush();

    if (waitpid(pid, &st, 0) < 0)
        die("waitpid failed");
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

#endif

/* gcc style escaping for make: '\ ', '\#' and '$$' */
static void fput_escaped(const char *s, FILE *f)
{
    for (; *s; s++) {
        if (*s == ' ' || *s == '#')
            fputc('\\', f);
        else if (*s == '$')
            fputc('$', f);
        fputc(*s, f);
    }
}

/* A failure to write the dependency file fails the build. */
static int write_depfile(void)
{
    size_t len = strlen(obj);
    char *path = xrealloc(NULL, len + 3), *tmp;
    FILE *f;
    int err;

    memcpy(path, obj, len + 1);
    if (len > 2 && !strcmp(path + len - 2, ".o"))
        len -= 2;
    strcpy(path + len, ".d");

    if (unmapped) {
        fprintf(stderr, "mscl: cannot map %s to a host path, "
                "wslpath or cygpath is required\n", unmapped);
        return 1;
    }

    tmp = xrealloc(NULL, len + 7);
    memcpy(tmp, path, len + 2);
    strcpy(tmp + len + 2, ".tmp");

    f = open_wb(tmp);
    if (f) {
        fput_escaped(obj, f);
        fputc(':', f);
        for (size_t i = 0; i < nb_deps; i++) {
            fputc(' ', f);
            fput_escaped(deps[i], f);
        }
        fputc('\n', f);
        /* -MP style dummy rules; make then treats a vanished header as out
         * of date instead of erroring out on a missing prerequisite */
        for (size_t i = 0; i < nb_deps; i++) {
            fput_escaped(deps[i], f);
            fputs(":\n", f);
        }
        err = ferror(f);
        if (!fclose(f) && !err && !replace_file(tmp, path))
            return 0;
        unlink_file(tmp);
    }
    fprintf(stderr, "mscl: cannot write %s\n", path);
    return 1;
}

int main(int argc, char **argv)
{
    int show_includes = 0;
    int status;

#ifdef _WIN32
    argv = split_cmdline(GetCommandLineW(), &argc);
    out_cp = GetConsoleOutputCP();
    if (!out_cp)
        out_cp = GetACP();
#endif

    if (argc < 2) {
        fprintf(stderr, "usage: mscl <compiler> <arguments...>\n");
        return 2;
    }

    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-showIncludes") || !strcmp(arg, "/showIncludes"))
            show_includes = 1;
        else if (!strncmp(arg, "-Fo", 3) || !strncmp(arg, "/Fo", 3))
            obj = arg + 3;
        else if (is_source(arg))
            src = file_basename(arg);
    }
#ifdef _WIN32
    if (src)
        src = recode(src, CP_UTF8, out_cp);
#endif

    /* without an object there is nothing to write a .d file for, leave the
     * include records alone in that case */
    if (!obj)
        show_includes = 0;

#ifdef _WIN32
    wchar_t *cmdline = cmdline_tail();
    if (!show_includes)
        return run(cmdline, 0);
    init_deps();
    status = run(cmdline, 1);
#else
    if (!show_includes) {
        execvp(argv[1], argv + 1);
        perror(argv[1]);
        return 127;
    }
    init_deps();
    status = run(argv + 1);
#endif

    /* A failed compilation is the primary result; do not write a dependency
     * file from its truncated output. */
    if (status != 0)
        return status;
    if (!write_depfile())
        return 0;
    unlink_file(obj);
    return 1;
}
