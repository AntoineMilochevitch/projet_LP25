CC = gcc
CFLAGS = -Wall -Wextra -I/usr/include
LDFLAGS = -lssl -lcrypto

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
EXECUTABLE = prg

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXECUTABLE)