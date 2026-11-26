@echo off
rem Broker MQTT Mosquitto, mo cong 1883 cho 3 thiet bi ESP32 (xem mosquitto.conf).
cd /d "%~dp0"
set "MOSQ=C:\Program Files\mosquitto\mosquitto.exe"
if not exist "%MOSQ%" goto :thieu
"%MOSQ%" -c mosquitto.conf -v
pause
exit /b
:thieu
echo Khong thay "%MOSQ%".
echo Cai Mosquitto tu https://mosquitto.org/download roi chay lai file nay.
pause
