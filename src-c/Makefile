CC := gcc
DEVNULL := /dev/null
ifeq ($(OS),Windows_NT)
	DEVNULL := NUL
endif

STD := c23
AUTO_STD := $(shell $(CC) -std=c23 -E - < $(DEVNULL) > $(DEVNULL) 2>&1 && echo c23 || echo c2x)
ifneq ($(AUTO_STD),)
	STD := $(AUTO_STD)
endif

CFLAGS := -std=$(STD) -Wall -Wextra -Werror -O2
TARGET := mdview
SRCS := main.c md_parser.c tokenizer.c error_report.c http.c html_serializer.c file_writer.c platform.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test:
	$(CC) $(CFLAGS) ../tests/test_md_parser.c md_parser.c tokenizer.c error_report.c -o ../tests/test_md_parser
	../tests/test_md_parser

single: mdview_single

mdview_single: mdview_single.c $(SRCS)
	$(CC) $(CFLAGS) mdview_single.c -o $@

clean:
	rm -f $(OBJS) $(TARGET) mdview_single ../tests/test_md_parser
