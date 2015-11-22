@ECHO OFF
SETLOCAL EnableDelayedExpansion

SET UPSTREAMURL=https://github.com/ShiftMediaProject
SET DEPENDENCIES=( ^
bzip2, ^
fdk-aac, ^
fontconfig, ^
freetype2, ^
fribidi, ^
game-music-emu, ^
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
rtmpdump, ^
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
git status >NUL 2>&1
IF %ERRORLEVEL% NEQ 0 (
  ECHO A working copy of git was not found. To use this script you must first install git for windows.
  EXIT /B 1
)

SET CURRDIR=%~dp1

cd ..\..
FOR %%I IN %DEPENDENCIES% DO (
    ECHO !PASSDEPENDENCIES! | FINDSTR /C:"%%I" >NUL 2>&1 || (
        CALL :cloneOrUpdateRepo "%%I" )
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
    project_get_dependencies.bat "!PASSDEPENDENCIES!" || GOTO exitOnError
    cd ..\..
)
ECHO.
EXIT /B %ERRORLEVEL%

:exitOnError
cd %CURRDIR%

:exit
REM Return the passed dependency list
(
    ENDLOCAL
    SET PASSDEPENDENCIES=%PASSDEPENDENCIES%
)
    
REM Check if this was laucnhed from an existing terminal or directly from .bat
REM  If launched by executing the .bat then pause on completion
ECHO %CMDCMDLINE% | FINDSTR /L %COMSPEC% >NUL 2>&1
IF %ERRORLEVEL% == 0 IF "%~1"=="" PAUSE

EXIT /B 0