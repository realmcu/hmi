# tests/app_time/Makefile
APP := ../..
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g \
          -I$(APP)/component/posix/include \
          -I$(APP)/component/posix/examples/port \
          -I$(APP)/app/app_core \
          -I$(APP)/app/app_time \
          -I.

run: test_app_time
	@TZ= ./test_app_time
	@TZ=America/New_York ./test_app_time
	@TZ=Asia/Shanghai ./test_app_time

test_app_time: test_app_time.c posix_stub.c $(APP)/app/app_time/app_time.c posix_stub.h
	$(CC) $(CFLAGS) -o $@ test_app_time.c posix_stub.c $(APP)/app/app_time/app_time.c

clean:
	rm -f test_app_time

.PHONY: run clean
