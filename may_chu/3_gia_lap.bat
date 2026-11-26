@echo off
rem Bo gia lap 3 thiet bi: phat du lieu qua MQTT giong vong tay, vong nguc, radar that.
cd /d "%~dp0"
set PYTHONIOENCODING=utf-8
set "PY=python"
where python >nul 2>nul || set "PY=py"
echo Chon kich ban:
echo   1 = binh_thuong     nhip xoang MIT-BIH 103, khong canh bao
echo   2 = rung_nhi        rung nhi MIT-BIH 221, canh bao nguy co cao
echo   3 = ngung_tho       tu giay 90, cu 45 giay thi ngung tho 20 giay
echo   4 = te_nga          te nga o giay 30, dem nguoc 15 giay roi bao nguoi nha
echo   5 = sos             bam nut SOS tren radar o giay 30
echo   6 = ngoai_tam_thu   ngoai tam thu nhi MIT-BIH 232, canh bao nhe
set "CHON=1"
set /p "CHON=Nhap 1-6 roi Enter [1]: "
set "KB=binh_thuong"
if "%CHON%"=="2" set "KB=rung_nhi"
if "%CHON%"=="3" set "KB=ngung_tho"
if "%CHON%"=="4" set "KB=te_nga"
if "%CHON%"=="5" set "KB=sos"
if "%CHON%"=="6" set "KB=ngoai_tam_thu"
%PY% gia_lap_thiet_bi.py --ma bn01 --kich-ban %KB%
pause
