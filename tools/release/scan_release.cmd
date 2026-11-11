@echo off
rem Taint scan of a release folder against this machine's retail-derived files (dirty side).
rem usage: scan_release.cmd [RELEASE_DIR]   (default E:\gtav-native\dist\gtav-recomp-win64)
set REL=%1
if "%REL%"=="" set REL=E:\gtav-native\dist\gtav-recomp-win64
set W=C:\Windows\System32
python -I E:\gtav-native\tools\release\taint_scan.py "%REL%" ^
  E:\gtav-rex\game\basefile_tu27.bin E:\gtav-rex\game\disc2_files\default.xexp E:\gtav-rex\game\update\update.rpf ^
  E:\gtav-native\build\shader_src_all E:\gtav-native\build\spirv E:\gtav-native\gtav\generated\default ^
  --control %W%\ucrtbase.dll %W%\msvcp140.dll %W%\ntdll.dll %W%\KernelBase.dll %W%\rsaenh.dll ^
  %W%\bcryptprimitives.dll %W%\advapi32.dll %W%\crypt32.dll %W%\dbghelp.dll %W%\kernel32.dll %W%\user32.dll ^
  E:\gtav-rex\rexglue-sdk\src E:\gtav-rex\rexglue-sdk\include E:\gtav-rex\rexglue-sdk\resources\templates
