# makeinfo HTML output init file
#
# Copyright (c) 2011, 2012 Free Software Foundation, Inc.
# Copyright (c) 2014 Andreas Cadhalpun
# Copyright (c) 2014 Tiancheng "Timothy" Gu
#
# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with FFmpeg; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

# no navigation elements
set_from_init_file('HEADERS', 0);

sub ffmpeg_heading_command($$$$$)
{
    my $self = shift;
    my $cmdname = shift;
    my $command = shift;
    my $args = shift;
    my $content = shift;

    my $result = '';

    # not clear that it may really happen
    if ($self->in_string) {
        $result .= $self->command_string($command) ."\n" if ($cmdname ne 'node');
        $result .= $content if (defined($content));
        return $result;
    }

    my $element_id = $self->command_id($command);
    $result .= "<a name=\"$element_id\"></a>\n"
        if (defined($element_id) and $element_id ne '');

    print STDERR "Process $command "
        .Texinfo::Structuring::_print_root_command_texi($command)."\n"
            if ($self->get_conf('DEBUG'));
    my $element;
    if ($Texinfo::Common::root_commands{$command->{'cmdname'}}
        and $command->{'parent'}
        and $command->{'parent'}->{'type'}
        and $command->{'parent'}->{'type'} eq 'element') {
        $element = $command->{'parent'};
    }
    if ($element) {
        $result .= &{$self->{'format_element_header'}}($self, $cmdname,
                                                       $command, $element);
    }

    my $heading_level;
    # node is used as heading if there is nothing else.
    if ($cmdname eq 'node') {
        if (!$element or (!$element->{'extra'}->{'section'}
            and $element->{'extra'}->{'node'}
            and $element->{'extra'}->{'node'} eq $command
             # bogus node may not have been normalized
            and defined($command->{'extra'}->{'normalized'}))) {
            if ($command->{'extra'}->{'normalized'} eq 'Top') {
                $heading_level = 0;
            } else {
                $heading_level = 3;
            }
        }
    } else {
        $heading_level = $command->{'level'};
    }

    my $heading = $self->command_text($command);
    # $heading not defined may happen if the command is a @node, for example
    # if there is an error in the node.
    if (defined($heading) and $heading ne '' and defined($heading_level)) {

        if ($Texinfo::Common::root_commands{$cmdname}
            and $Texinfo::Common::sectioning_commands{$cmdname}) {
            my $content_href = $self->command_contents_href($command, 'contents',
                                                            $self->{'current_filename'});
            if ($content_href) {
                my $this_href = $content_href =~ s/^\#toc-/\#/r;
                $heading .= '<span class="pull-right">'.
                              '<a class="anchor hidden-xs" '.
                                 "href=\"$this_href\" aria-hidden=\"true\">".
            ($ENV{"FA_ICONS"} ? '<i class="fa fa-link"></i>'
                              : '#').
                              '</a> '.
                              '<a class="anchor hidden-xs"'.
                                 "href=\"$content_href\" aria-hidden=\"true\">".
            ($ENV{"FA_ICONS"} ? '<i class="fa fa-navicon"></i>'
                              : 'TOC').
                              '</a>'.
                            '</span>';
            }
        }

        if ($self->in_preformatted()) {
            $result .= $heading."\n";
        } else {
            # if the level was changed, set the command name right
            if ($cmdname ne 'node'
                and $heading_level ne $Texinfo::Common::command_structuring_level{$cmdname}) {
                $cmdname
                    = $Texinfo::Common::level_to_structuring_command{$cmdname}->[$heading_level];
            }
            $result .= &{$self->{'format_heading_text'}}(
                        $self, $cmdname, $heading,
                        $heading_level +
                        $self->get_conf('CHAPTER_HEADER_LEVEL') - 1, $command);
        }
    }
    $result .= $content if (defined($content));
    return $result;
}

foreach my $command (keys(%Texinfo::Common::sectioning_commands), 'node') {
    texinfo_register_command_formatting($command, \&ffmpeg_heading_command);
}

# determine if texinfo is at least version 6.8
my $program_version_num = version->declare(get_conf('PACKAGE_VERSION'))->numify;
my $program_version_6_8 = $program_version_num >= 6.008000;

# print the TOC where @contents is used
if ($program_version_6_8) {
    set_from_init_file('CONTENTS_OUTPUT_LOCATION', 'inline');
} else {
    set_from_init_file('INLINE_CONTENTS', 1);
}

# make chapters <h2>
set_from_init_file('CHAPTER_HEADER_LEVEL', 2);

# Do not add <hr>
set_from_init_file('DEFAULT_RULE', '');
set_from_init_file('BIG_RULE', '');

# Customized file beginning
sub ffmpeg_begin_file($$$)
{
    my $self = shift;
    my $filename = shift;
    my $element = shift;

    my $command;
    if ($element and $self->get_conf('SPLIT')) {
        $command = $self->element_command($element);
    }

    my ($title, $description, $encoding, $date, $css_lines,
        $doctype, $bodytext, $copying_comment, $after_body_open,
        $extra_head, $program_and_version, $program_homepage,
        $program, $generator) = $self->_file_header_informations($command);

    my $links = $self->_get_links ($filename, $element);

    my $head1 = $ENV{"FFMPEG_HEADER1"} || <<EOT;
<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
<!-- Created by $program_and_version, $program_homepage -->
  <head>
    <meta charset="utf-8">
    <title>
EOT
    my $head_title = <<EOT;
      $title
EOT

    my $head2 = $ENV{"FFMPEG_HEADER2"} || <<EOT;
    </title>
    <meta name="viewport" content="width=device-width,initial-scale=1.0">
    <link rel="stylesheet" type="text/css" href="bootstrap.min.css">
    <link rel="stylesheet" type="text/css" href="style.min.css">
  </head>
  <body>
    <div class="container">
      <h1>
EOT

    my $head3 = $ENV{"FFMPEG_HEADER3"} || <<EOT;
      </h1>
EOT

    return $head1 . $head_title . $head2 . $head_title . $head3;
}
if ($program_version_6_8) {
    texinfo_register_formatting_function('format_begin_file', \&ffmpeg_begin_file);
} else {
    texinfo_register_formatting_function('begin_file', \&ffmpeg_begin_file);
}

sub ffmpeg_program_string($)
{
  my $self = shift;
  if (defined($self->get_conf('PROGRAM'))
      and $self->get_conf('PROGRAM') ne ''
      and defined($self->get_conf('PACKAGE_URL'))) {
    return $self->convert_tree(
      $self->gdt('This document was generated using @uref{{program_homepage}, @emph{{program}}}.',
         { 'program_homepage' => $self->get_conf('PACKAGE_URL'),
           'program' => $self->get_conf('PROGRAM') }));
  } else {
    return $self->convert_tree(
      $self->gdt('This document was generated automatically.'));
  }
}
if ($program_version_6_8) {
    texinfo_register_formatting_function('format_program_string', \&ffmpeg_program_string);
} else {
    texinfo_register_formatting_function('program_string', \&ffmpeg_program_string);
}

# Customized file ending
sub ffmpeg_end_file($)
{
    my $self = shift;
    my $program_string = &{$self->{'format_program_string'}}($self);
    my $program_text = <<EOT;
      <p style="font-size: small;">
        $program_string
      </p>
EOT
    my $footer = $ENV{FFMPEG_FOOTER} || <<EOT;
    </div>
  </body>
</html>
EOT
    return $program_text . $footer;
}
if ($program_version_6_8) {
    texinfo_register_formatting_function('format_end_file', \&ffmpeg_end_file);
} else {
    texinfo_register_formatting_function('end_file', \&ffmpeg_end_file);
}

# Dummy title command
# Ignore title. Title is handled through ffmpeg_begin_file().
set_from_init_file('USE_TITLEPAGE_FOR_TITLE', 1);
sub ffmpeg_title($$$$)
{
    return '';
}

texinfo_register_command_formatting('titlefont',
                                    \&ffmpeg_title);

# Customized float command. Part of code borrowed from GNU Texinfo.
sub ffmpeg_float($$$$$)
{
    my $self = shift;
    my $cmdname = shift;
    my $command = shift;
    my $args = shift;
    my $content = shift;

    my ($caption, $prepended) = Texinfo::Common::float_name_caption($self,
                                                                $command);
    my $caption_text = '';
    my $prepended_text;
    my $prepended_save = '';

    if ($self->in_string()) {
        if ($prepended) {
            $prepended_text = $self->convert_tree_new_formatting_context(
                $prepended, 'float prepended');
        } else {
            $prepended_text = '';
        }
        if ($caption) {
            $caption_text = $self->convert_tree_new_formatting_context(
                {'contents' => $caption->{'args'}->[0]->{'contents'}},
                'float caption');
        }
        return $prepended.$content.$caption_text;
    }

    my $id = $self->command_id($command);
    my $label;
    if (defined($id) and $id ne '') {
        $label = "<a name=\"$id\"></a>";
    } else {
        $label = '';
    }

    if ($prepended) {
        if ($caption) {
            # prepend the prepended tree to the first paragraph
            my @caption_original_contents = @{$caption->{'args'}->[0]->{'contents'}};
            my @caption_contents;
            my $new_paragraph;
            while (@caption_original_contents) {
                my $content = shift @caption_original_contents;
                if ($content->{'type'} and $content->{'type'} eq 'paragraph') {
                    %{$new_paragraph} = %{$content};
                    $new_paragraph->{'contents'} = [@{$content->{'contents'}}];
                    unshift (@{$new_paragraph->{'contents'}}, {'cmdname' => 'strong',
                             'args' => [{'type' => 'brace_command_arg',
                                                    'contents' => [$prepended]}]});
                    push @caption_contents, $new_paragraph;
                    last;
                } else {
                    push @caption_contents, $content;
                }
            }
            push @caption_contents, @caption_original_contents;
            if ($new_paragraph) {
                $caption_text = $self->convert_tree_new_formatting_context(
                 {'contents' => \@caption_contents}, 'float caption');
                $prepended_text = '';
            }
        }
        if ($caption_text eq '') {
            $prepended_text = $self->convert_tree_new_formatting_context(
                $prepended, 'float prepended');
            if ($prepended_text ne '') {
                $prepended_save = $prepended_text;
                $prepended_text = '<p><strong>'.$prepended_text.'</strong></p>';
            }
        }
    } else {
        $prepended_text = '';
    }

    if ($caption and $caption_text eq '') {
        $caption_text = $self->convert_tree_new_formatting_context(
            $caption->{'args'}->[0], 'float caption');
    }
    if ($prepended_text.$caption_text ne '') {
        $prepended_text = $self->_attribute_class('div','float-caption'). '>'
                . $prepended_text;
        $caption_text .= '</div>';
    }
    my $html_class = '';
    if ($prepended_save =~ /NOTE/) {
        $html_class = 'info';
        $prepended_text = '';
        $caption_text   = '';
    } elsif ($prepended_save =~ /IMPORTANT/) {
        $html_class = 'warning';
        $prepended_text = '';
        $caption_text   = '';
    }
    return $self->_attribute_class('div', $html_class). '>' . "\n" .
        $prepended_text . $caption_text . $content . '</div>';
}

texinfo_register_command_formatting('float',
                                    \&ffmpeg_float);

1;
