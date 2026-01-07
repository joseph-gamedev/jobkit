@echo off
setlocal

set BUILD_DIR=build
set CONFIG=Release

cmake -S . -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=%CONFIG% -DJOBKIT_BUILD_TESTS=ON
if errorlevel 1 exit /b 1

cmake --build %BUILD_DIR% --config %CONFIG%
if errorlevel 1 exit /b 1

ctest --test-dir %BUILD_DIR% -C %CONFIG% --output-on-failure
