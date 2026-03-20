@echo off
echo ========================================
echo  Smart Car Access Server
echo ========================================

:: Cai dat dependencies neu chua co
echo [1/2] Kiem tra dependencies...
pip install fastapi uvicorn cryptography zeroconf pydantic --quiet
if %errorlevel% neq 0 (
    echo [ERROR] pip install that bai!
    pause
    exit /b 1
)
echo [OK] Dependencies san sang

:: Chay server
echo.
echo [2/2] Dang khoi dong server tai http://0.0.0.0:8000 ...
echo       Nhan Ctrl+C de dung server
echo ========================================
cd /d "%~dp0"
set PYTHONIOENCODING=utf-8
python -m uvicorn main:app --host 0.0.0.0 --port 8000 --reload

pause
