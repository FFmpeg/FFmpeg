#!/bin/sh

set -e

UPDATE_COMMIT=${1:-master}

cd $(dirname $0)/..

trim() {
    cd tests/checkasm/ext
    oldifs="$IFS"
    # Set IFS to contain only a newline, no regular space, to make the
    # for loop below properly handle file names with spaces.
    IFS="
"
    for i in $(ls -A); do
        case "$i" in
        include|src|LICENSE)
            # Keep these
            ;;
        *)
            if ! git rm -rf "$i"; then
                echo "Unable to trim out $i - untracked file?"
                exit 1
            fi
            ;;
        esac
    done
    # Remove the meson files from the include/src subdirectories.
    for i in $(find . -name meson.build); do
        if ! git rm -f "$i"; then
            echo "Unable to trim out $i - untracked file?"
            exit 1
        fi
    done
    IFS="$oldifs"
}

git update-index --refresh
if ! git diff-index --quiet HEAD --; then
    echo Working tree has modifications - aborting.
    exit 1
fi

orig_commit=$(git rev-parse HEAD)
if git subtree pull --squash --prefix=tests/checkasm/ext \
   -m "checkasm: Update from upstream" \
   https://code.ffmpeg.org/FFmpeg/checkasm.git ${UPDATE_COMMIT}; then
    # Successfully merged updates (or no updates at all).
    new_commit=$(git rev-parse HEAD)
    trim
    if [ "$orig_commit" = "$new_commit" ]; then
        # Nothing merged from upstream
        git commit -m "checkasm: Trim out unused upstream files"
    else
        # Merged new changes; amend the results from trimming the subtree,
        # in case upstream added new files we don't need.
        git commit --amend --no-edit
    fi
else
    # Failed merge - possibly due to conflicts, due to updates to files that
    # we have trimmed out. Try removing those files again, with "git rm",
    # and check if that was enough to resolve the conflicts.
    trim
    if ! git commit; then
        echo "Committing trimmed output failed - are there remaining merge conflicts?"
        exit 1
    else
        echo "Resolved conflicts by re-trimming out files."
    fi
fi
