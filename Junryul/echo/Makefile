CC = gcc
CFLAGS = -Wall -g

OBJS_COMMON = csapp.o

# 최종 실행파일
TARGETS = echo_client echo_server

all: $(TARGETS)

echo_client: echo_client.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

echo_server: echo_server.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

# .c → .o 규칙
%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(TARGETS)
