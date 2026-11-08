@echo off
setlocal
set "SIGNALLING_URL=ws://127.0.0.1:8888"
if not "%~1"=="" set "SIGNALLING_URL=%~1"
pushd "%~dp0"
"%~dp0DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe" -Windowed -ForceRes -ResX=1920 -ResY=1080 -PixelStreamingEncoderCodec=H264 "-PixelStreamingURL=%SIGNALLING_URL%" %2 %3 %4 %5 %6 %7 %8 %9
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
