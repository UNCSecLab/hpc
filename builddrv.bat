@echo on

:: build the driver and move it into win driver dir
cd drv
build
copy objchk_win7_x86\i386\HPCTestDrv.sys C:\Windows\System32\drivers
cd ..\