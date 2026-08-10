@echo off
REM Build dist\netlinktest.exe - links the REAL NetLink receive path
REM (src\plugin\net\NetLink.cpp + SteamP2P.cpp + CoopLog.cpp) against the same
REM vendored+patched ENet the plugin ships, and pins its receive-side bounds
REM checks over an in-memory socket-hook pipe. No game, no KenshiLib, no Steam,
REM no real sockets (src\netlinktest). Windows twin of
REM scripts\linux\build_netlinktest.sh.
setlocal

set "REPO=%~dp0.."
pushd "%REPO%" >nul
set "REPO=%CD%"
popd >nul

set "VS10=C:\Program Files (x86)\Microsoft Visual Studio 10.0"
set "VC=%VS10%\VC"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"

set "PATH=%VC%\bin\amd64;%VC%\bin;%VS10%\Common7\IDE;%SDK%\Bin\x64;%SDK%\Bin;%PATH%"
REM Deliberately NO KenshiLib on the include path: NetLink must not need it,
REM and this build proves it stays that way.
set "INCLUDE=%VC%\include;%SDK%\Include;%REPO%\third_party\vc10_compat;%REPO%\third_party\enet\enet\include"
set "LIB=%VC%\lib\amd64;%SDK%\Lib\x64"

if not exist "%REPO%\dist" mkdir "%REPO%\dist"
if not exist "%REPO%\build\netlinktest" mkdir "%REPO%\build\netlinktest"

set "ENET=%REPO%\third_party\enet\enet"

echo === Building netlinktest.exe (Release^|x64, v100) ===
REM WIN32_LEAN_AND_MEAN matches the plugin's own compile of NetLink.cpp:
REM NetLink.h and SteamP2P.cpp include windows.h BEFORE enet/enet.h, and
REM without it windows.h drags in winsock.h, which collides with winsock2.h.
cl.exe /nologo /O2 /EHsc /W3 /DWIN32 /DWIN32_LEAN_AND_MEAN /DNDEBUG ^
    /Fo"%REPO%\build\netlinktest\\" ^
    /Fe"%REPO%\dist\netlinktest.exe" ^
    "%REPO%\src\netlinktest\main.cpp" ^
    "%REPO%\src\plugin\net\NetLink.cpp" ^
    "%REPO%\src\plugin\net\SteamP2P.cpp" ^
    "%REPO%\src\plugin\CoopLog.cpp" ^
    "%ENET%\callbacks.c" "%ENET%\compress.c" "%ENET%\host.c" "%ENET%\list.c" ^
    "%ENET%\packet.c" "%ENET%\peer.c" "%ENET%\protocol.c" "%ENET%\win32.c" ^
    ws2_32.lib winmm.lib
if errorlevel 1 (
    echo netlinktest build FAILED
    exit /b 1
)
echo netlinktest built: %REPO%\dist\netlinktest.exe
exit /b 0
