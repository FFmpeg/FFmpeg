#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(strftime);
use Encode qw(decode);
use Data::Dumper;
use Getopt::Long;
use Digest::SHA;
use utf8;

use DateTime;
use DateTime::Format::ISO8601;

sub trim { my $s = shift; $s =~ s/^\s+|\s+$//g; return $s };

sub print_help {
    print "Usage: $0 [options]\n";
    print "Options:\n";
    print "  --names          Print only the names\n";
    print "  --emails         Print only the email addresses\n";
    print "  --full           Print both names and email addresses (default)\n";
    print "  --date YY-MM-DD  Generate the GA for a given date, defaults to current system time\n";
    print "  -h, --help       Show this help message\n";
    exit;
}

my $print_full = 1;
my $print_names = 0;
my $print_emails = 0;
my $date         = DateTime->now()->iso8601;
my $help = 0;

GetOptions(
    "full" => \$print_full,
    "names" => \$print_names,
    "emails" => \$print_emails,
    "help" => \$help,
    "date=s" => \$date,
    "h" => \$help,
);

print_help() if $help;

if ($print_names || $print_emails) {
    $print_full = 0;
}

sub get_date_range {
    my ($now) = @_;

    # date on which the GA update rule was established, and the voter list
    # was extraordinarily updated; cf.:
    # * http://lists.ffmpeg.org/pipermail/ffmpeg-devel/2023-October/316054.html
    #   Message-Id <169818211998.11195.16532637803201641594@lain.khirnov.net>
    # * http://lists.ffmpeg.org/pipermail/ffmpeg-devel/2023-November/316618.html
    #   Message-Id <5efcab06-8510-4226-bf18-68820c7c69ba@betaapp.fastmail.com>
    my $date_ga_rule       = DateTime->new(year => 2023, month => 11, day => 06);
    # date when the regular update rule is first applied
    my $date_first_regular = DateTime->new(year => 2024);

    if ($now->is_between($date_ga_rule, $date_first_regular)) {
        return ($date_ga_rule->clone()->set_year($date_ga_rule->year - 3), $date_ga_rule);
    }

    if ($now < $date_ga_rule) {
        print STDERR  "GA before $date_ga_rule is not well-defined, be very careful with the output\n";
    }

    my $cur_year_jan  = $now->clone()->truncate(to => "year");
    my $cur_year_jul  = $cur_year_jan->clone()->set_month(7);
    my $date_until    = $now > $cur_year_jul ? $cur_year_jul : $cur_year_jan;
    my $date_since    = $date_until->clone()->set_year($date_until->year - 3);

    return ($date_since, $date_until);
}

my ($since, $until) = get_date_range(DateTime::Format::ISO8601->parse_datetime($date));

my @shortlog = split /\n/, decode('UTF-8',
    `git log --pretty=format:"%aN <%aE>" --since="$since" --until="$until" | sort | uniq -c | sort -r`,
    Encode::FB_CROAK);
my %assembly = ();

foreach my $line (@shortlog) {
    my ($count, $name, $email) = $line =~ m/^ *(\d+) *(.*?) <(.*?)>/;
    if ($count < 20) {
        next;
    }

    $name = trim $name;
    if ($count < 50) {
        my $true = 0;
        my @commits = split /(^|\n)commit [a-z0-9]{40}(\n|$)/,
            decode('UTF-8',
                   `git log --name-only --use-mailmap --author="$email" --since="$since" --until="$until"`,
                   Encode::FB_CROAK);
        foreach my $commit (@commits) {
                $true++; # if ($commit =~ /\n[\w\/]+\.(c|h|S|asm|texi)/);
        }

        if ($true < 20) {
            next;
        }
    }

    $assembly{$name} = $email;
}

# generate the output string
my @out_lines;
foreach my $name (sort keys %assembly) {
    my $email = $assembly{$name};
    my $val;
    if ($print_full) {
        $val = sprintf("%s <%s>", $name, $email);
    } elsif ($print_names) {
        $val = $name;
    } elsif ($print_emails) {
        $val = $email;
    }
    push(@out_lines, ($val));
}
my $out_str = join("\n", @out_lines) . "\n";
utf8::encode($out_str);

printf("# GA for $since/$until; %d people; SHA256:%s\n%s",
       scalar @out_lines, Digest::SHA::sha256_hex($out_str), $out_str);
