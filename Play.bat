@echo off
rem Starts "Klevaya Dolina" in a 1280x720 window (the C++ module must be built: Tools\BuildEditor.bat).
call "%~dp0Tools\Env.bat"
start "" "%UE_EDITOR%" "%FV_UPROJECT%" -game -windowed -ResX=1280 -ResY=720
