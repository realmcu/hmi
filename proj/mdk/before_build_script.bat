@echo off
set flash_bank_path=2M


::1>NUL mkdir     ..\..\..\bin\flash_map_config\flash_%flash_bank_path%
::1>NUL copy      ..\..\..\..\platform\inc\flash_map.h             				..\..\..\bin\flash_map_config\flash_%flash_bank_path%\
::1>NUL copy ..\..\..\bin\flash_map_config\flash_%flash_bank_path%\flash_map.h 	..\..\..\bin\flash_map_config\flash_map.h

