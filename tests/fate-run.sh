#! /bin/sh

export LC_ALL=C

base=$(dirname $0)
. "${base}/md5.sh"

base64=tests/base64

test="${1#fate-}"
samples=$2
target_exec=$3
target_path=$4
command=$5
cmp=${6:-diff}
ref=${7:-"${base}/ref/fate/${test}"}
fuzz=$8
threads=${9:-1}
thread_type=${10:-3}

outdir="tests/data/fate"
outfile="${outdir}/${test}"
errfile="${outdir}/${test}.err"
cmpfile="${outdir}/${test}.diff"
repfile="${outdir}/${test}.rep"

do_tiny_psnr(){
    psnr=$(tests/tiny_psnr "$1" "$2" 2 0 0)
    val=$(expr "$psnr" : ".*$3: *\([0-9.]*\)")
    size1=$(expr "$psnr" : '.*bytes: *\([0-9]*\)')
    size2=$(expr "$psnr" : '.*bytes:[ 0-9]*/ *\([0-9]*\)')
    res=$(echo "if ($val $4 $5) 1" | bc)
    if [ "$res" != 1 ] || [ $size1 != $size2 ]; then
        echo "$psnr"
        return 1
    fi
}

oneoff(){
    do_tiny_psnr "$1" "$2" MAXDIFF '<=' ${fuzz:-1}
}

stddev(){
    do_tiny_psnr "$1" "$2" stddev  '<=' ${fuzz:-1}
}

run(){
    test "${V:-0}" -gt 0 && echo "$target_exec" $target_path/"$@" >&3
    $target_exec $target_path/"$@"
}

ffmpeg(){
    run ffmpeg -v 0 -threads $threads -thread_type $thread_type "$@"
}

framecrc(){
    ffmpeg "$@" -f framecrc -
}

framemd5(){
    ffmpeg "$@" -f framemd5 -
}

crc(){
    ffmpeg "$@" -f crc -
}

md5(){
    ffmpeg "$@" md5:
}

pcm(){
    ffmpeg "$@" -vn -f s16le -
}

regtest(){
    t="${test#$2-}"
    ref=${base}/ref/$2/$t
    cleanfiles="$cleanfiles $outfile $errfile"
    outfile=tests/data/regression/$2/$t
    errfile=tests/data/$t.$2.err
    ${base}/${1}-regression.sh $t $2 $3 "$target_exec" "$target_path" "$threads" "$thread_type"
}

codectest(){
    regtest codec $1 tests/$1
}

lavftest(){
    regtest lavf lavf tests/vsynth1
}

lavfitest(){
    cleanfiles="tests/data/lavfi/${test#lavfi-}.nut"
    regtest lavfi lavfi tests/vsynth1
}

seektest(){
    t="${test#seek-}"
    ref=${base}/ref/seek/$t
    case $t in
        image_*) file="tests/data/images/${t#image_}/%02d.${t#image_}" ;;
        *)       file=$(echo $t | tr _ '?')
                 for d in acodec vsynth2 lavf; do
                     test -f tests/data/$d/$file && break
                 done
                 file=$(echo tests/data/$d/$file)
                 ;;
    esac
    $target_exec $target_path/tests/seek_test $target_path/$file
}

mkdir -p "$outdir"

exec 3>&2
$command > "$outfile" 2>$errfile
err=$?

if [ $err -gt 128 ]; then
    sig=$(kill -l $err 2>/dev/null)
    test "${sig}" = "${sig%[!A-Za-z]*}" || unset sig
fi

if test -e "$ref"; then
    case $cmp in
        diff)   diff -u -w "$ref" "$outfile"            >$cmpfile ;;
        oneoff) oneoff     "$ref" "$outfile" "$fuzz"    >$cmpfile ;;
        stddev) stddev     "$ref" "$outfile" "$fuzz"    >$cmpfile ;;
    esac
    cmperr=$?
    test $err = 0 && err=$cmperr
    test $err = 0 || cat $cmpfile
else
    echo "reference file '$ref' not found"
    err=1
fi

echo "${test}:${sig:-$err}:$($base64 <$cmpfile):$($base64 <$errfile)" >$repfile

test $err = 0 && rm -f $outfile $errfile $cmpfile $cleanfiles
exit $err
