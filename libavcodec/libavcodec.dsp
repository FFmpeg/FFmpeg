# Microsoft Developer Studio Project File - Name="libavcodec" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libavcodec - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libavcodec.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libavcodec.mak" CFG="libavcodec - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libavcodec - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libavcodec - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libavcodec - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "../Release/libavcodec"
# PROP Intermediate_Dir "../Release/libavcodec"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /FR /YX /FD /c
# ADD BASE RSC /l 0x40c /d "NDEBUG"
# ADD RSC /l 0x40c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "libavcodec - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "../Debug/libavcodec"
# PROP Intermediate_Dir "../Debug/libavcodec"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /FR /YX /FD /GZ /c
# ADD BASE RSC /l 0x40c /d "_DEBUG"
# ADD RSC /l 0x40c /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "libavcodec - Win32 Release"
# Name "libavcodec - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "libac3"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\libac3\ac3.h
# End Source File
# Begin Source File

SOURCE=.\libac3\ac3_internal.h
# End Source File
# Begin Source File

SOURCE=.\libac3\bit_allocate.c
# End Source File
# Begin Source File

SOURCE=.\libac3\bitstream.c
# End Source File
# Begin Source File

SOURCE=.\libac3\bitstream.h
# End Source File
# Begin Source File

SOURCE=.\libac3\downmix.c
# End Source File
# Begin Source File

SOURCE=.\libac3\imdct.c
# End Source File
# Begin Source File

SOURCE=.\libac3\parse.c
# End Source File
# Begin Source File

SOURCE=.\libac3\tables.h
# End Source File
# End Group
# Begin Group "mpglib"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mpglib\dct64_i386.c
# End Source File
# Begin Source File

SOURCE=.\mpglib\decode_i386.c
# End Source File
# Begin Source File

SOURCE=.\mpglib\huffman.h
# End Source File
# Begin Source File

SOURCE=.\mpglib\l2tables.h
# End Source File
# Begin Source File

SOURCE=.\mpglib\layer1.c
# End Source File
# Begin Source File

SOURCE=.\mpglib\layer2.c
# End Source File
# Begin Source File

SOURCE=.\mpglib\layer3.c
# End Source File
# Begin Source File

SOURCE=.\mpglib\mpg123.h
# End Source File
# Begin Source File

SOURCE=.\mpglib\tabinit.c
# End Source File
# End Group
# Begin Source File

SOURCE=.\ac3dec.c
# End Source File
# Begin Source File

SOURCE=.\ac3enc.c
# End Source File
# Begin Source File

SOURCE=.\ac3enc.h
# End Source File
# Begin Source File

SOURCE=.\ac3tab.h
# End Source File
# Begin Source File

SOURCE=.\avcodec.h
# End Source File
# Begin Source File

SOURCE=.\common.c
# End Source File
# Begin Source File

SOURCE=.\common.h
# End Source File
# Begin Source File

SOURCE=.\dsputil.c
# End Source File
# Begin Source File

SOURCE=.\dsputil.h
# End Source File
# Begin Source File

SOURCE=.\fastmemcpy.h
# End Source File
# Begin Source File

SOURCE=.\h263.c
# End Source File
# Begin Source File

SOURCE=.\h263data.h
# End Source File
# Begin Source File

SOURCE=.\h263dec.c
# End Source File
# Begin Source File

SOURCE=.\imgconvert.c
# End Source File
# Begin Source File

SOURCE=.\imgresample.c
# End Source File
# Begin Source File

SOURCE=.\jfdctfst.c
# End Source File
# Begin Source File

SOURCE=.\jrevdct.c
# End Source File
# Begin Source File

SOURCE=.\mjpeg.c
# End Source File
# Begin Source File

SOURCE=.\motion_est.c
# End Source File
# Begin Source File

SOURCE=.\mpeg12.c
# End Source File
# Begin Source File

SOURCE=.\mpeg12data.h
# End Source File
# Begin Source File

SOURCE=.\mpeg4data.h
# End Source File
# Begin Source File

SOURCE=.\mpegaudio.c
# End Source File
# Begin Source File

SOURCE=.\mpegaudio.h
# End Source File
# Begin Source File

SOURCE=.\mpegaudiodec.c
# End Source File
# Begin Source File

SOURCE=.\mpegaudiotab.h
# End Source File
# Begin Source File

SOURCE=.\mpegvideo.c
# End Source File
# Begin Source File

SOURCE=.\mpegvideo.h
# End Source File
# Begin Source File

SOURCE=.\msmpeg4.c
# End Source File
# Begin Source File

SOURCE=.\msmpeg4data.h
# End Source File
# Begin Source File

SOURCE=.\resample.c
# End Source File
# Begin Source File

SOURCE=.\rv10.c
# End Source File
# Begin Source File

SOURCE=.\utils.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# End Target
# End Project
