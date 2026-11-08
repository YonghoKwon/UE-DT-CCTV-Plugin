@echo off
setlocal
pushd "%~dp0"
"%~dp0DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe" -Windowed -ForceRes -ResX=1920 -ResY=1080 -PixelStreamingEncoderCodec=H264 %*
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
