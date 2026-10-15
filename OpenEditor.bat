@echo off
rem Opens the project in the Unreal Editor through the ASCII junction path (MSVC fails on Cyrillic paths).
call "%~dp0Tools\Env.bat"
start "" "%UE_EDITOR%" "%FV_UPROJECT%"
