@echo off
rem Nen tang: chi so suc khoe + AI + canh bao + dashboard http://localhost:8080
rem Sua 2 dong "set" ngay duoi neu can:
rem   MA  = ma nguoi deo, giong MA_NGUOI_DUNG trong cau_hinh.h cua firmware
rem   PIN = ma phai nhap tren dashboard khi doi so dien thoai hoac tat bao dong; de trong = khong hoi
rem   TELEGRAM_TOKEN, TELEGRAM_CHAT = gui canh bao qua Telegram cho nguoi nha (xem HUONG_DAN.md, muc Telegram)
rem   de trong = khong gui. Khong chia se file nay len mang khi da dien ma bot.
set "MA=bn01"
set "PIN="
set "TELEGRAM_TOKEN="
set "TELEGRAM_CHAT="
cd /d "%~dp0"
set PYTHONIOENCODING=utf-8
set "PY=python"
where python >nul 2>nul || set "PY=py"
if exist "aiot_models\aiot_infer.py" goto :co_ai
echo Chua thay aiot_models\aiot_infer.py: giai nen aiot_models.zip tai tu Colab vao thu muc may_chu de bat AI.
echo Tam thoi chi chay dashboard va chi so, chua co AI.
%PY% may_chu.py web --ma %MA% --pin "%PIN%"
goto :het
:co_ai
%PY% may_chu.py ai --ma %MA% --mo-hinh aiot_models --pin "%PIN%"
:het
pause
