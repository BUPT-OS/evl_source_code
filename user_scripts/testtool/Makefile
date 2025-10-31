# 交叉编译工具链前缀
CROSS_COMPILE = riscv64-linux-gnu-
CC = $(CROSS_COMPILE)gcc

# the exec finally generated
TARGET = tt
# source code
SRC = tt.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -Wall -O2 -o $@ $^

clean:
	rm -f $(TARGET)
