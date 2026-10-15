@echo off
rem Compiles a single .cpp of the module (no link). Usage: CompileFile.bat Source\FishingValley\Private\FVScene.cpp
call "%~dp0Env.bat"
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" FishingValleyEditor Win64 Development -Project="%FV_UPROJECT%" -WaitMutex -SingleFile="%FV_PROJ%\%~1"
