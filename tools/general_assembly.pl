#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(strftime);
use Encode qw(decode);
use Data::Dumper;
use Getopt::Long;

binmode(STDOUT, ":utf8");

sub trim { my $s = shift; $s =~ s/^\s+|\s+$//g; return $s };

sub print_help {
    print "Usage: $0 [options]\n";
    print "Options:\n";
    print "  --names          Print only the names\n";
    print "  --emails         Print only the email addresses\n";
    print "  --full           Print both names and email addresses (default)\n";
    print "  -h, --help       Show this help message\n";
    exit;
}

my $print_full = 1;
my $print_names = 0;
my $print_emails = 0;
my $help = 0;

GetOptions(
    "full" => \$print_full,
    "names" => \$print_names,
    "emails" => \$print_emails,
    "help" => \$help,
    "h" => \$help,
);

print_help() if $help;

if ($print_names || $print_emails) {
    $print_full = 0;
}

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
foreach my $name (sort keys %assembly) {
    my $email = $assembly{$name};
    if ($print_full) {
        printf("%s <%s>\n", $name, $email);
    } elsif ($print_names) {
        printf("%s\n", $name);
    } elsif ($print_emails) {
        printf("%s\n", $email);
    }
}
