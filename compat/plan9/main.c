/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

int plan9_main(int argc, char **argv);

#undef main
int main(int argc, char **argv)
{
    /* The setfcr() function in lib9 is broken, must use asm. */
#ifdef __i386
    short fcr;
    __asm__ volatile ("fstcw        %0 \n"
                      "or      $63, %0 \n"
                      "fldcw        %0 \n"
                      : "=m"(fcr));
#endif

    return plan9_main(argc, argv);
}
