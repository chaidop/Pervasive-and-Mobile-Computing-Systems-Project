CC=gcc
CFLAGS=-Wall -g
SRCS = project.c

all: project

project: project.c
	$(CC) $(CFLAGS) project.c -o project -lpthread -lrt