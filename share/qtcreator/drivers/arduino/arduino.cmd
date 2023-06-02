@echo off

if DEFINED PROCESSOR_ARCHITEW6432 goto :install_wow

:install
if DEFINED PROCESSOR_ARCHITECTURE goto :install_64
"%~dp0\dpinst-x86.exe" /se /sw
exit

:install_64
if %PROCESSOR_ARCHITECTURE%==x86 goto :install_32
"%~dp0\dpinst-amd64.exe" /se /sw
exit

:install_32
"%~dp0\dpinst-x86.exe" /se /sw
exit

:install_wow
"%~dp0\dpinst-amd64.exe" /se /sw
exit