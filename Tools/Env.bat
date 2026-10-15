@echo off
rem Machine-specific paths shared by every .bat/.ps1 in the project: when moving to another PC edit only this file.
rem The values below are examples: set them for your machine before the first build.
rem FV_PROJ is an ASCII junction to the project folder, because MSVC fails on Cyrillic paths.
rem It is created automatically (pointing at the folder this script lives in) if it does not exist yet.
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "FV_PROJ=C:\UEProjects\FishingValley"
set "UE_EDITOR=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "FV_UPROJECT=%FV_PROJ%\FishingValley.uproject"
rem Android toolchain for Tools\BuildAndroid.bat (layout: Sdk\, Sdk\ndk\<NDK_VER>\, jdk\). Not needed for the PC build.
set "FV_ANDROID=C:\Android"
set "FV_NDK_VER=27.2.12479018"
set VSLANG=1033
if exist "%FV_UPROJECT%" goto :eof
for %%I in ("%~dp0..") do set "FV_REAL=%%~fI"
for %%I in ("%FV_PROJ%") do if not exist "%%~dpI" mkdir "%%~dpI"
mklink /J "%FV_PROJ%" "%FV_REAL%" >nul
