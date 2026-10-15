@echo off
rem Runs the scripted autotest: screenshots go to Saved\AutoTest, log to Saved\Logs\FV_autotest.log
call "%~dp0Env.bat"
"%UE_EDITOR%" "%FV_UPROJECT%" -game -windowed -ResX=1280 -ResY=720 -fvautotest -log=FV_autotest.log -nosplash -unattended
