@echo off
set keil_compiler_include_floder=%1
set full_name_path=%2
set linker_output_file_name=%3
echo %keil_compiler_include_floder%
echo %full_name_path%
echo %linker_output_file_name%

%keil_compiler_include_floder%\..\bin\fromelf.exe --bin -o  "bin/" "%full_name_path%"
REM ---------------------------------------------
REM Optional: Generate a disassembly file for debugging.
REM To enable, remove the "::" at the beginning of the command.
REM Note: The "--interleave=source" flag interleaves source code with disassembled code.
REM This helps in correlating the source with its machine code, but the file will
REM contain your source code. Make sure to protect its confidentiality.
REM ---------------------------------------------
::%keil_compiler_include_floder%\..\bin\fromelf.exe -acd --interleave=source -o "bin\%linker_output_file_name%.disasm" "%full_name_path%"

::..\..\..\..\tool\prepend_header\prepend_header.exe -v

..\..\..\..\tool\prepend_header\prepend_header.exe -t app_code -b 9 -p "bin\%linker_output_file_name%.bin" -m 1 -c sha256 -a "..\..\..\..\tool\key.json"
..\..\..\..\tool\MD5\md5.exe "bin\%linker_output_file_name%.bin"
..\..\..\..\tool\MD5\md5.exe "bin\%linker_output_file_name%_MP.bin"
del "bin\%linker_output_file_name%_MP.bin"