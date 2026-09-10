# try to find an md5 program

if md5sum -b < /dev/null > /dev/null 2>&1; then
    do_md5sum() { md5sum -b $1; }
elif command md5 < /dev/null > /dev/null 2>&1; then
    do_md5sum() { command md5 $1 | sed 's#MD5 (\(.*\)) = \(.*\)#\2 *\1#'; }
elif [ -x /sbin/md5 ]; then
    do_md5sum() { /sbin/md5 -r $1 | sed 's/\([0-9a-f]\) [ *]*/\1 */'; }
elif openssl version >/dev/null 2>&1; then
    do_md5sum() { openssl md5 $1 | sed 's/MD5(\(.*\))= \(.*\)/\2 *\1/'; }
else
    do_md5sum() { echo No md5sum program found; }
fi
