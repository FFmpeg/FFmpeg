#!/usr/bin/env ruby

require 'open3'
require 'json'

ffmpeg_bin = 'ffmpeg'
target_il  = -24.0
target_lra = +11.0
target_tp  = -2.0
samplerate = '48k'

if ARGF.argv.count != 2
  puts "Usage: #{$PROGRAM_NAME} input.wav output.wav"
  exit 1
end

ff_cmd = Array.new([
  ffmpeg_bin,
  '-hide_banner',
  '-i', ARGF.argv[0],
  '-af', "loudnorm='I=#{target_il}:LRA=#{target_lra}:tp=#{target_tp}:print_format=json'",
  '-f', 'null',
  '-']);

_stdin, _stdout, stderr, wait_thr = Open3.popen3(*ff_cmd)

if wait_thr.value.success?
  stats = JSON.parse(stderr.read.lines[-12, 12].join)
  loudnorm_string  = 'loudnorm='
  loudnorm_string += 'print_format=summary:'
  loudnorm_string += 'linear=true:'
  loudnorm_string += "I=#{target_il}:"
  loudnorm_string += "LRA=#{target_lra}:"
  loudnorm_string += "tp=#{target_tp}:"
  loudnorm_string += "measured_I=#{stats['input_i']}:"
  loudnorm_string += "measured_LRA=#{stats['input_lra']}:"
  loudnorm_string += "measured_tp=#{stats['input_tp']}:"
  loudnorm_string += "measured_thresh=#{stats['input_thresh']}:"
  loudnorm_string += "offset=#{stats['target_offset']}"
else
  puts stderr.read
  exit 1
end

ff_cmd = Array.new([
  ffmpeg_bin,
  '-y', '-hide_banner',
  '-i', ARGF.argv[0],
  '-af', loudnorm_string,
  '-ar', samplerate,
  ARGF.argv[1].to_s]);

_stdin, _stdout, stderr, wait_thr = Open3.popen3(*ff_cmd)

if wait_thr.value.success?
  puts stderr.read.lines[-12, 12].join
  exit 0
else
  puts stderr.read
  exit 1
end
