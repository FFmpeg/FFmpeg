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

# Texinfo 7.0 changed the syntax of various functions.
# Provide a shim for older versions.
sub ff_set_from_init_file($$) {
    my $key = shift;
    my $value = shift;
    if (exists &{'texinfo_set_from_init_file'}) {
        texinfo_set_from_init_file($key, $value);
    } else {
        set_from_init_file($key, $value);
    }
}

sub ff_get_conf($) {
    my $key = shift;
    if (exists &{'texinfo_get_conf'}) {
        texinfo_get_conf($key);
    } else {
        get_conf($key);
    }
}

sub get_formatting_function($$) {
    my $obj = shift;
    my $func = shift;

    my $sub = $obj->can('formatting_function');
    if ($sub) {
        return $obj->formatting_function($func);
    } else {
        return $obj->{$func};
    }
}

# determine texinfo version
my $package_version = ff_get_conf('PACKAGE_VERSION');
$package_version =~ s/\+dev$//;
my $program_version_num = version->declare($package_version)->numify;
my $program_version_6_8 = $program_version_num >= 6.008000;

# no navigation elements
ff_set_from_init_file('HEADERS', 0);

my %sectioning_commands = %Texinfo::Common::sectioning_commands;
if (scalar(keys(%sectioning_commands)) == 0) {
  %sectioning_commands = %Texinfo::Commands::sectioning_heading_commands;
}

my %root_commands = %Texinfo::Common::root_commands;
if (scalar(keys(%root_commands)) == 0) {
  %root_commands = %Texinfo::Commands::root_commands;
}

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

    # no need to set it as the $element_id is output unconditionally
    my $heading_id;

    my $element_id = $self->command_id($command);
    $result .= "<a name=\"$element_id\"></a>\n"
        if (defined($element_id) and $element_id ne '');

    print STDERR "Process $command "
        .Texinfo::Structuring::_print_root_command_texi($command)."\n"
            if ($self->get_conf('DEBUG'));
    my $output_unit;
    if ($root_commands{$command->{'cmdname'}}) {
        if ($command->{'associated_unit'}) {
          $output_unit = $command->{'associated_unit'};
        } elsif ($command->{'structure'}
                 and $command->{'structure'}->{'associated_unit'}) {
          $output_unit = $command->{'structure'}->{'associated_unit'};
        } elsif ($command->{'parent'}
                 and $command->{'parent'}->{'type'}
                 and $command->{'parent'}->{'type'} eq 'element') {
          $output_unit = $command->{'parent'};
        }
    }

    if ($output_unit) {
        $result .= &{get_formatting_function($self, 'format_element_header')}($self, $cmdname,
                                                       $command, $output_unit);
    }

    my $heading_level;
    # node is used as heading if there is nothing else.
    if ($cmdname eq 'node') {
        if (!$output_unit or
            (((!$output_unit->{'extra'}->{'section'}
              and $output_unit->{'extra'}->{'node'}
              and $output_unit->{'extra'}->{'node'} eq $command)
             or
             ((($output_unit->{'extra'}->{'unit_command'}
                and $output_unit->{'extra'}->{'unit_command'} eq $command)
               or
               ($output_unit->{'unit_command'}
                and $output_unit->{'unit_command'} eq $command))
              and $command->{'extra'}
              and not $command->{'extra'}->{'associated_section'}))
             # bogus node may not have been normalized
            and defined($command->{'extra'}->{'normalized'}))) {
            if ($command->{'extra'}->{'normalized'} eq 'Top') {
                $heading_level = 0;
            } else {
                $heading_level = 3;
            }
        }
    } else {
        if (defined($command->{'extra'})
            and defined($command->{'extra'}->{'section_level'})) {
          $heading_level = $command->{'extra'}->{'section_level'};
        } elsif ($command->{'structure'}
                 and defined($command->{'structure'}->{'section_level'})) {
          $heading_level = $command->{'structure'}->{'section_level'};
        } else {
          $heading_level = $command->{'level'};
        }
    }

    my $heading = $self->command_text($command);
    # $heading not defined may happen if the command is a @node, for example
    # if there is an error in the node.
    if (defined($heading) and $heading ne '' and defined($heading_level)) {

        if ($root_commands{$cmdname}
            and $sectioning_commands{$cmdname}) {
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

        my $in_preformatted;
        if ($program_version_num >= 7.001090) {
          $in_preformatted = $self->in_preformatted_context();
        } else {
          $in_preformatted = $self->in_preformatted();
        }
        if ($in_preformatted) {
            $result .= $heading."\n";
        } else {
            # if the level was changed, set the command name right
            if ($cmdname ne 'node'
                and $heading_level ne $Texinfo::Common::command_structuring_level{$cmdname}) {
                $cmdname
                    = $Texinfo::Common::level_to_structuring_command{$cmdname}->[$heading_level];
            }
            if ($program_version_num >= 7.000000) {
                $result .= &{get_formatting_function($self,'format_heading_text')}($self,
                     $cmdname, [$cmdname], $heading,
                     $heading_level +$self->get_conf('CHAPTER_HEADER_LEVEL') -1,
                     $heading_id, $command);

            } else {
              $result .= &{get_formatting_function($self,'format_heading_text')}(
                        $self, $cmdname, $heading,
                        $heading_level +
                        $self->get_conf('CHAPTER_HEADER_LEVEL') - 1, $command);
            }
        }
    }
    $result .= $content if (defined($content));
    return $result;
}

foreach my $command (keys(%sectioning_commands), 'node') {
    texinfo_register_command_formatting($command, \&ffmpeg_heading_command);
}

# print the TOC where @contents is used
if ($program_version_6_8) {
    ff_set_from_init_file('CONTENTS_OUTPUT_LOCATION', 'inline');
} else {
    ff_set_from_init_file('INLINE_CONTENTS', 1);
}

# make chapters <h2>
ff_set_from_init_file('CHAPTER_HEADER_LEVEL', 2);

# Do not add <hr>
ff_set_from_init_file('DEFAULT_RULE', '');
ff_set_from_init_file('BIG_RULE', '');

# Customized file beginning
sub ffmpeg_begin_file($$$)
{
    my $self = shift;
    my $filename = shift;
    my $element = shift;

    my ($element_command, $node_command, $command_for_title);
    if ($element) {
        if ($element->{'unit_command'}) {
          $element_command = $element->{'unit_command'};
        } elsif ($self->can('tree_unit_element_command')) {
          $element_command = $self->tree_unit_element_command($element);
        } elsif ($self->can('tree_unit_element_command')) {
          $element_command = $self->element_command($element);
        }

       $node_command = $element_command;
       if ($element_command and $element_command->{'cmdname'}
           and $element_command->{'cmdname'} ne 'node'
           and $element_command->{'extra'}
           and $element_command->{'extra'}->{'associated_node'}) {
         $node_command = $element_command->{'extra'}->{'associated_node'};
       }

       $command_for_title = $element_command if ($self->get_conf('SPLIT'));
    }

    my ($title, $description, $keywords, $encoding, $date, $css_lines, $doctype,
        $root_html_element_attributes, $body_attributes, $copying_comment,
        $after_body_open, $extra_head, $program_and_version, $program_homepage,
        $program, $generator);
    if ($program_version_num >= 7.001090) {
        ($title, $description, $keywords, $encoding, $date, $css_lines, $doctype,
         $root_html_element_attributes, $body_attributes, $copying_comment,
         $after_body_open, $extra_head, $program_and_version, $program_homepage,
         $program, $generator) = $self->_file_header_information($command_for_title,
                                                                 $filename);
    } elsif ($program_version_num >= 7.000000) {
        ($title, $description, $encoding, $date, $css_lines, $doctype,
         $root_html_element_attributes, $copying_comment, $after_body_open,
         $extra_head, $program_and_version, $program_homepage,
         $program, $generator) = $self->_file_header_information($command_for_title,
                                                                 $filename);
    } else {
        ($title, $description, $encoding, $date, $css_lines,
         $doctype, $root_html_element_attributes, $copying_comment,
         $after_body_open, $extra_head, $program_and_version, $program_homepage,
         $program, $generator) = $self->_file_header_informations($command_for_title);
    }

    my $links;
    if ($program_version_num >= 7.000000) {
      $links = $self->_get_links($filename, $element, $node_command);
    } else {
      $links = $self->_get_links ($filename, $element);
    }

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
    if ($program_version_num >= 7.001090) {
     return $self->convert_tree(
      $self->cdt('This document was generated using @uref{{program_homepage}, @emph{{program}}}.',
         { 'program_homepage' => {'text' => $self->get_conf('PACKAGE_URL')},
           'program' => {'text' => $self->get_conf('PROGRAM') }}));
    } else {
     return $self->convert_tree(
      $self->gdt('This document was generated using @uref{{program_homepage}, @emph{{program}}}.',
         { 'program_homepage' => {'text' => $self->get_conf('PACKAGE_URL')},
           'program' => {'text' => $self->get_conf('PROGRAM') }}));
    }
  } else {
    if ($program_version_num >= 7.001090) {
      return $self->convert_tree(
        $self->cdt('This document was generated automatically.'));
    } else {
      return $self->convert_tree(
        $self->gdt('This document was generated automatically.'));
    }
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
    my $program_string = &{get_formatting_function($self,'format_program_string')}($self);
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
ff_set_from_init_file('USE_TITLEPAGE_FOR_TITLE', 1);
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

    my ($caption, $prepended);
    if ($program_version_num >= 7.000000) {
        ($caption, $prepended) = Texinfo::Convert::Converter::float_name_caption($self,
                                                                                 $command);
    } else {
        ($caption, $prepended) = Texinfo::Common::float_name_caption($self,
                                                                     $command);
    }
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
        if ($program_version_num >= 7.000000) {
            $prepended_text = $self->html_attribute_class('div',['float-caption']). '>'
                    . $prepended_text;
        } else {
            $prepended_text = $self->_attribute_class('div','float-caption'). '>'
                    . $prepended_text;
        }
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
    if ($program_version_num >= 7.000000) {
        return $self->html_attribute_class('div', [$html_class]). '>' . "\n" .
            $prepended_text . $caption_text . $content . '</div>';
    } else {
        return $self->_attribute_class('div', $html_class). '>' . "\n" .
            $prepended_text . $caption_text . $content . '</div>';
    }
}

texinfo_register_command_formatting('float',
                                    \&ffmpeg_float);

1;
