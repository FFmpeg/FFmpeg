;NSIS Script For FFmpeg

;Title Of Your Application
Name "FFmpeg"
CompletedText "FFmpeg install completed! Enjoy your meal!"

; do a CRC check
CRCCheck On

; output file name
OutFile "FFinstall.exe"

; license page introduction
LicenseText "You must agree to this license before installing."

; license data
LicenseData ".\COPYING"

; the default installation directory
InstallDir "$PROGRAMFILES\FFmpeg"

;The text to prompt the user to enter a directory
DirText "Please select the folder below"

Section "Install"
  ;Install Files
  SetOutPath $INSTDIR
  SetCompress Auto
  SetOverwrite IfNewer
  File ".\ffmpeg.exe"
  File ".\SDL.dll"
  File ".\ffplay.exe"
  File ".\COPYING"
  File ".\CREDITS"
  
  ; documentation
  SetOutPath $INSTDIR\doc
  File ".\doc\faq.html"
  File ".\doc\ffmpeg-doc.html"
  File ".\doc\ffplay-doc.html"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FFmpeg" "DisplayName" "FFmpeg (remove only)"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FFmpeg" "UninstallString" "$INSTDIR\Uninst.exe"
WriteUninstaller "Uninst.exe"
SectionEnd

Section "Shortcuts"
  ;Add Shortcuts
SectionEnd

UninstallText "This will uninstall FFmpeg from your system"

Section Uninstall
  ; delete files
  Delete "$INSTDIR\ffmpeg.exe"
  Delete "$INSTDIR\SDL.dll"
  Delete "$INSTDIR\ffplay.exe"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\CREDITS"
  
  ; delete documentation
  Delete "$INSTDIR\doc\faq.html"
  Delete "$INSTDIR\ffmpeg-doc.html"
  Delete "$INSTDIR\doc\ffplay-doc.html"

  RMDir /r $INSTDIR\doc

  ; delete uninstaller and unistall registry entries
  Delete "$INSTDIR\Uninst.exe"
  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\FFmpeg"
  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFmpeg"
  RMDir "$INSTDIR"
SectionEnd

