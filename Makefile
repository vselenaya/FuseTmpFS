# Это комментарий, который говорит, что переменная CC указывает компилятор, используемый для сборки
CC=g++
# Это еще один комментарий. Он поясняет, что в переменной CFLAGS лежат флаги, которые передаются компилятору
CFLAGS=-Wall -Werror -Wextra -D_FILE_OFFSET_BITS=64 -g -Wno-error=terminate -Wno-error=missing-field-initializers  # последний флг, чтобы не было ошибки из-за неинициализированных полей fuse_operations
# Название программы:
PROGRAM=tm

main: tmpfs.cpp
	$(CC) $(CFLAGS) tmpfs.cpp -o $(PROGRAM) -lfuse
clean:
	rm $(PROGRAM)