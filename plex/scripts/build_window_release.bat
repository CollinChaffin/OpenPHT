@echo off

call "%VS120COMNTOOLS%vsvars32.bat" || exit /b 1

rd /s /q c:\tmp
rd /s /q upload
md upload

call plex\scripts\fetch-depends-windows.bat || exit /b 1

rd /s /q build-windows-i386
md build-windows-i386
cd build-windows-i386

cmake -GNinja -DCMAKE_INSTALL_PREFIX=output -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTEXTUREPACKERPATH=TexturePacker.exe .. || exit /b 1
ninja release_package || exit /b 1

move c:\tmp\OpenPHT*exe %WORKSPACE%\upload
move OpenPHT*7z %WORKSPACE%\upload
move OpenPHT*symsrv*zip %WORKSPACE%\upload
move c:\tmp\OpenPHT*zip %WORKSPACE%\upload
