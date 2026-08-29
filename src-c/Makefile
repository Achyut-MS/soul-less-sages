CC := gcc
CFLAGS := -std=c23 -Wall -Wextra -Werror -O2
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

clean:
	rm -f $(OBJS) $(TARGET) ../tests/test_md_parser
