/*
 * Various simple utilities for ffmpeg system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

#if !defined(CONFIG_NOCUTILS)
/**
 * Return TRUE if val is a prefix of str. If it returns TRUE, ptr is
 * set to the next character in 'str' after the prefix.
 *
 * @param str input string
 * @param val prefix to test
 * @param ptr updated after the prefix in str in there is a match
 * @return TRUE if there is a match
 */
int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/**
 * Return TRUE if val is a prefix of str (case independent). If it
 * returns TRUE, ptr is set to the next character in 'str' after the
 * prefix.
 *
 * @param str input string
 * @param val prefix to test
 * @param ptr updated after the prefix in str in there is a match
 * @return TRUE if there is a match */
int stristart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
	if (toupper(*(const unsigned char *)p) != toupper(*(const unsigned char *)q))
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/**
 * Copy the string str to buf. If str length is bigger than buf_size -
 * 1 then it is clamped to buf_size - 1.
 * NOTE: this function does what strncpy should have done to be
 * useful. NEVER use strncpy.
 * 
 * @param buf destination buffer
 * @param buf_size size of destination buffer
 * @param str source string
 */
void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size) 
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

#endif

/* add one element to a dynamic array */
void __dynarray_add(unsigned long **tab_ptr, int *nb_ptr, unsigned long elem)
{
    int nb, nb_alloc;
    unsigned long *tab;

    nb = *nb_ptr;
    tab = *tab_ptr;
    if ((nb & (nb - 1)) == 0) {
        if (nb == 0)
            nb_alloc = 1;
        else
            nb_alloc = nb * 2;
        tab = av_realloc(tab, nb_alloc * sizeof(unsigned long));
        *tab_ptr = tab;
    }
    tab[nb++] = elem;
    *nb_ptr = nb;
}

time_t mktimegm(struct tm *tm)
{
    time_t t;

    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;

    if (m < 3) {
        m += 12;
        y--;
    }

    t = 86400 * 
        (d + (153 * m - 457) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 719469);

    t += 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;

    return t;
}

/* get a positive number between n_min and n_max, for a maximum length
   of len_max. Return -1 if error. */
static int date_get_num(const char **pp,
                        int n_min, int n_max, int len_max)
{
    int i, val, c;
    const char *p;

    p = *pp;
    val = 0;
    for(i = 0; i < len_max; i++) {
        c = *p;
        if (!isdigit(c))
            break;
        val = (val * 10) + c - '0';
        p++;
    }
    /* no number read ? */
    if (p == *pp)
        return -1;
    if (val < n_min || val > n_max)
        return -1;
    *pp = p;
    return val;
}

/* small strptime for ffmpeg */
const char *small_strptime(const char *p, const char *fmt, 
                           struct tm *dt)
{
    int c, val;

    for(;;) {
        c = *fmt++;
        if (c == '\0') {
            return p;
        } else if (c == '%') {
            c = *fmt++;
            switch(c) {
            case 'H':
                val = date_get_num(&p, 0, 23, 2);
                if (val == -1)
                    return NULL;
                dt->tm_hour = val;
                break;
            case 'M':
                val = date_get_num(&p, 0, 59, 2);
                if (val == -1)
                    return NULL;
                dt->tm_min = val;
                break;
            case 'S':
                val = date_get_num(&p, 0, 59, 2);
                if (val == -1)
                    return NULL;
                dt->tm_sec = val;
                break;
            case 'Y':
                val = date_get_num(&p, 0, 9999, 4);
                if (val == -1)
                    return NULL;
                dt->tm_year = val - 1900;
                break;
            case 'm':
                val = date_get_num(&p, 1, 12, 2);
                if (val == -1)
                    return NULL;
                dt->tm_mon = val - 1;
                break;
            case 'd':
                val = date_get_num(&p, 1, 31, 2);
                if (val == -1)
                    return NULL;
                dt->tm_mday = val;
                break;
            case '%':
                goto match;
            default:
                return NULL;
            }
        } else {
        match:
            if (c != *p)
                return NULL;
            p++;
        }
    }
    return p;
}

