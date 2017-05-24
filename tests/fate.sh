#! /bin/sh

config=$1

die(){
    echo "$@"
    exit 1
}

test -r "$config"  || die "usage: fate.sh <config>"

workdir=$(cd $(dirname $config) && pwd)
make=make
tar='tar c'

. "$config"

test -n "$slot"    || die "slot not specified"
test -n "$repo"    || die "repo not specified"
test -d "$samples" || die "samples location not specified"

: ${branch:=master}

lock(){
    lock=$1/fate.lock
    (set -C; exec >$lock) 2>/dev/null || return
    trap 'rm $lock' EXIT
}

checkout(){
    case "$repo" in
        file:*|/*) src="${repo#file:}"      ;;
        git:*)     git clone --quiet --branch "$branch" "$repo" "$src" ;;
    esac
}

update()(
    cd ${src} || return
    case "$repo" in
        git:*) git fetch --quiet --force && git reset --quiet --hard "origin/$branch" ;;
    esac
)

configure()(
    cd ${build} || return
    ${shell} ${src}/configure                                           \
        --prefix="${inst}"                                              \
        --samples="${samples}"                                          \
        --enable-gpl                                                    \
        --enable-memory-poisoning                                       \
        --enable-avresample                                             \
        ${ignore_tests:+--ignore-tests="$ignore_tests"}                 \
        ${arch:+--arch=$arch}                                           \
        ${cpu:+--cpu="$cpu"}                                            \
        ${toolchain:+--toolchain="$toolchain"}                          \
        ${cross_prefix:+--cross-prefix="$cross_prefix"}                 \
        ${as:+--as="$as"}                                               \
        ${cc:+--cc="$cc"}                                               \
        ${ld:+--ld="$ld"}                                               \
        ${target_os:+--target-os="$target_os"}                          \
        ${sysroot:+--sysroot="$sysroot"}                                \
        ${target_exec:+--target-exec="$target_exec"}                    \
        ${target_path:+--target-path="$target_path"}                    \
        ${target_samples:+--target-samples="$target_samples"}           \
        ${extra_cflags:+--extra-cflags="$extra_cflags"}                 \
        ${extra_ldflags:+--extra-ldflags="$extra_ldflags"}              \
        ${extra_libs:+--extra-libs="$extra_libs"}                       \
        ${extra_conf}
)

compile()(
    cd ${build} || return
    ${make} ${makeopts} && ${make} install
)

fate()(
    test "$build_only" = "yes" && return
    cd ${build} || return
    ${make} ${makeopts_fate-${makeopts}} -k fate
)

clean(){
    rm -rf ${build} ${inst}
}

report(){
    date=$(date -u +%Y%m%d%H%M%S)
    echo "fate:1:${date}:${slot}:${version}:$1:$2:${branch}:${comment}" >report
    cat ${build}/ffbuild/config.fate >>report
    cat ${build}/tests/data/fate/*.rep >>report 2>/dev/null || for i in ${build}/tests/data/fate/*.rep ; do cat "$i" >>report 2>/dev/null; done
    test -n "$fate_recv" && $tar report *.log | gzip | $fate_recv
}

fail(){
    report "$@"
    clean
    exit
}

mkdir -p ${workdir} || die "Error creating ${workdir}"
lock ${workdir}     || die "${workdir} locked"
cd ${workdir}       || die "cd ${workdir} failed"

src=${workdir}/src
: ${build:=${workdir}/build}
: ${inst:=${workdir}/install}

test -d "$src" && update || checkout || die "Error fetching source"

cd ${workdir}

version=$(${src}/ffbuild/version.sh ${src})
test "$version" = "$(cat version-$slot 2>/dev/null)" && exit 0
echo ${version} >version-$slot

rm -rf "${build}" *.log
mkdir -p ${build}

configure >configure.log 2>&1 || fail 3 "error configuring"
compile   >compile.log   2>&1 || fail 2 "error compiling"
fate      >test.log      2>&1 || fail 1 "error testing"
report 0 success
clean
