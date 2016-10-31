#!/bin/sh

if [ "$1" != "merge" -a "$1" != "noop" ]; then
    printf "Usage: $0 <merge|noop [REF_HASH]>\n"
    exit 0
fi

[ "$1" = "noop" ] && merge_opts="-s ours"

nextrev=$(git rev-list libav/master --not master --no-merges | tail -n1)
if [ -z "$nextrev" ]; then
    printf "Nothing to merge..\n"
    exit 0
fi
printf "Merging $(git log -n 1 --oneline $nextrev)\n"
git merge --no-commit $merge_opts --no-ff --log $nextrev

if [ "$1" = "noop" -a -n "$2" ]; then
    printf "\nThis commit is a noop, see $2\n" >> .git/MERGE_MSG
fi

printf "\nMerged-by: $(git config --get user.name) <$(git config --get user.email)>\n" >> .git/MERGE_MSG
