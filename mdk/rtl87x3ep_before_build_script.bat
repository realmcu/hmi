@echo off
set flash_bank_path=%1
set ic_type=%2
set target=%3


copy ..\..\..\..\..\bin\%ic_type%\flash_map_config\%flash_bank_path%\flash_%flash_bank_path%\flash_map.h ..\..\..\..\..\bin\%ic_type%\flash_map_config\flash_map.h
copy ..\..\..\..\..\bin\%ic_type%\upperstack_stamp\%flash_bank_path%\upperstack_compile_stamp.h ..\..\..\..\..\bin\%ic_type%\upperstack_stamp\upperstack_compile_stamp.h
