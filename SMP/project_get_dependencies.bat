@ECHO OFF
SETLOCAL EnableDelayedExpansion

SET UPSTREAMURL=https://github.com/ShiftMediaProject
SET DEPENDENCIES=( ^
bzip2, ^
fontconfig, ^
freetype2, ^
fribidi, ^
game-music-emu, ^
gmp, ^
gnutls, ^
lame, ^
libass, ^
libbluray, ^
libcdio, ^
libcdio-paranoia, ^
libiconv, ^
libilbc, ^
liblzma, ^
libssh, ^
libvpx, ^
mfx_dispatch, ^
modplug, ^
opus, ^
sdl, ^
soxr, ^
speex, ^
theora, ^
vorbis, ^
x264, ^
x265, ^
xvid, ^
zlib ^
)

REM Get passed in list of dependencies to skip
SET PASSDEPENDENCIES=%~1

REM Check if git is installed and available
IF "%MSVC_VER%"=="" (
    git status >NUL 2>&1
    IF %ERRORLEVEL% NEQ 0 (
      ECHO A working copy of git was not found. To use this script you must first install git for windows.
      EXIT /B 1
    )
)

REM Store current directory and ensure working directory is the location of current .bat
SET CURRDIR=%CD%
cd %~dp0

REM Initialise error check value
SET ERROR=0

cd ..\..
FOR %%I IN %DEPENDENCIES% DO (
    ECHO !PASSDEPENDENCIES! | FINDSTR /C:"%%I" >NUL 2>&1 || (
        REM Check if MSVC_VER environment variable is set
        IF "%MSVC_VER%"=="" (
            CALL :cloneOrUpdateRepo "%%I" || GOTO exit
        ) ELSE (
            CALL :downloadLibs "%%I" || GOTO exit
        )
    )
)
cd %CURRDIR% >NUL
GOTO exit

REM Function to clone or update a repo
REM  cloneOrUpdateRepo: RepoName
REM    RepoName = Name of the repository
:cloneOrUpdateRepo
SET REPONAME=%~1
REM Check if the repo folder already exists
IF EXIST "%REPONAME%" (
    ECHO %REPONAME%: Existing folder found. Checking for updates...
    cd %REPONAME%
    REM Check if any updates are available
    FOR /f %%J IN ('git rev-parse HEAD') do set CURRHEAD=%%J
    FOR /f %%J IN ('git ls-remote origin HEAD') do set ORIGHEAD=%%J
    IF "!CURRHEAD!"=="!ORIGHEAD!" (
        ECHO %REPONAME%: Repository up to date.
    ) ELSE (
        REM Stash any uncommited changes then update from origin
        ECHO %REPONAME%: Updates available. Updating repository...
        git checkout master --quiet
        git stash --quiet
        git pull origin master --quiet -ff
        git stash pop --quiet
    )
    cd ..\
) ELSE (
    ECHO %REPONAME%: Existing folder not found. Cloning repository...
    REM Clone from the origin repo
    SET REPOURL=%UPSTREAMURL%/%REPONAME%.git
    git clone !REPOURL! --quiet
    REM Initialise autocrlf options to fix cross platform interoperation
    REM  Once updated the repo needs to be reset to correct the local line endings
    cd %REPONAME%
    git config --local core.autocrlf false
    git rm --cached -r . --quiet
    git reset --hard --quiet
    cd ..\
)
REM Add current repo to list of already passed dependencies
SET PASSDEPENDENCIES=%PASSDEPENDENCIES% %REPONAME%
REM Check if the repo itself has required dependencies
IF EXIST "%REPONAME%\SMP\project_get_dependencies.bat" (
    ECHO %REPONAME%: Found additional dependencies...
    ECHO.
    cd %REPONAME%\SMP
    project_get_dependencies.bat "!PASSDEPENDENCIES!" || EXIT /B 1
    cd ..\..
)
ECHO.
EXIT /B %ERRORLEVEL%

REM Function to download existing prebuilt libraries
REM  downloadLibs: RepoName
REM    RepoName = Name of the repository
:downloadLibs
SET REPONAME=%~1
REM Get latest release
ECHO %REPONAME%: Getting latest release...
SET UPSTREAMAPIURL=%UPSTREAMURL:github.com=api.github.com/repos%
REM Check if secure OAuth is available
IF "%GITHUBTOKEN%" == "" (
    powershell -nologo -noprofile -command "try { Invoke-RestMethod -Uri %UPSTREAMAPIURL%/%REPONAME%/releases/latest > latest.json } catch {exit 1}"
) ELSE (
    powershell -nologo -noprofile -command "try { Invoke-RestMethod -Uri %UPSTREAMAPIURL%/%REPONAME%/releases/latest -Headers @{'Authorization' = 'token %GITHUBTOKEN%'} > latest.json } catch {exit 1}"
)
IF NOT %ERRORLEVEL% == 0 ( ECHO Failed getting latest %REPONAME% release & EXIT /B 1 )
REM Get tag for latest release
FOR /F "tokens=* USEBACKQ" %%F IN (`TYPE latest.json ^| FINDSTR /B "tag_name"`) DO SET TAG=%%F
FOR /F "tokens=2 delims=: " %%F in ("%TAG%") DO SET TAG=%%F
IF "%TAG%"=="" ( ECHO Failed getting latest %REPONAME% release tag information & EXIT /B 1 )
REM Get download name of latest release
FOR /F "tokens=* USEBACKQ" %%F IN (`TYPE latest.json ^| FINDSTR "name="`) DO SET LIBNAME=%%F
SET LIBNAME=%LIBNAME:*name=%
FOR /F "tokens=1 delims=_" %%F in ("%LIBNAME:~1%") DO SET LIBNAME=%%F
IF "%LIBNAME%"=="" ( ECHO Failed getting latest %REPONAME% release name information & EXIT /B 1 )
DEL /F /Q latest.json
REM Get the download location for the required tag
SET TAG2=%TAG:+=.%
SET DLURL=%UPSTREAMURL%/%REPONAME%/releases/download/%TAG%/%LIBNAME%_%TAG2%_msvc%MSVC_VER%.zip
REM Download a pre-built archive and extract
ECHO %REPONAME%: Downloading %LIBNAME%_%TAG%_msvc%MSVC_VER%.zip...
SET PREBUILTDIR=prebuilt
MKDIR %PREBUILTDIR% >NUL 2>&1
powershell -nologo -noprofile -command "try { (New-Object Net.WebClient).DownloadFile('%DLURL%', '%PREBUILTDIR%\temp.zip') } catch {exit 1}"
IF NOT %ERRORLEVEL% == 0 ( ECHO Failed downloading %DLURL% & EXIT /B 1 )
powershell -nologo -noprofile -command "Add-Type -AssemblyName System.IO.Compression.FileSystem; $zip=[System.IO.Compression.ZipFile]::OpenRead('%PREBUILTDIR%\temp.zip'); foreach ($item in $zip.Entries) { try {$file=(Join-Path -Path .\%PREBUILTDIR% -ChildPath $item.FullName); $null=[System.IO.Directory]::CreateDirectory((Split-Path -Path $file)); [System.IO.Compression.ZipFileExtensions]::ExtractToFile($item,$file,$true)} catch {exit 1} }"
IF NOT %ERRORLEVEL% == 0 ( ECHO Failed extracting downloaded archive & EXIT /B 1 )
DEL /F /Q %PREBUILTDIR%\\temp.zip
ECHO.
EXIT /B %ERRORLEVEL%

:exitOnError
cd %CURRDIR%
SET ERROR=1

:exit
REM Directly exit if an AppVeyor build
IF NOT "%APPVEYOR%"=="" (
    GOTO return
)
REM Return the passed dependency list
(
    ENDLOCAL
    SET PASSDEPENDENCIES=%PASSDEPENDENCIES%
)
    
REM Check if this was launched from an existing terminal or directly from .bat
REM  If launched by executing the .bat then pause on completion
ECHO %CMDCMDLINE% | FINDSTR /L %COMSPEC% >NUL 2>&1
IF %ERRORLEVEL% == 0 IF "%~1"=="" PAUSE

:return
EXIT /B %ERROR%