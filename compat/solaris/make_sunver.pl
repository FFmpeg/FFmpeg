#!/usr/bin/env perl

# make_sunver.pl
#
#   Copyright (C) 2010, 2011, 2012, 2013
#   Free Software Foundation, Inc.
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.GPLv3.  If not see
# <http://www.gnu.org/licenses/>.

# This script takes at least two arguments, a GNU style version script and
# a list of object and archive files, and generates a corresponding Sun
# style version script as follows:
#
# Each glob pattern, C++ mangled pattern or literal in the input script is
# matched against all global symbols in the input objects, emitting those
# that matched (or nothing if no match was found).
# A comment with the original pattern and its type is left in the output
# file to make it easy to understand the matches.
#
# It uses elfdump when present (native), GNU readelf otherwise.
# It depends on the GNU version of c++filt, since it must understand the
# GNU mangling style.

use FileHandle;
use IPC::Open2;

# Enforce C locale.
$ENV{'LC_ALL'} = "C";
$ENV{'LANG'} = "C";

# Input version script, GNU style.
my $symvers = shift;

##########
# Get all the symbols from the library, match them, and add them to a hash.

my %sym_hash = ();

# List of objects and archives to process.
my @OBJECTS = ();

# List of shared objects to omit from processing.
my @SHAREDOBJS = ();

# Filter out those input archives that have corresponding shared objects to
# avoid adding all symbols matched in the archive to the output map.
foreach $file (@ARGV) {
    if (($so = $file) =~ s/\.a$/.so/ && -e $so) {
    printf STDERR "omitted $file -> $so\n";
    push (@SHAREDOBJS, $so);
    } else {
    push (@OBJECTS, $file);
    }
}

# We need to detect and ignore hidden symbols.  Solaris nm can only detect
# this in the harder to parse default output format, and GNU nm not at all,
# so use elfdump -s in the native case and GNU readelf -s otherwise.
# GNU objdump -t cannot be used since it produces a variable number of
# columns.

# The path to elfdump.
my $elfdump = "/usr/ccs/bin/elfdump";

if (-f $elfdump) {
    open ELFDUMP,$elfdump.' -s '.(join ' ',@OBJECTS).'|' or die $!;
    my $skip_arsym = 0;

    while (<ELFDUMP>) {
    chomp;

    # Ignore empty lines.
    if (/^$/) {
        # End of archive symbol table, stop skipping.
        $skip_arsym = 0 if $skip_arsym;
        next;
    }

    # Keep skipping until end of archive symbol table.
    next if ($skip_arsym);

    # Ignore object name header for individual objects and archives.
    next if (/:$/);

    # Ignore table header lines.
    next if (/^Symbol Table Section:/);
    next if (/index.*value.*size/);

    # Start of archive symbol table: start skipping.
    if (/^Symbol Table: \(archive/) {
        $skip_arsym = 1;
        next;
    }

    # Split table.
    (undef, undef, undef, undef, $bind, $oth, undef, $shndx, $name) = split;

    # Error out for unknown input.
    die "unknown input line:\n$_" unless defined($bind);

    # Ignore local symbols.
    next if ($bind eq "LOCL");
    # Ignore hidden symbols.
    next if ($oth eq "H");
    # Ignore undefined symbols.
    next if ($shndx eq "UNDEF");
    # Error out for unhandled cases.
    if ($bind !~ /^(GLOB|WEAK)/ or $oth ne "D") {
        die "unhandled symbol:\n$_";
    }

    # Remember symbol.
    $sym_hash{$name}++;
    }
    close ELFDUMP or die "$elfdump error";
} else {
    open READELF, 'readelf -s -W '.(join ' ',@OBJECTS).'|' or die $!;
    # Process each symbol.
    while (<READELF>) {
    chomp;

    # Ignore empty lines.
    next if (/^$/);

    # Ignore object name header.
    next if (/^File: .*$/);

    # Ignore table header lines.
    next if (/^Symbol table.*contains.*:/);
    next if (/Num:.*Value.*Size/);

    # Split table.
    (undef, undef, undef, undef, $bind, $vis, $ndx, $name) = split;

    # Error out for unknown input.
    die "unknown input line:\n$_" unless defined($bind);

    # Ignore local symbols.
    next if ($bind eq "LOCAL");
    # Ignore hidden symbols.
    next if ($vis eq "HIDDEN");
    # Ignore undefined symbols.
    next if ($ndx eq "UND");
    # Error out for unhandled cases.
    if ($bind !~ /^(GLOBAL|WEAK)/ or $vis ne "DEFAULT") {
        die "unhandled symbol:\n$_";
    }

    # Remember symbol.
    $sym_hash{$name}++;
    }
    close READELF or die "readelf error";
}

##########
# The various types of glob patterns.
#
# A glob pattern that is to be applied to the demangled name: 'cxx'.
# A glob patterns that applies directly to the name in the .o files: 'glob'.
# This pattern is ignored; used for local variables (usually just '*'): 'ign'.

# The type of the current pattern.
my $glob = 'glob';

# We're currently inside `extern "C++"', which Sun ld doesn't understand.
my $in_extern = 0;

# The c++filt command to use.  This *must* be GNU c++filt; the Sun Studio
# c++filt doesn't handle the GNU mangling style.
my $cxxfilt = $ENV{'CXXFILT'} || "c++filt";

# The current version name.
my $current_version = "";

# Was there any attempt to match a symbol to this version?
my $matches_attempted;

# The number of versions which matched this symbol.
my $matched_symbols;

open F,$symvers or die $!;

# Print information about generating this file
print "# This file was generated by make_sunver.pl.  DO NOT EDIT!\n";
print "# It was generated by:\n";
printf "# %s %s %s\n", $0, $symvers, (join ' ',@ARGV);
printf "# Omitted archives with corresponding shared libraries: %s\n",
    (join ' ', @SHAREDOBJS) if $#SHAREDOBJS >= 0;
print "#\n\n";

print "\$mapfile_version 2\n";

while (<F>) {
    # Lines of the form '};'
    if (/^([ \t]*)(\}[ \t]*;[ \t]*)$/) {
    $glob = 'glob';
    if ($in_extern) {
        $in_extern--;
        print "$1##$2\n";
    } else {
        print;
    }
    next;
    }

    # Lines of the form '} SOME_VERSION_NAME_1.0;'
    if (/^[ \t]*\}[ \tA-Z0-9_.a-z]+;[ \t]*$/) {
    $glob = 'glob';
    # We tried to match symbols agains this version, but none matched.
    # Emit dummy hidden symbol to avoid marking this version WEAK.
    if ($matches_attempted && $matched_symbols == 0) {
        print "  hidden:\n";
        print "    .force_WEAK_off_$current_version = DATA S0x0 V0x0;\n";
    }
    print; next;
    }

    # Comment and blank lines
    if (/^[ \t]*\#/) { print; next; }
    if (/^[ \t]*$/) { print; next; }

    # Lines of the form '{'
    if (/^([ \t]*){$/) {
    if ($in_extern) {
        print "$1##{\n";
    } else {
        print;
    }
    next;
    }

    # Lines of the form 'SOME_VERSION_NAME_1.1 {'
    if (/^([A-Z0-9_.]+)[ \t]+{$/) {
    # Record version name.
    $current_version = $1;
    # Reset match attempts, #matched symbols for this version.
    $matches_attempted = 0;
    $matched_symbols = 0;
    print "SYMBOL_VERSION $1 {\n";
    next;
    }

    # Ignore 'global:'
    if (/^[ \t]*global:$/) { print; next; }

    # After 'local:', globs should be ignored, they won't be exported.
    if (/^[ \t]*local:$/) {
    $glob = 'ign';
    print;
    next;
    }

    # After 'extern "C++"', globs are C++ patterns
    if (/^([ \t]*)(extern \"C\+\+\"[ \t]*)$/) {
    $in_extern++;
    $glob = 'cxx';
    # Need to comment, Sun ld cannot handle this.
    print "$1##$2\n"; next;
    }

    # Chomp newline now we're done with passing through the input file.
    chomp;

    # Catch globs.  Note that '{}' is not allowed in globs by this script,
    # so only '*' and '[]' are available.
    if (/^([ \t]*)([^ \t;{}#]+);?[ \t]*$/) {
    my $ws = $1;
    my $ptn = $2;
    # Turn the glob into a regex by replacing '*' with '.*', '?' with '.'.
    # Keep $ptn so we can still print the original form.
    ($pattern = $ptn) =~ s/\*/\.\*/g;
    $pattern =~ s/\?/\./g;

    if ($glob eq 'ign') {
        # We're in a local: * section; just continue.
        print "$_\n";
        next;
    }

    # Print the glob commented for human readers.
    print "$ws##$ptn ($glob)\n";
    # We tried to match a symbol to this version.
    $matches_attempted++;

    if ($glob eq 'glob') {
        my %ptn_syms = ();

        # Match ptn against symbols in %sym_hash.
        foreach my $sym (keys %sym_hash) {
        # Maybe it matches one of the patterns based on the symbol in
        # the .o file.
        $ptn_syms{$sym}++ if ($sym =~ /^$pattern$/);
        }

        foreach my $sym (sort keys(%ptn_syms)) {
        $matched_symbols++;
        print "$ws$sym;\n";
        }
    } elsif ($glob eq 'cxx') {
        my %dem_syms = ();

        # Verify that we're actually using GNU c++filt.  Other versions
        # most likely cannot handle GNU style symbol mangling.
        my $cxxout = `$cxxfilt --version 2>&1`;
        $cxxout =~ m/GNU/ or die "$0 requires GNU c++filt to function";

        # Talk to c++filt through a pair of file descriptors.
        # Need to start a fresh instance per pattern, otherwise the
        # process grows to 500+ MB.
        my $pid = open2(*FILTIN, *FILTOUT, $cxxfilt) or die $!;

        # Match ptn against symbols in %sym_hash.
        foreach my $sym (keys %sym_hash) {
        # No?  Well, maybe its demangled form matches one of those
        # patterns.
        printf FILTOUT "%s\n",$sym;
        my $dem = <FILTIN>;
        chomp $dem;
        $dem_syms{$sym}++ if ($dem =~ /^$pattern$/);
        }

        close FILTOUT or die "c++filt error";
        close FILTIN or die "c++filt error";
        # Need to wait for the c++filt process to avoid lots of zombies.
        waitpid $pid, 0;

        foreach my $sym (sort keys(%dem_syms)) {
        $matched_symbols++;
        print "$ws$sym;\n";
        }
    } else {
        # No?  Well, then ignore it.
    }
    next;
    }
    # Important sanity check.  This script can't handle lots of formats
    # that GNU ld can, so be sure to error out if one is seen!
    die "strange line `$_'";
}
close F;
