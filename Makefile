CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lcurl -lcjson

TARGET = stock_monitor
SRC = monitor.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean