#! /bin/sh

base=$(dirname $0)
. "${base}/md5.sh"

test="${1#fate-}"
samples=$2
target_exec=$3
target_path=$4
command=$5
cmp=${6:-diff}
ref=${7:-"${base}/ref/fate/${test}"}
fuzz=$8

outdir="tests/data/fate"
outfile="${outdir}/${test}"

do_tiny_psnr(){
    psnr=$(tests/tiny_psnr "$1" "$2" 2 0 0)
    val=$(expr "$psnr" : ".*$3: *\([0-9.]*\)")
    size1=$(expr "$psnr" : '.*bytes: *\([0-9]*\)')
    size2=$(expr "$psnr" : '.*bytes:[ 0-9]*/ *\([0-9]*\)')
    res=$(echo "$val $4 $5" | bc)
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

ffmpeg(){
    $target_exec $target_path/ffmpeg "$@"
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
    outfile=tests/data/regression/$2/$t
    ${base}/${1}-regression.sh $t $2 $3 "$target_exec" "$target_path"
}

codectest(){
    regtest codec $1 tests/$1
}

lavftest(){
    regtest lavf lavf tests/vsynth1
}

lavfitest(){
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

$command > "$outfile" 2>/dev/null || exit

if ! test -e "$ref"; then
    echo "reference file '$ref' not found"
    exit 1
fi

case $cmp in
    diff)   diff -u -w "$ref" "$outfile"            ;;
    oneoff) oneoff     "$ref" "$outfile" "$fuzz"    ;;
    stddev) stddev     "$ref" "$outfile" "$fuzz"    ;;
esac

test $? = 0 && rm $outfile
