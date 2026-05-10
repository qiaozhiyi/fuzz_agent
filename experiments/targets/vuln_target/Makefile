CC=afl-clang-fast

all: vuln

vuln: vuln.c
	$(CC) -O0 -g -fno-stack-protector -o vuln vuln.c

clean:
	rm -f vuln
