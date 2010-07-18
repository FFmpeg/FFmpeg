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

# compatibility with Mike's test specs
SAMPLES_PATH=$samples
BUILD_PATH=$target_path

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

if ! test -e "$ref"; then
    echo "reference file '$ref' not found"
    exit 1
fi

mkdir -p "$outdir"

eval $target_exec $command > "$outfile" 2>/dev/null || exit

case $cmp in
    diff)   diff -u -w "$ref" "$outfile"            ;;
    oneoff) oneoff     "$ref" "$outfile" "$fuzz"    ;;
    stddev) stddev     "$ref" "$outfile" "$fuzz"    ;;
esac

test $? = 0 && rm $outfile
