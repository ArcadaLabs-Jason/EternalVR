@echo off
rem Starts the EternalVR launcher with the Parallel Eye Rendering box shown (Play tab, Picture).
rem Tick the box there to turn it on, untick it to turn it off. Experimental: see README-ALPHA.md.
set ETERNALVR_SHOW_PARALLEL_EYES=1
start "" "%~dp0EternalVR.Launcher.exe"
