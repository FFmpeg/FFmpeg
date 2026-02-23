#!/bin/sh
#
# Validate commit messages against FFmpeg conventions.
#
# Based on lint_commit_msg.py from mpv (LGPL).
# Original authors: Kacper Michajłow, Timo Rothenpieler
#
# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
# Exit code 0 = all pass, non-zero = errors found.
#
# Usage:
#   Single message from stdin:
#     git log -1 --format="%B" <commit> | sh tools/check_commit_msg.sh
#
#   Single message from file (pre-commit commit-msg hook):
#     sh tools/check_commit_msg.sh .git/COMMIT_EDITMSG
#
#   Multiple commits via revision range:
#     sh tools/check_commit_msg.sh HEAD~5..HEAD
#     sh tools/check_commit_msg.sh origin/master..my-branch
#     sh tools/check_commit_msg.sh -10          # last 10 commits
#
# Examples:
#   echo "avcodec/vvc: fix intra prediction" | sh tools/check_commit_msg.sh
#   # => commit message OK
#
#   echo "fix bug" | sh tools/check_commit_msg.sh
#   # => ERROR: subject does not match 'component: description' pattern: fix bug
#
# Checks (errors block, warnings only print):
#   - Subject matches "component[/module]: description" pattern   (error)
#   - No conventional-commits style (feat:/fix:/chore: etc.)      (error)
#   - Blank line between subject and body (if body exists)        (error)
#   - No duplicate Signed-off-by from same person                 (error)
#   - No multiple subject-like lines (2+) in body (squash-mess)   (error)
#   - Subject line <= 120 characters                              (warning)
#   - No trailing whitespace on subject line                      (warning)
#   - Subject does not end with a period                          (warning)
#   - Prefix does not contain file extension (.c, .h, etc.)       (warning)

RED='\033[1;31m'
YEL='\033[1;33m'
RST='\033[0m'

# --- Single-message validation (operates on a temp file) ---

check_message() {
    msgfile=$1
    err=0
    warn_count=0

    error() {
        printf "${RED}ERROR:${RST} %s\n" "$1" >&2
        err=1
    }

    warning() {
        printf "${YEL}WARNING:${RST} %s\n" "$1" >&2
        warn_count=$((warn_count + 1))
    }

    # Check for empty message
    if ! grep -q '[^[:space:]]' "$msgfile"; then
        error "commit message is empty"
        return $err
    fi

    # Split into subject (first line) and the rest
    subject=$(head -n 1 "$msgfile")

    # --- Subject checks ---

    # Trailing whitespace on subject (second pattern is a literal tab)
    case "$subject" in
        *" "|*"	")
            warning "trailing whitespace on subject line"
            ;;
    esac

    # Subject format: component/module: description
    # Allow nested paths like avcodec/vvc/inter: or single component like doc:
    # Also allow "Merge", "Revert" and "Reapply" subjects used by forges
    # Patterns supported:
    #   component: desc                           (avcodec/vvc: fix)
    #   component modifier: desc                  (avcodec/vvc decode: fix)
    #   component, component: desc                (avformat/a, avcodec/b: fix)
    #   .component: desc                          (.forgejo/CODEOWNERS: add)
    #   {component}: desc                         ({lib{a,b}/x86/,}Makefile: fix)
    # COMP: a component token starting with a letter, at least 2 characters.
    COMP='[a-zA-Z][]a-zA-Z0-9_./{},*?|()[-]+'
    case "$subject" in
        Merge\ *|Revert\ *|Reapply\ *)
            # Merge, revert and reapply commits get a pass on format checks
            ;;
        *)
            # Require a non-empty description after ": " so that bare
            # "component:" or "component: " (no text) are rejected.
            if ! printf '%s\n' "$subject" | grep -qE "^[{.]?${COMP}(, *${COMP})*( +[a-zA-Z0-9_]+)*: [^[:space:]]"; then
                error "subject does not match 'component: description' pattern: $subject"
            fi

            # Do not use conventional-commits style (feat:/fix:/chore:/refactor:)
            if printf '%s\n' "$subject" | grep -qEi '^(feat|fix|chore|refactor)[!:(]'; then
                error "do not use conventional-commits style (feat:/fix:/chore:/refactor:)"
            fi

            # Prefix should not include file extension
            prefix=$(printf '%s\n' "$subject" | sed -n 's/: .*//p')
            if printf '%s\n' "$prefix" | grep -qE '[a-z0-9]\.(c|h|m|texi)$'; then
                warning "prefix should not include file extension: $prefix"
            fi
            ;;
    esac

    # Subject should not end with a period
    case "$subject" in
        *.)
            warning "subject should not end with a period"
            ;;
    esac

    # Subject length
    subj_len=${#subject}
    if [ "$subj_len" -gt 120 ]; then
        warning "subject is $subj_len characters (> 120)"
    fi

    # --- Blank line between subject and body ---
    # Use sed to check line 2 directly; works regardless of trailing newline.
    second_line=$(sed -n '2p' "$msgfile")
    rest=$(sed -n '2,$p' "$msgfile")
    if [ -n "$rest" ]; then
        if [ -n "$second_line" ]; then
            error "missing blank line between subject and body"
        fi

        # Body is everything after the blank separator line
        body=$(tail -n +3 "$msgfile")
    else
        body=""
    fi

    # --- Squash-mess detection ---

    if [ -n "$body" ]; then
        # Multiple Signed-off-by from the same person
        sob_dups=$(printf '%s\n' "$body" | grep -i '^Signed-off-by:' | sort | uniq -d)
        if [ -n "$sob_dups" ]; then
            error "duplicate Signed-off-by: $(printf '%s\n' "$sob_dups" | head -n 1)"
        fi

        # Heuristic squash detection: count body lines that look like
        # additional commit subjects of the form "component/module: text".
        # Intentionally restricted to prefixes that contain '/' so that
        # prose definitions (e.g. "maximum: No restriction") and data
        # labels are not flagged as subjects; this trades recall for
        # precision and will not catch squash bodies made entirely of
        # single-component subjects like "doc: ..." or "configure: ...".
        # Match any non-space after ": " so lowercase subjects are counted too.
        # Exclude known trailer tags.
        subj_like_count=$(printf '%s\n' "$body" \
            | grep -E '^[a-zA-Z][a-zA-Z0-9_./-]*/[a-zA-Z0-9_./-]+: [^[:space:]]' \
            | grep -ivcE '^(Signed-off-by|Reviewed-by|Acked-by|Tested-by|CC|Reported-by|Co-authored-by|Link|Fixes|Note|Suggested-by|Bug):')
        if [ "$subj_like_count" -ge 2 ]; then
            error "body contains $subj_like_count subject-like lines (squash-mess?)"
        fi
    fi

    if [ $err -eq 0 ] && [ $warn_count -eq 0 ]; then
        printf "commit message OK\n"
    fi

    return $err
}

# --- Main ---

if [ $# -eq 0 ]; then
    # No argument: read a single message from stdin
    tmpfile=$(mktemp)
    trap 'rm -f "$tmpfile"' EXIT
    cat > "$tmpfile"
    check_message "$tmpfile"
    exit $?
elif [ $# -eq 1 ] && [ -f "$1" ]; then
    # Single argument is an existing file: treat as commit message file
    # (used by pre-commit commit-msg hook passing .git/COMMIT_EDITMSG)
    check_message "$1"
    exit $?
else
    # Argument(s) provided: treat as git revision range.
    # Store SHAs in a temp file and iterate via 'while read' rather than
    # 'for sha in $revs', which depends on unquoted word-splitting and is
    # not portable to shells that don't enable SH_WORD_SPLIT by default.
    tmpfile=$(mktemp)
    revsfile=$(mktemp)
    trap 'rm -f "$tmpfile" "$revsfile"' EXIT

    if ! git log --format=%H "$@" > "$revsfile" 2>/dev/null; then
        printf "${RED}ERROR:${RST} invalid revision range: %s\n" "$*" >&2
        exit 1
    fi
    if [ ! -s "$revsfile" ]; then
        printf "no commits in range: %s\n" "$*"
        exit 0
    fi

    total=0
    failures=0

    while IFS= read -r sha; do
        total=$((total + 1))
        printf '\n--- %s: %s ---\n' "$sha" "$(git log -1 --format=%s "$sha")"
        git log -1 --format="%B" "$sha" > "$tmpfile"
        if ! check_message "$tmpfile"; then
            failures=$((failures + 1))
        fi
    done < "$revsfile"

    printf '\n--- Result: %d/%d passed ---\n' "$((total - failures))" "$total"
    [ "$failures" -eq 0 ]
    exit $?
fi
