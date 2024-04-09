#!/usr/bin/env perl

# This script will print the dependency of a Texinfo file to stdout.
# texidep.pl <src-path> <input.texi> <output.ext>

use warnings;
use strict;

die unless @ARGV == 3;

my ($src_path, $root, $target) = @ARGV;

sub print_deps {
    my ($file, $deps) = @_;
    $deps->{$file} = 1;

    open(my $fh, "<", "$file") or die "Cannot open file '$file': $!";
    while (<$fh>) {
        if (my ($i) = /^\@(?:verbatim)?include\s+(\S+)/) {
            die "Circular dependency found in file $root\n" if exists $deps->{"doc/$1"};
            print "$target: doc/$1\n";

            # skip looking for config.texi dependencies, since it has
            # none, and is not located in the source tree
            if ("$1" ne "config.texi") {
                print_deps("$src_path/doc/$1", {%$deps});
            }
        }
    }
}

print_deps($root, {});
