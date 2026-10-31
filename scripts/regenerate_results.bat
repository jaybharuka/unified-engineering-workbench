@echo off
rem Regenerates every raw CSV in results\ (run from the package root; no admin rights needed).
cd /d "%~dp0\.."
bin\wb_cli.exe all --out results
bin\wb_cli.exe gen-samples --out data
echo.
echo Done. Optional plots: python tools\make_plots.py
pause
