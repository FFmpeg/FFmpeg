#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(strftime);
use Encode qw(decode);
use Data::Dumper;

sub trim { my $s = shift; $s =~ s/^\s+|\s+$//g; return $s };

my @shortlog = split /\n/, decode('UTF-8', `git log --pretty=format:"%aN <%aE>" --since="last 36 months" | sort | uniq -c | sort -r`, Encode::FB_CROAK);
my %assembly = ();

foreach my $line (@shortlog) {
    my ($count, $name, $email) = $line =~ m/^ *(\d+) *(.*?) <(.*?)>/;
    if ($count < 20) {
        next;
    }

    $name = trim $name;
    if ($count < 50) {
        my $true = 0;
        my @commits = split /(^|\n)commit [a-z0-9]{40}(\n|$)/, decode('UTF-8', `git log --name-only --use-mailmap --author="$email" --since="last 36 months"`, Encode::FB_CROAK);
        foreach my $commit (@commits) {
                $true++; # if ($commit =~ /\n[\w\/]+\.(c|h|S|asm|texi)/);
        }

        if ($true < 20) {
            next;
        }
    }

    $assembly{$name} = $email;
}

printf("# %s %s", strftime("%Y-%m-%d", localtime), decode('UTF-8', `git rev-parse HEAD`, Encode::FB_CROAK));
foreach my $email (sort values %assembly) {
    printf("%s\n", $email);
}
