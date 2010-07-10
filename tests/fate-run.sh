#! /bin/sh

base=$(dirname $0)
. "${base}/md5.sh"

test="${1#fate-}"
SAMPLES_PATH=$2
target_exec=$3
BUILD_PATH=$4
command=$5
cmp=${6:-diff}

ref=${7:-"${base}/ref/fate/${test}"}
fuzz=$8
outdir="tests/data/fate"
outfile="${outdir}/${test}"

oneoff(){
    psnr=$(tests/tiny_psnr "$1" "$2" 2 0 0)
    max=$(expr "$psnr" : '.*MAXDIFF: *\([0-9]*\)')
    size1=$(expr "$psnr" : '.*bytes: *\([0-9]*\)')
    size2=$(expr "$psnr" : '.*bytes:[ 0-9]*/ *\([0-9]*\)')
    if [ $max -gt ${3:-1} ] || [ $size1 != $size2 ]; then
        echo "$psnr"
        return 1
    fi
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
esac

test $? = 0 && rm $outfile
