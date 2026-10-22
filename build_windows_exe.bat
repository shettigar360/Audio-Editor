@echo off
echo [1/3] Compiling Windows Icon and Version Resource (app_icon.rc -> app_icon.res)...
windres app_icon.rc -O coff -o app_icon.res

echo [2/3] Building Audio Track (audio_engine.exe) with embedded EXE Thumbnail & VersionInfo...
if not exist "dist\bin" mkdir "dist\bin"
gcc -O2 -std=c11 -Iinclude src/main.c src/daw_tabs.c src/video_player.c app_icon.res -o dist/bin/audio_engine.exe -mwindows -lmingw32 -lSDL2main -lSDL2 -ldwmapi -luser32 -lgdi32 -lwinmm -lshell32 -lcomdlg32 -lavformat -lavcodec -lswscale -lswresample -lavutil -lm

echo [3/3] Creating 'Audio Track' Shortcut (without .exe word) with custom icon...
powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut((Join-Path $PWD 'dist\bin\Audio Track.lnk')); $s.TargetPath = (Join-Path $PWD 'dist\bin\audio_engine.exe'); $s.WorkingDirectory = (Join-Path $PWD 'dist\bin'); $s.IconLocation = (Join-Path $PWD 'app_icon.ico'); $s.Description = 'Audio Track DAW'; $s.Save()" 2>nul

echo Build complete: dist\bin\audio_engine.exe and shortcut 'dist\bin\Audio Track'
