@echo off
set keil_compiler_include_floder=%1
set full_name_path=%2
set linker_output_file_name=%3
set project_floder_path=%4
set objects_floder_path=%5

set TARGET_MAP_PATH=Listings
set TARGET_OBJ_PATH=Objects
echo %keil_compiler_include_floder%
echo %full_name_path%
echo %linker_output_file_name%
echo %project_floder_path%
echo %objects_floder_path%

%keil_compiler_include_floder%\..\bin\fromelf.exe --bin -o  bin\ "%full_name_path%"
%keil_compiler_include_floder%\..\bin\fromelf.exe -acd --interleave=source -o "bin\%linker_output_file_name%.disasm" "%full_name_path%"
..\..\..\..\tools\prepend_header\prepend_header.exe -t app_code -b 15 -p "bin\%linker_output_file_name%.bin" -m 1 -i "..\mp.ini" -r "..\..\..\..\tools\keys\rtk_ecdsa.pem" -a "..\..\..\..\tools\keys\key.json" --aesmode CTR
..\..\..\..\tools\md5\md5.exe "bin\%linker_output_file_name%.bin"
..\..\..\..\tools\md5\md5.exe "bin\%linker_output_file_name%_MP.bin"
if exist "bin\%linker_output_file_name%_MP.bin" del "bin\%linker_output_file_name%_MP.bin"

copy "%TARGET_OBJ_PATH%\%linker_output_file_name%.htm" "bin\\%linker_output_file_name%.htm"
copy "%TARGET_MAP_PATH%\%linker_output_file_name%.map" "bin\\%linker_output_file_name%.map"
