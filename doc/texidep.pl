#! /usr/bin/perl

use warnings;
use strict;

my ($root, $target) = @ARGV;

sub print_deps {
    my ($file, $deps) = @_;
    $deps->{$file} = 1;

    open(my $fh, "$file") or die "Cannot open file '$file': $!";
    while (<$fh>) {
        /^@(?:verbatim)?include\s+(\S+)/ and do {
            die "Circular dependency found in file $root\n" if exists $deps->{"doc/$1"};
            print "$target: doc/$1\n";
            print_deps("doc/$1", {%$deps});
        }
    }
}

print_deps($root, {});
