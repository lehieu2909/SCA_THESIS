@echo off
echo ========================================
echo  Stop Smart Car Access Server
echo ========================================

:: Tim va tat process uvicorn/python dang chay tren port 8000
for /f "tokens=5" %%a in ('netstat -aon ^| findstr ":8000" ^| findstr "LISTENING"') do (
    echo [INFO] Tim thay process PID: %%a tren port 8000
    taskkill /PID %%a /F >nul 2>&1
    if %errorlevel% equ 0 (
        echo [OK] Da tat process %%a
    ) else (
        echo [WARN] Khong the tat process %%a
    )
)

:: Kiem tra lai
netstat -aon | findstr ":8000" | findstr "LISTENING" >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo [OK] Server da dung thanh cong!
) else (
    echo.
    echo [WARN] Van con process tren port 8000, thu chay voi quyen Admin
)

echo ========================================
pause
