CC = gcc

CFLAGS = -Wall -Wextra -Iinclude

LIBS = -lws2_32

COMMON = protocol.c

SENDER_SRC = src/sender.c \
             src/stop_wait.c \
             src/go_back_n.c \
             src/selective_repeat.c

RECEIVER_SRC = src/receiver.c \
               src/stop_wait.c \
               src/go_back_n.c \
               src/selective_repeat.c

CHANNEL_SRC = src/channel.c


all: sender receiver channel


sender:
	$(CC) $(CFLAGS) $(SENDER_SRC) $(COMMON) -o sender.exe $(LIBS)


receiver:
	$(CC) $(CFLAGS) $(RECEIVER_SRC) $(COMMON) -o receiver.exe $(LIBS)


channel:
	$(CC) $(CFLAGS) $(CHANNEL_SRC) $(COMMON) -o channel.exe $(LIBS)


clean:
	del /Q sender.exe receiver.exe channel.exe 2>NUL