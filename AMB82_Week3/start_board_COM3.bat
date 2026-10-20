@echo off
cd /d "%~dp0"
python server.py --port COM3 --baud 115200
pause
