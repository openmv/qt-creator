@echo off

if DEFINED PROCESSOR_ARCHITEW6432 goto :install_wow

:install
ver | findstr /c:"Version 5." > nul
if %ERRORLEVEL%==0 goto :install_7
ver | findstr /c:"Version 6.0" > nul
if %ERRORLEVEL%==0 goto :install_7
ver | findstr /c:"Version 6.1" > nul
if %ERRORLEVEL%==0 goto :install_7
ver | findstr /c:"Version 6.2" > nul
if %ERRORLEVEL%==0 goto :install_8
ver | findstr /c:"Version 6.3" > nul
if %ERRORLEVEL%==0 goto :install_8_1
goto :install_10

:install_7
if DEFINED PROCESSOR_ARCHITECTURE goto :install_7_64
"%~dp0\..\dfuse\Driver\Win7\x86\dpinst_x86.exe" /se /sw
exit

:install_7_64
if %PROCESSOR_ARCHITECTURE%==x86 goto :install_7_32
"%~dp0\..\dfuse\Driver\Win7\x64\dpinst_amd64.exe" /se /sw
exit

:install_7_32
"%~dp0\..\dfuse\Driver\Win7\x86\dpinst_x86.exe" /se /sw
exit

:install_8
if DEFINED PROCESSOR_ARCHITECTURE goto :install_8_64
"%~dp0\..\dfuse\Driver\Win8\x86\dpinst_x86.exe" /se /sw
exit

:install_8_64
if %PROCESSOR_ARCHITECTURE%==x86 goto :install_8_32
"%~dp0\..\dfuse\Driver\Win8\x64\dpinst_amd64.exe" /se /sw
exit

:install_8_32
"%~dp0\..\dfuse\Driver\Win8\x86\dpinst_x86.exe" /se /sw
exit

:install_8_1
if DEFINED PROCESSOR_ARCHITECTURE goto :install_8_1_64
"%~dp0\..\dfuse\Driver\Win8.1\x86\dpinst_x86.exe" /se /sw
exit

:install_8_1_64
if %PROCESSOR_ARCHITECTURE%==x86 goto :install_8_1_32
"%~dp0\..\dfuse\Driver\Win8.1\x64\dpinst_amd64.exe" /se /sw
exit

:install_8_1_32
"%~dp0\..\dfuse\Driver\Win8.1\x86\dpinst_x86.exe" /se /sw
exit

:install_10
if DEFINED PROCESSOR_ARCHITECTURE goto :install_10_64
"%~dp0\..\dfuse\Driver\Win10\dpinst_x86.exe" /se /sw
exit

:install_10_64
if %PROCESSOR_ARCHITECTURE%==x86 goto :install_10_32
"%~dp0\..\dfuse\Driver\Win10\dpinst_amd64.exe" /se /sw
exit

:install_10_32
"%~dp0\..\dfuse\Driver\Win10\dpinst_x86.exe" /se /sw
exit

:install_wow
ver | findstr /c:"Version 5." > nul
if %ERRORLEVEL%==0 goto :install_wow_7
ver | findstr /c:"Version 6.0" > nul
if %ERRORLEVEL%==0 goto :install_wow_7
ver | findstr /c:"Version 6.1" > nul
if %ERRORLEVEL%==0 goto :install_wow_7
ver | findstr /c:"Version 6.2" > nul
if %ERRORLEVEL%==0 goto :install_wow_8
ver | findstr /c:"Version 6.3" > nul
if %ERRORLEVEL%==0 goto :install_wow_8_1
goto :install_wow_10

:install_wow_7
"%~dp0\..\dfuse\Driver\Win7\x64\dpinst_amd64.exe" /se /sw
exit

:install_wow_8
"%~dp0\..\dfuse\Driver\Win8\x64\dpinst_amd64.exe" /se /sw
exit

:install_wow_8_1
"%~dp0\..\dfuse\Driver\Win8.1\x64\dpinst_amd64.exe" /se /sw
exit

:install_wow_10
"%~dp0\..\dfuse\Driver\Win10\dpinst_amd64.exe" /se /sw
exit
