@echo off
echo === Building Maxwell Charger Demo ===
echo.
echo Installing dependencies...
pip install -r requirements.txt
echo.
echo Building .exe...
pyinstaller --onefile --windowed --name "ChargerDemo" charger_demo.py
echo.
echo Done! EXE at: dist\ChargerDemo.exe
pause
