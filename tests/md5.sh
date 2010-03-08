# try to find an md5 program

if [ X"$(echo | md5sum 2> /dev/null)" != X ]; then
    do_md5sum() { md5sum -b $1; }
elif [ X"$(echo | md5 2> /dev/null)" != X ]; then
    do_md5sum() { md5 $1 | sed 's#MD5 (\(.*\)) = \(.*\)#\2 *\1#'; }
elif [ -x /sbin/md5 ]; then
    do_md5sum() { /sbin/md5 -r $1 | sed 's# \**\./# *./#'; }
else
    do_md5sum() { echo No md5sum program found; }
fi
