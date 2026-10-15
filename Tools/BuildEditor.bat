@echo off
rem Builds the FishingValley editor module (UnrealEditor-FishingValley.dll). Extra args are passed to UBT.
rem Uses the ASCII junction path because MSVC cannot handle the Cyrillic project path.
call "%~dp0Env.bat"
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" FishingValleyEditor Win64 Development -Project="%FV_UPROJECT%" -WaitMutex %*
