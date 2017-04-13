ShiftMediaProject FFmpeg
=============
[![Build status](https://ci.appveyor.com/api/projects/status/4x01kkkws4aok5oq?svg=true)](https://ci.appveyor.com/project/Sibras/ffmpeg)
[![Github All Releases](https://img.shields.io/github/downloads/ShiftMediaProject/FFmpeg/total.svg)](https://github.com/ShiftMediaProject/FFmpeg/releases)
[![GitHub release](https://img.shields.io/github/release/ShiftMediaProject/FFmpeg.svg)](https://github.com/ShiftMediaProject/FFmpeg/releases/latest)
[![GitHub issues](https://img.shields.io/github/issues/ShiftMediaProject/FFmpeg.svg)](https://github.com/ShiftMediaProject/FFmpeg/issues)
[![license](https://img.shields.io/github/license/ShiftMediaProject/FFmpeg.svg)](https://github.com/ShiftMediaProject/FFmpeg)
[![donate](https://img.shields.io/badge/donate-link-brightgreen.svg)](https://shiftmediaproject.github.io/8-donate/)
## ShiftMediaProject

Shift Media Project aims to provide native Windows development libraries for FFmpeg and associated dependencies to support simpler creation and debugging of rich media content directly within Visual Studio. [https://shiftmediaproject.github.io/](https://shiftmediaproject.github.io/)

## FFmpeg

FFmpeg is a collection of libraries and tools to process multimedia content such as audio, video, subtitles and related metadata. [https://ffmpeg.org](https://ffmpeg.org)

## Downloads

Pre-built executables are available from the [releases](https://github.com/ShiftMediaProject/FFmpeg/releases) page in a single archive containing both 32bit and 64bit versions.
Development libraries are also available from the [releases](https://github.com/ShiftMediaProject/FFmpeg/releases) page. These libraries are available for each supported Visual Studio version (2013, 2015 or 2017) with a different download for each version. Each download contains both static and dynamic libraries to choose from in both 32bit and 64bit versions.

## Code

This repository contains code from the corresponding upstream project with additional modifications to allow it to be compiled with Visual Studio. New custom Visual Studio projects are provided within the 'SMP' sub-directory. Refer to the 'readme' contained within the 'SMP' directory for further details.

## Issues

Any issues related to the ShiftMediaProject specific changes should be sent to the [issues](https://github.com/ShiftMediaProject/FFmpeg/issues) page for the repository. Any issues related to the upstream project should be sent upstream directly (see the issues information of the upstream repository for more details).

## License

ShiftMediaProject original code is released under [GPLv2](https://www.gnu.org/licenses/gpl-2.0.html). All code from the upstream repository remains under its original license (see the license information of the upstream repository for more details).

## Copyright

As this repository includes code from upstream project(s) it includes many copyright owners. ShiftMediaProject makes NO claim of copyright on any upstream code. However, all original ShiftMediaProject authored code is copyright ShiftMediaProject. For a complete copyright list please checkout the source code to examine license headers. Unless expressly stated otherwise all code submitted to the ShiftMediaProject project (in any form) is licensed under [GPLv2](https://www.gnu.org/licenses/gpl-2.0.html) and copyright is donated to ShiftMediaProject. If you submit code that is not your own work it is your responsibility to place a header stating the copyright.

## Contributing

Patches related to the ShiftMediaProject specific changes should be sent as pull requests to the main repository. Any changes related to the upstream project should be sent upstream directly (see the contributing information of the upstream repository for more details).