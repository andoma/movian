@echo off
cd %~dp0
telxcc -i "%1" -o "%~dpn1.srt"
pause
