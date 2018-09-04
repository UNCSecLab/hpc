@echo on

sc start HPCTestDrv
timeout 1

testcode\test.exe
timeout 5

sc stop HPCTestDrv
timeout 1