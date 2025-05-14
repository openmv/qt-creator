@echo off

if DEFINED PROCESSOR_ARCHITEW6432 goto :install_wow

:install
if DEFINED PROCESSOR_ARCHITECTURE goto :install_64
REG ADD "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
exit

:install_64
if %PROCESSOR_ARCHITECTURE%==x86 goto :install_32
REG ADD "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
exit

:install_32
REG ADD "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
exit

:install_wow
%SystemRoot%\Sysnative\REG ADD "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
exit
