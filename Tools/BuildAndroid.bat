@echo off
rem Builds, cooks and packages the Android APK (arm64, ETC2 textures, data inside the APK).
rem Requires: the UE "Android" target platform (Epic Games Launcher -> UE -> Options -> Target Platforms)
rem and the SDK/NDK/JDK installed in %FV_ANDROID% (see Tools\Env.bat). Result: Build\Android_APK\ and KlevayaDolina.apk
setlocal
call "%~dp0Env.bat"
set "ANDROID_HOME=%FV_ANDROID%\Sdk"
set "ANDROID_SDK_ROOT=%FV_ANDROID%\Sdk"
set "NDKROOT=%FV_ANDROID%\Sdk\ndk\%FV_NDK_VER%"
set "NDK_ROOT=%FV_ANDROID%\Sdk\ndk\%FV_NDK_VER%"
set "JAVA_HOME=%FV_ANDROID%\jdk"
if not exist "%NDKROOT%" echo Android NDK not found: %NDKROOT% - install SDK/NDK/JDK or fix FV_ANDROID in Tools\Env.bat & exit /b 1
if not exist "%JAVA_HOME%\bin\java.exe" echo JDK not found: %JAVA_HOME% & exit /b 1
set "PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\platform-tools;%PATH%"
rem Java tools (sdkmanager, Gradle) hang on this PC's IPv6 route to Google's servers: force IPv4.
set "GRADLE_OPTS=-Djava.net.preferIPv4Stack=true"
set "SDKMANAGER_OPTS=-Djava.net.preferIPv4Stack=true"
rem UE's Gradle steps call helper .bat files by relative name: make sure cmd searches the current directory.
set NoDefaultCurrentDirectoryInExePath=
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Shipping
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%FV_UPROJECT%" -noP4 -utf8output -unattended -platform=Android -cookflavor=ETC2 -clientconfig=%CONFIG% -build -cook -stage -package -pak -iostore -compressed -archive -archivedirectory="%FV_PROJ%\Build\Android_APK" -nocompileeditor -skipbuildeditor
if errorlevel 1 exit /b 1
for /r "%FV_PROJ%\Build\Android_APK" %%F in (*.apk) do copy /y "%%F" "%FV_PROJ%\KlevayaDolina.apk" >nul
echo APK: %FV_PROJ%\KlevayaDolina.apk
