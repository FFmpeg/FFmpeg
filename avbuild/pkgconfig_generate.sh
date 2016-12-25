#!/bin/sh

. avbuild/config.sh

if test "$shared" = "yes"; then
    shared=true
else
    shared=false
fi

shortname=$1
name=lib${shortname}
comment=$2
libs=$(eval echo \$extralibs_${shortname})
deps=$(eval echo \$${shortname}_deps)

for dep in $deps; do
    depname=lib${dep}
    . ${depname}/${depname}.version
    depversion=$(eval echo \$${depname}_VERSION)
    requires="$requires ${depname} >= ${depversion}, "
done

requires=${requires%, }

version=$(grep ${name}_VERSION= $name/${name}.version | cut -d= -f2)

cat <<EOF > $name/$name.pc
prefix=$prefix
exec_prefix=\${prefix}
libdir=$libdir
includedir=$incdir

Name: $name
Description: $comment
Version: $version
Requires: $($shared || echo $requires)
Requires.private: $($shared && echo $requires)
Conflicts:
Libs: -L\${libdir} -l${shortname} $($shared || echo $libs)
Libs.private: $($shared && echo $libs)
Cflags: -I\${includedir}
EOF

cat <<EOF > $name/$name-uninstalled.pc
prefix=
exec_prefix=
libdir=\${pcfiledir}
includedir=${source_path}

Name: $name
Description: $comment
Version: $version
Requires: $requires
Conflicts:
Libs: \${libdir}/${LIBPREF}${shortname}${LIBSUF} $libs
Cflags: -I\${includedir}
EOF
