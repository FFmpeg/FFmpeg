#!/usr/bin/env perl
#
# Copyright (c) 2025 Martin Storsjo
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# A script for reformatting ARM/AArch64 assembly according to the following
# style:
# - Instructions start after 8 columns, operands start after 24 columns
# - Vector register layouts and modifiers like "uxtw" are written in lowercase
# - Optionally align operand columns vertically according to their
#   maximum width (accommodating for e.g. x0 vs x10, or v0.8b vs v16.16b).
#
# The input code is passed to stdin, and the reformatted code is written
# on stdout.

use strict;

my $indent_operands = 0;
my $instr_indent = 8;
my $operand_indent = 24;
my $match_indent = 0;

while (@ARGV) {
    my $opt = shift;

    if ($opt eq "-operands") {
        $indent_operands = 1;
    } elsif ($opt eq "-indent") {
        $instr_indent = shift;
    } elsif ($opt eq "-operand-indent") {
        $operand_indent = shift;
    } elsif ($opt eq "-match-indent") {
        $match_indent = 1;
    } else {
        die "Unrecognized parameter $opt\n";
    }
}

if ($operand_indent < $instr_indent) {
    die "Can't indent operands to $operand_indent while indenting " .
        "instructions to $instr_indent\n";
}

# Return a string consisting of n spaces
sub spaces {
    my $n = $_[0];
    return " " x $n;
}

sub indentcolumns {
    my $input = $_[0];
    my $chars = $_[1];
    my @operands = split(/,/, $input);
    my $num = @operands;
    my $ret = "";
    for (my $i = 0; $i < $num; $i++) {
        my $cur = $operands[$i];
        # Trim out leading/trailing whitespace
        $cur =~ s/^\s+|\s+$//g;
        $ret .= $cur;
        if ($i + 1 < $num) {
            # If we have a following operand, add a comma and whitespace to
            # align the next operand.
            my $next = $operands[$i+1];
            my $len = length($cur);
            if ($len > $chars) {
                # If this operand was too wide for the intended column width,
                # don't try to realign the line at all, just return the input
                # untouched.
                return $input;
            }
            my $pad = $chars - $len;
            if ($next =~ /[su]xt[bhw]|[la]s[lr]/) {
                # If the next item isn't a regular operand, but a modifier,
                # don't try to align that. E.g. "add x0,  x0,  w1, uxtw #1".
                $pad = 0;
            }
            $ret .= "," . spaces(1 + $pad);
        }
    }
    return $ret;
}

# Realign the operands part of an instruction line, making each operand
# take up the maximum width for that kind of operand.
sub columns {
    my $rest = $_[0];
    if ($rest !~ /,/) {
        # No commas, no operands to split and align
        return $rest;
    }
    if ($rest =~ /{|[^\w]\[/) {
        # Check for instructions that use register ranges, like {v0.8b,v1.8b}
        # or mem address operands, like "ldr x0, [sp]" - we skip trying to
        # realign these.
        return $rest;
    }
    if ($rest =~ /v[0-9]+\.[0-9]+[bhsd]/) {
        # If we have references to aarch64 style vector registers, like
        # v0.8b, then align all operands to the maximum width of such
        # operands - v16.16b.
        #
        # TODO: Ideally, we'd handle mixed operand types individually.
        return indentcolumns($rest, 7);
    }
    # Indent operands according to the maximum width of regular registers,
    # like x10.
    return indentcolumns($rest, 3);
}

while (<STDIN>) {
    # Trim off trailing whitespace.
    chomp;
    if (/^([\.\w\d]+:)?(\s+)([\w\\][\w\\\.]*)(?:(\s+)(.*)|$)/) {
        my $label = $1;
        my $indent = $2;
        my $instr = $3;
        my $origspace = $4;
        my $rest = $5;

        my $orig_operand_indent = length($label) + length($indent) +
                                  length($instr) + length($origspace);

        if ($indent_operands) {
            $rest = columns($rest);
        }

        my $size = $instr_indent;
        if ($match_indent) {
            # Try to check the current attempted indent size and normalize
            # to it; match existing ident sizes of 4, 8, 10 and 12 columns.
            my $cur_indent = length($label) + length($indent);
            if ($cur_indent >= 3 && $cur_indent <= 5) {
                $size = 4;
            } elsif ($cur_indent >= 7 && $cur_indent <= 9) {
                $size = 8;
            } elsif ($cur_indent == 10 || $cur_indent == 12) {
                $size = $cur_indent;
            }
        }
        if (length($label) >= $size) {
            # Not enough space for the label; just add a space between the label
            # and the instruction.
            $indent = " ";
        } else {
            $indent = spaces($size - length($label));
        }

        my $instr_end = length($label) + length($indent) + length($instr);
        $size = $operand_indent - $instr_end;
        if ($match_indent) {
            # Check how the operands currently seem to be indented.
            my $cur_indent = $orig_operand_indent;
            if ($cur_indent >= 11 && $cur_indent <= 13) {
                $size = 12;
            } elsif ($cur_indent >= 14 && $cur_indent <= 17) {
                $size = 16;
            } elsif ($cur_indent >= 18 && $cur_indent <= 22) {
                $size = 20;
            } elsif ($cur_indent >= 23 && $cur_indent <= 27) {
                $size = 24;
            }
            $size -= $instr_end;
        }
        my $operand_space = " ";
        if ($size > 0) {
            $operand_space = spaces($size);
        }

        # Lowercase the aarch64 vector layout description, .8B -> .8b
        $rest =~ s/(\.[84216]*[BHSD])/lc($1)/ge;
        # Lowercase modifiers like "uxtw" or "lsl"
        $rest =~ s/([SU]XT[BWH]|[LA]S[LR])/lc($1)/ge;

        # Reassemble the line
        if ($rest eq "") {
            $_ = $label . $indent . $instr;
        } else {
            $_ = $label . $indent . $instr . $operand_space . $rest;
        }
    }
    print $_ . "\n";
}
