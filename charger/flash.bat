@echo off
REM ============================================================
REM  Flash firmware to STM32F407VET6 via ST-Link (SWD)
REM  Sử dụng STM32CubeProgrammer CLI
REM ============================================================

REM --- Cấu hình đường dẫn ---
REM Thay đổi nếu CubeProgrammer cài ở chỗ khác
set CUBE_PROG="C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

REM --- Đường dẫn file firmware ---
set BUILD_DIR=%~dp0build\Debug
set ELF_FILE=%BUILD_DIR%\charger.elf

REM --- Kiểm tra CubeProgrammer ---
if not exist %CUBE_PROG% (
    echo [ERROR] STM32CubeProgrammer khong tim thay tai:
    echo         %CUBE_PROG%
    echo.
    echo Hay cai dat hoac sua duong dan trong file nay.
    pause
    exit /b 1
)

REM --- Kiểm tra file ELF ---
if not exist "%ELF_FILE%" (
    echo [ERROR] Khong tim thay firmware: %ELF_FILE%
    echo.
    echo Hay build project truoc: Ctrl+Shift+B trong VSCode
    echo Hoac chay: cmake --build build/Debug
    pause
    exit /b 1
)

echo ============================================================
echo   STM32 Flash Tool
echo   Target : STM32F407VET6
echo   File   : %ELF_FILE%
echo   Method : ST-Link SWD
echo ============================================================
echo.

REM --- Kết nối và flash ---
echo [1/3] Ket noi ST-Link...
%CUBE_PROG% -c port=SWD reset=HWrst -q

if errorlevel 1 (
    echo [ERROR] Khong ket noi duoc ST-Link. Kiem tra:
    echo   - Cap USB ST-Link da cam chua?
    echo   - Driver ST-Link da cai chua?
    echo   - Board da cap nguon chua?
    pause
    exit /b 1
)

echo [2/3] Xoa Flash va ghi firmware...
%CUBE_PROG% -c port=SWD reset=HWrst -e all -d "%ELF_FILE%" -v

if errorlevel 1 (
    echo [ERROR] Flash that bai!
    pause
    exit /b 1
)

echo [3/3] Reset MCU...
%CUBE_PROG% -c port=SWD -hardRst

echo.
echo ============================================================
echo   [OK] Flash thanh cong! MCU da reset va chay firmware.
echo ============================================================
echo.
pause
