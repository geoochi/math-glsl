@echo off
REM math-glsl -- build the native GLSL video renderer on Windows
REM
REM   build.bat            tries MSVC (cl), then mingw (gcc)
REM   build.bat demo       build, then render demo\wave.mp4
REM
REM NOTE: for cl you must run this from a "x64 Native Tools Command Prompt
REM for VS", or run vcvars64.bat first, so that cl is on PATH.
REM
REM Requires ffmpeg.exe on PATH.

setlocal
set SRC=src\render_glsl.c
set OUT=render_glsl.exe

where cl >nul 2>nul
if %ERRORLEVEL%==0 (
  echo [build] MSVC
  cl /nologo /O2 /W3 /Fe:%OUT% %SRC% opengl32.lib gdi32.lib user32.lib
  if errorlevel 1 exit /b 1
  goto built
)

where gcc >nul 2>nul
if %ERRORLEVEL%==0 (
  echo [build] mingw gcc
  gcc -O2 -Wall -Wextra -o %OUT% %SRC% -lopengl32 -lgdi32 -luser32
  if errorlevel 1 exit /b 1
  goto built
)

echo [build] no C compiler found.
echo         Install Visual Studio Build Tools, or MSYS2 / mingw-w64.
exit /b 1

:built
echo [build] ok -^> %OUT%

if /I "%1"=="demo" (
  if not exist demo mkdir demo
  %OUT% demo\wave.mp4 1920 1080 60 5
)

endlocal
