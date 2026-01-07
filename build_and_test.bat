@echo off
setlocal

set BUILD_DIR=build

cmake -S . -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=Release -DJOBKIT_BUILD_TESTS=ON
if errorlevel 1 exit /b 1

cmake --build %BUILD_DIR%
if errorlevel 1 exit /b 1

ctest --test-dir %BUILD_DIR% --output-on-failure
