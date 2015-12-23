@ECHO OFF

if not exist "..\..\FFVS-Project-Generator" (
echo "Error: FFVS Project Generator files not found next to current FFmpeg folder."
echo "Please clone the project from the online repositories before continuing (https://github.com/ShiftMediaProject/FFVS-Project-Generator.git)."
goto TERMINATE
)

if not exist "..\..\FFVS-Project-Generator\bin\project_generate.exe" (
echo "Error: FFVS Project Generator executable file not found."
echo "Please build the executable using the supplied project before continuing."
goto TERMINATE
)

cd ..\..\FFVS-Project-Generator\bin
project_generate.exe --enable-version3 --enable-avisynth --enable-libmp3lame --enable-libvorbis --enable-libspeex --enable-libopus --enable-libilbc --enable-libtheora --enable-libvpx --enable-libgme --enable-libmodplug --enable-libsoxr --enable-libfreetype --enable-fontconfig --enable-libfribidi --enable-libass --enable-gnutls --disable-schannel --enable-gmp --enable-libssh --enable-libbluray --enable-opengl --enable-libmfx --toolchain=msvc
exit 0
   
:TERMINATE
pause