@echo off
rem Cai thu vien Python cho may chu. Chi can chay 1 lan.
cd /d "%~dp0"
set "PY=python"
where python >nul 2>nul || set "PY=py"
%PY% -m pip install -r requirements.txt
pause
