#!/bin/sh

. ffbuild/config.sh

if test "$shared" = "yes"; then
    shared=true
else
    shared=false
fi

shortname=$1
name=lib${shortname}
fullname=${name}${build_suffix}
comment=$2
libs=$(eval echo \$extralibs_${shortname})
deps=$(eval echo \$${shortname}_deps)

for dep in $deps; do
    depname=lib${dep}
    fulldepname=${depname}${build_suffix}
    . ${depname}/${depname}.version
    depversion=$(eval echo \$${depname}_VERSION)
    requires="$requires ${fulldepname} >= ${depversion}, "
done
requires=${requires%, }

version=$(grep ${name}_VERSION= $name/${name}.version | cut -d= -f2)

cat <<EOF > $name/$fullname.pc
prefix=$prefix
exec_prefix=\${prefix}
libdir=$libdir
includedir=$incdir

Name: $fullname
Description: $comment
Version: $version
Requires: $($shared || echo $requires)
Requires.private: $($shared && echo $requires)
Conflicts:
Libs: -L\${libdir} $rpath -l${fullname#lib} $($shared || echo $libs)
Libs.private: $($shared && echo $libs)
Cflags: -I\${includedir}
EOF

mkdir -p doc/examples/pc-uninstalled
includedir=${source_path}
[ "$includedir" = . ] && includedir="\${pcfiledir}/../../.."
    cat <<EOF > doc/examples/pc-uninstalled/${name}-uninstalled.pc
prefix=
exec_prefix=
libdir=\${pcfiledir}/../../../$name
includedir=${source_path}

Name: $fullname
Description: $comment
Version: $version
Requires: $requires
Conflicts:
Libs: -L\${libdir} -Wl,-rpath,\${libdir} -l${fullname#lib} $($shared || echo $libs)
Cflags: -I\${includedir}
EOF
