@echo off
setlocal enabledelayedexpansion

REM ============================================
REM MiEngine2 Shader Compilation Script
REM ============================================

REM Try to find glslc from Vulkan SDK
set GLSLC=glslc
where glslc >nul 2>&1
if %errorlevel% neq 0 (
    if defined VULKAN_SDK (
        set GLSLC=%VULKAN_SDK%\Bin\glslc.exe
    ) else (
        echo ERROR: glslc not found. Please ensure Vulkan SDK is installed and VULKAN_SDK is set.
        exit /b 1
    )
)

echo Using compiler: %GLSLC%
echo.

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0

REM Track statistics
set /a COMPILED=0
set /a FAILED=0
set /a SKIPPED=0

echo ============================================
echo Compiling shaders...
echo ============================================
echo.

REM Compile root shaders
call :compile_dir "%SCRIPT_DIR%"

REM Compile subdirectory shaders
for /d %%D in ("%SCRIPT_DIR%*") do (
    call :compile_dir "%%D\"
)

echo.
echo ============================================
echo Compilation complete!
echo   Compiled: %COMPILED%
echo   Failed:   %FAILED%
echo   Skipped:  %SKIPPED%
echo ============================================

if %FAILED% gtr 0 (
    exit /b 1
)
exit /b 0

REM ============================================
REM Function to compile shaders in a directory
REM ============================================
:compile_dir
set DIR=%~1

REM Vertex shaders
for %%F in ("%DIR%*.vert") do (
    if exist "%%F" call :compile_shader "%%F"
)

REM Fragment shaders
for %%F in ("%DIR%*.frag") do (
    if exist "%%F" call :compile_shader "%%F"
)

REM Compute shaders
for %%F in ("%DIR%*.comp") do (
    if exist "%%F" call :compile_shader "%%F"
)

REM Ray generation shaders
for %%F in ("%DIR%*.rgen") do (
    if exist "%%F" call :compile_shader "%%F" "--target-spv=spv1.4"
)

REM Closest hit shaders
for %%F in ("%DIR%*.rchit") do (
    if exist "%%F" call :compile_shader "%%F" "--target-spv=spv1.4"
)

REM Miss shaders
for %%F in ("%DIR%*.rmiss") do (
    if exist "%%F" call :compile_shader "%%F" "--target-spv=spv1.4"
)

REM Any hit shaders
for %%F in ("%DIR%*.rahit") do (
    if exist "%%F" call :compile_shader "%%F" "--target-spv=spv1.4"
)

REM Intersection shaders
for %%F in ("%DIR%*.rint") do (
    if exist "%%F" call :compile_shader "%%F" "--target-spv=spv1.4"
)

goto :eof

REM ============================================
REM Function to compile a single shader
REM ============================================
:compile_shader
set SHADER=%~1
set EXTRA_ARGS=%~2

REM Skip .glsl files (include files)
if "%%~xF"==".glsl" (
    set /a SKIPPED+=1
    goto :eof
)

set OUTPUT=%SHADER%.spv

REM Get relative path for display
set DISPLAY_PATH=%SHADER%
set DISPLAY_PATH=!DISPLAY_PATH:%SCRIPT_DIR%=!

echo Compiling: !DISPLAY_PATH!

"%GLSLC%" %EXTRA_ARGS% "%SHADER%" -o "%OUTPUT%" 2>&1
if %errorlevel% neq 0 (
    echo   FAILED: !DISPLAY_PATH!
    set /a FAILED+=1
) else (
    echo   OK
    set /a COMPILED+=1
)

goto :eof
