@echo off
echo ==========================================
echo   SacPin Charger Control - Build EXE
echo ==========================================
echo.

echo [1/3] Installing dependencies...
pip install -r requirements.txt
echo.

echo [2/3] Building EXE with PyInstaller...
pyinstaller --onefile --windowed ^
    --name "SacPinChargerControl" ^
    --add-data "models;models" ^
    --add-data "services;services" ^
    --add-data "viewmodels;viewmodels" ^
    --add-data "views;views" ^
    --hidden-import=serial ^
    --hidden-import=serial.tools.list_ports ^
    --collect-all=PyQt5 ^
    --collect-all=pyqtgraph ^
    main.py
echo.

echo [3/3] Done!
echo.
echo EXE location: dist\SacPinChargerControl.exe
echo.
pause
