CC := gcc
DEVNULL := /dev/null
EXE :=
NET_LIBS :=
RM = rm -f

ifeq ($(OS),Windows_NT)
	DEVNULL := NUL
	EXE := .exe
	NET_LIBS := -lws2_32 -lshell32
	RM = powershell -Command "\"$(OBJS) $(TARGET) mdview_single$(EXE) ../tests/test_parser$(EXE)\" -split ' ' | ForEach-Object { if ($$_) { Remove-Item -Force -ErrorAction SilentlyContinue $$_ } }"
endif

STD := c23
AUTO_STD := $(shell $(CC) -std=c23 -E - < $(DEVNULL) > $(DEVNULL) 2>&1 && echo c23 || echo c2x)
ifneq ($(AUTO_STD),)
	STD := $(AUTO_STD)
endif

CFLAGS := -std=$(STD) -Wall -Wextra -Werror -O2
TARGET := mdview$(EXE)
SRCS := main.c md_parser.c tokenizer.c error_report.c http.c html_serializer.c file_writer.c platform.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean test single

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(NET_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test:
	$(CC) $(CFLAGS) ../tests/test_parser.c md_parser.c tokenizer.c error_report.c -o ../tests/test_parser$(EXE)
	../tests/test_parser$(EXE)

single: mdview_single$(EXE)

mdview_single$(EXE): mdview_single.c $(SRCS)
	$(CC) $(CFLAGS) mdview_single.c -o $@ $(NET_LIBS)

clean:
	-$(RM)
