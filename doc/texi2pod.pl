#!/usr/bin/env perl

#   Copyright (C) 1999, 2000, 2001 Free Software Foundation, Inc.

# This file is part of GNU CC.

# GNU CC is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# GNU CC is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with GNU CC; see the file COPYING.  If not, write to
# the Free Software Foundation, 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA

# This does trivial (and I mean _trivial_) conversion of Texinfo
# markup to Perl POD format.  It's intended to be used to extract
# something suitable for a manpage from a Texinfo document.

use warnings;

$output = 0;
$skipping = 0;
%sects = ();
@sects_sequence = ();
$section = "";
@icstack = ();
@endwstack = ();
@skstack = ();
@instack = ();
$shift = "";
%defs = ();
$fnno = 1;
$inf = "";
@ibase = ();

while ($_ = shift) {
    if (/^-D(.*)$/) {
        if ($1 ne "") {
            $flag = $1;
        } else {
            $flag = shift;
        }
        $value = "";
        ($flag, $value) = ($flag =~ /^([^=]+)(?:=(.+))?/);
        die "no flag specified for -D\n"
            unless $flag ne "";
        die "flags may only contain letters, digits, hyphens, dashes and underscores\n"
            unless $flag =~ /^[a-zA-Z0-9_-]+$/;
        $defs{$flag} = $value;
    } elsif (/^-I(.*)$/) {
        push @ibase, $1 ne "" ? $1 : shift;
    } elsif (/^-/) {
        usage();
    } else {
        $in = $_, next unless defined $in;
        $out = $_, next unless defined $out;
        usage();
    }
}

push @ibase, ".";

if (defined $in) {
    $inf = gensym();
    open($inf, "<$in") or die "opening \"$in\": $!\n";
    push @ibase, $1 if $in =~ m|^(.+)/[^/]+$|;
} else {
    $inf = \*STDIN;
}

if (defined $out) {
    open(STDOUT, ">$out") or die "opening \"$out\": $!\n";
}

while(defined $inf) {
INF: while(<$inf>) {
    # Certain commands are discarded without further processing.
    /^\@(?:
         [a-z]+index            # @*index: useful only in complete manual
         |need                  # @need: useful only in printed manual
         |(?:end\s+)?group      # @group .. @end group: ditto
         |page                  # @page: ditto
         |node                  # @node: useful only in .info file
         |(?:end\s+)?ifnottex   # @ifnottex .. @end ifnottex: use contents
        )\b/x and next;

    chomp;

    # Look for filename and title markers.
    /^\@setfilename\s+([^.]+)/ and $fn = $1, next;
    /^\@settitle\s+([^.]+)/ and $tl = postprocess($1), next;

    # Identify a man title but keep only the one we are interested in.
    /^\@c\s+man\s+title\s+([A-Za-z0-9-]+)\s+(.+)/ and do {
        if (exists $defs{$1}) {
            $fn = $1;
            $tl = postprocess($2);
        }
        next;
    };

    /^\@include\s+(.+)$/ and do {
        push @instack, $inf;
        $inf = gensym();

        for (@ibase) {
            open($inf, "<" . $_ . "/" . $1) and next INF;
        }
        die "cannot open $1: $!\n";
    };

    # Look for blocks surrounded by @c man begin SECTION ... @c man end.
    # This really oughta be @ifman ... @end ifman and the like, but such
    # would require rev'ing all other Texinfo translators.
    /^\@c\s+man\s+begin\s+([A-Za-z ]+)/ and $sect = $1, push (@sects_sequence, $sect), $output = 1, next;
    /^\@c\s+man\s+end/ and do {
        $sects{$sect} = "" unless exists $sects{$sect};
        $sects{$sect} .= postprocess($section);
        $section = "";
        $output = 0;
        next;
    };

    # handle variables
    /^\@set\s+([a-zA-Z0-9_-]+)\s*(.*)$/ and do {
        $defs{$1} = $2;
        next;
    };
    /^\@clear\s+([a-zA-Z0-9_-]+)/ and do {
        delete $defs{$1};
        next;
    };

    next unless $output;

    # Discard comments.  (Can't do it above, because then we'd never see
    # @c man lines.)
    /^\@c\b/ and next;

    # End-block handler goes up here because it needs to operate even
    # if we are skipping.
    /^\@end\s+([a-z]+)/ and do {
        # Ignore @end foo, where foo is not an operation which may
        # cause us to skip, if we are presently skipping.
        my $ended = $1;
        next if $skipping && $ended !~ /^(?:ifset|ifclear|ignore|menu|iftex)$/;

        die "\@end $ended without \@$ended at line $.\n" unless defined $endw;
        die "\@$endw ended by \@end $ended at line $.\n" unless $ended eq $endw;

        $endw = pop @endwstack;

        if ($ended =~ /^(?:ifset|ifclear|ignore|menu|iftex)$/) {
            $skipping = pop @skstack;
            next;
        } elsif ($ended =~ /^(?:example|smallexample|display)$/) {
            $shift = "";
            $_ = "";        # need a paragraph break
        } elsif ($ended =~ /^(?:itemize|enumerate|(?:multi|[fv])?table)$/) {
            $_ = "\n=back\n";
            $ic = pop @icstack;
        } else {
            die "unknown command \@end $ended at line $.\n";
        }
    };

    # We must handle commands which can cause skipping even while we
    # are skipping, otherwise we will not process nested conditionals
    # correctly.
    /^\@ifset\s+([a-zA-Z0-9_-]+)/ and do {
        push @endwstack, $endw;
        push @skstack, $skipping;
        $endw = "ifset";
        $skipping = 1 unless exists $defs{$1};
        next;
    };

    /^\@ifclear\s+([a-zA-Z0-9_-]+)/ and do {
        push @endwstack, $endw;
        push @skstack, $skipping;
        $endw = "ifclear";
        $skipping = 1 if exists $defs{$1};
        next;
    };

    /^\@(ignore|menu|iftex)\b/ and do {
        push @endwstack, $endw;
        push @skstack, $skipping;
        $endw = $1;
        $skipping = 1;
        next;
    };

    next if $skipping;

    # Character entities.  First the ones that can be replaced by raw text
    # or discarded outright:
    s/\@copyright\{\}/(c)/g;
    s/\@dots\{\}/.../g;
    s/\@enddots\{\}/..../g;
    s/\@([.!? ])/$1/g;
    s/\@[:-]//g;
    s/\@bullet(?:\{\})?/*/g;
    s/\@TeX\{\}/TeX/g;
    s/\@pounds\{\}/\#/g;
    s/\@minus(?:\{\})?/-/g;
    s/\\,/,/g;

    # Now the ones that have to be replaced by special escapes
    # (which will be turned back into text by unmunge())
    s/&/&amp;/g;
    s/\@\{/&lbrace;/g;
    s/\@\}/&rbrace;/g;
    s/\@\@/&at;/g;

    # Inside a verbatim block, handle @var specially.
    if ($shift ne "") {
        s/\@var\{([^\}]*)\}/<$1>/g;
    }

    # POD doesn't interpret E<> inside a verbatim block.
    if ($shift eq "") {
        s/</&lt;/g;
        s/>/&gt;/g;
    } else {
        s/</&LT;/g;
        s/>/&GT;/g;
    }

    # Single line command handlers.

    /^\@(?:section|unnumbered|unnumberedsec|center|heading)\s+(.+)$/
        and $_ = "\n=head2 $1\n";
    /^\@(?:subsection|subheading)\s+(.+)$/
        and $_ = "\n=head3 $1\n";
    /^\@(?:subsubsection|subsubheading)\s+(.+)$/
        and $_ = "\n=head4 $1\n";

    # Block command handlers:
    /^\@itemize\s*(\@[a-z]+|\*|-)?/ and do {
        push @endwstack, $endw;
        push @icstack, $ic;
        $ic = $1 ? $1 : "*";
        $_ = "\n=over 4\n";
        $endw = "itemize";
    };

    /^\@enumerate(?:\s+([a-zA-Z0-9]+))?/ and do {
        push @endwstack, $endw;
        push @icstack, $ic;
        if (defined $1) {
            $ic = $1 . ".";
        } else {
            $ic = "1.";
        }
        $_ = "\n=over 4\n";
        $endw = "enumerate";
    };

    /^\@((?:multi|[fv])?table)\s+(\@[a-z]+)/ and do {
        push @endwstack, $endw;
        push @icstack, $ic;
        $endw = $1;
        $ic = $2;
        $ic =~ s/\@(?:samp|strong|key|gcctabopt|option|env)/B/;
        $ic =~ s/\@(?:code|kbd)/C/;
        $ic =~ s/\@(?:dfn|var|emph|cite|i)/I/;
        $ic =~ s/\@(?:file)/F/;
        $ic =~ s/\@(?:columnfractions)//;
        $_ = "\n=over 4\n";
    };

    /^\@(multitable)\s+{.*/ and do {
        push @endwstack, $endw;
        push @icstack, $ic;
        $endw = $1;
        $ic = "";
        $_ = "\n=over 4\n";
    };

    /^\@((?:small)?example|display)/ and do {
        push @endwstack, $endw;
        $endw = $1;
        $shift = "\t";
        $_ = "";        # need a paragraph break
    };

    /^\@item\s+(.*\S)\s*$/ and $endw eq "multitable" and do {
        my $columns = $1;
        $columns =~ s/\@tab/ : /;

        $_ = "\n=item B&LT;". $columns ."&GT;\n";
    };

    /^\@tab\s+(.*\S)\s*$/ and $endw eq "multitable" and do {
        my $columns = $1;
        $columns =~ s/\@tab//;

        $_ = $columns;
        $section =~ s/$//;
    };

    /^\@itemx?\s*(.+)?$/ and do {
        if (defined $1) {
            # Entity escapes prevent munging by the <> processing below.
            $_ = "\n=item $ic\&LT;$1\&GT;\n";
        } else {
            $_ = "\n=item $ic\n";
            $ic =~ y/A-Ya-y/B-Zb-z/;
            $ic =~ s/(\d+)/$1 + 1/eg;
        }
    };

    $section .= $shift.$_."\n";
}
# End of current file.
close($inf);
$inf = pop @instack;
}

die "No filename or title\n" unless defined $fn && defined $tl;

# always use utf8
print "=encoding utf8\n\n";

$sects{NAME} = "$fn \- $tl\n";
$sects{FOOTNOTES} .= "=back\n" if exists $sects{FOOTNOTES};

unshift @sects_sequence, "NAME";
for $sect (@sects_sequence) {
    if(exists $sects{$sect}) {
        $head = $sect;
        $head =~ s/SEEALSO/SEE ALSO/;
        print "=head1 $head\n\n";
        print scalar unmunge ($sects{$sect});
        print "\n";
    }
}

sub usage
{
    die "usage: $0 [-D toggle...] [infile [outfile]]\n";
}

sub postprocess
{
    local $_ = $_[0];

    # @value{foo} is replaced by whatever 'foo' is defined as.
    while (m/(\@value\{([a-zA-Z0-9_-]+)\})/g) {
        if (! exists $defs{$2}) {
            print STDERR "Option $2 not defined\n";
            s/\Q$1\E//;
        } else {
            $value = $defs{$2};
            s/\Q$1\E/$value/;
        }
    }

    # Formatting commands.
    # Temporary escape for @r.
    s/\@r\{([^\}]*)\}/R<$1>/g;
    s/\@(?:dfn|var|emph|cite|i)\{([^\}]*)\}/I<$1>/g;
    s/\@(?:code|kbd)\{([^\}]*)\}/C<$1>/g;
    s/\@(?:gccoptlist|samp|strong|key|option|env|command|b)\{([^\}]*)\}/B<$1>/g;
    s/\@sc\{([^\}]*)\}/\U$1/g;
    s/\@file\{([^\}]*)\}/F<$1>/g;
    s/\@w\{([^\}]*)\}/S<$1>/g;
    s/\@(?:dmn|math)\{([^\}]*)\}/$1/g;

    # Cross references are thrown away, as are @noindent and @refill.
    # (@noindent is impossible in .pod, and @refill is unnecessary.)
    # @* is also impossible in .pod; we discard it and any newline that
    # follows it.  Similarly, our macro @gol must be discarded.

    s/\@anchor\{(?:[^\}]*)\}//g;
    s/\(?\@xref\{(?:[^\}]*)\}(?:[^.<]|(?:<[^<>]*>))*\.\)?//g;
    s/\s+\(\@pxref\{(?:[^\}]*)\}\)//g;
    s/;\s+\@pxref\{(?:[^\}]*)\}//g;
    s/\@ref\{([^\}]*)\}/$1/g;
    s/\@noindent\s*//g;
    s/\@refill//g;
    s/\@gol//g;
    s/\@\*\s*\n?//g;

    # @uref can take one, two, or three arguments, with different
    # semantics each time.  @url and @email are just like @uref with
    # one argument, for our purposes.
    s/\@(?:uref|url|email)\{([^\},]*)\}/&lt;B<$1>&gt;/g;
    s/\@uref\{([^\},]*),([^\},]*)\}/$2 (C<$1>)/g;
    s/\@uref\{([^\},]*),([^\},]*),([^\},]*)\}/$3/g;

    # Turn B<blah I<blah> blah> into B<blah> I<blah> B<blah> to
    # match Texinfo semantics of @emph inside @samp.  Also handle @r
    # inside bold.
    s/&LT;/</g;
    s/&GT;/>/g;
    1 while s/B<((?:[^<>]|I<[^<>]*>)*)R<([^>]*)>/B<$1>${2}B</g;
    1 while (s/B<([^<>]*)I<([^>]+)>/B<$1>I<$2>B</g);
    1 while (s/I<([^<>]*)B<([^>]+)>/I<$1>B<$2>I</g);
    s/[BI]<>//g;
    s/([BI])<(\s+)([^>]+)>/$2$1<$3>/g;
    s/([BI])<([^>]+?)(\s+)>/$1<$2>$3/g;

    # Extract footnotes.  This has to be done after all other
    # processing because otherwise the regexp will choke on formatting
    # inside @footnote.
    while (/\@footnote/g) {
        s/\@footnote\{([^\}]+)\}/[$fnno]/;
        add_footnote($1, $fnno);
        $fnno++;
    }

    return $_;
}

sub unmunge
{
    # Replace escaped symbols with their equivalents.
    local $_ = $_[0];

    s/&lt;/E<lt>/g;
    s/&gt;/E<gt>/g;
    s/&lbrace;/\{/g;
    s/&rbrace;/\}/g;
    s/&at;/\@/g;
    s/&amp;/&/g;
    return $_;
}

sub add_footnote
{
    unless (exists $sects{FOOTNOTES}) {
        $sects{FOOTNOTES} = "\n=over 4\n\n";
    }

    $sects{FOOTNOTES} .= "=item $fnno.\n\n"; $fnno++;
    $sects{FOOTNOTES} .= $_[0];
    $sects{FOOTNOTES} .= "\n\n";
}

# stolen from Symbol.pm
{
    my $genseq = 0;
    sub gensym
    {
        my $name = "GEN" . $genseq++;
        my $ref = \*{$name};
        delete $::{$name};
        return $ref;
    }
}
