# ==============================================================================
# Makefile for Zero-Dep Markdown Viewer / Visualizer
# Conforms to ISO C23 standard library + POSIX/Win32 APIs.
#
# Requirements:
#   - Build on Linux (gcc/clang) and Windows (MinGW-w64 / MSYS2)
#   - Flags: -Wall -Wextra -Werror -std=c23 -O2
#
# Targets:
#   all         - Compile default mdview executable
#   single      - Compile amalgamated single-translation-unit executable (for +5 bonus)
#   asan        - Build with AddressSanitizer and UndefinedBehaviorSanitizer
#   coverage    - Build with gcov coverage instrumentations
#   fuzz        - Build the round-trip grammar mutation fuzzer
#   commonmark  - Build CommonMark compliance validation harness
#   test        - Compile and run all unit test suites
#   clean       - Clean all compiler artifacts and binaries
#
# Cross-Compilation Hint (Linux to Windows via MinGW-w64):
#   make CC=x86_64-w64-mingw32-gcc LDFLAGS="-lws2_32 -lshell32" EXE=.exe
# ==============================================================================

CC = gcc
LDFLAGS =
EXE =
DURATION = 300

NET_LIBS =

# Detect OS and adjust clean / link flags
ifeq ($(OS),Windows_NT)
    EXE = .exe
    NET_LIBS = -lws2_32 -lshell32
    RM = powershell -Command "Remove-Item -Force -ErrorAction SilentlyContinue"
    DEVNULL = NUL
    FILTER_COV = findstr /C:"Lines executed"
    RUN_INTEGRATION = powershell -ExecutionPolicy Bypass -File ./tests/integration_tests.ps1
    NO_BUILD_FLAG = -NoBuild
    define CLEAN_RECIPE
	-$(RM) mdview.exe, mdview_single.exe, fuzz_roundtrip.exe, run_conformance.exe, parser_check.exe, html_serializer_check.exe, platform_check.exe, file_writer_check.exe, net_payload_check.exe
	-$(RM) src-c/*.o, src-c/*.gcda, src-c/*.gcno
	-$(RM) tests/*.gcda, tests/*.gcno
	-$(RM) tests/commonmark/*.gcda, tests/commonmark/*.gcno
	-$(RM) *.gcda, *.gcno, *.gcov
    endef
else
    EXE =
    NET_LIBS =
    RM = rm -f
    DEVNULL = /dev/null
    FILTER_COV = grep "Lines executed"
    RUN_INTEGRATION = bash ./tests/integration_tests.sh
    NO_BUILD_FLAG = --no-build
    define CLEAN_RECIPE
	-$(RM) mdview mdview_single fuzz_roundtrip run_conformance parser_check html_serializer_check platform_check file_writer_check net_payload_check
	-$(RM) src-c/*.o src-c/*.gcda src-c/*.gcno
	-$(RM) tests/*.gcda tests/*.gcno
	-$(RM) tests/commonmark/*.gcda tests/commonmark/*.gcno
	-$(RM) *.gcda *.gcno *.gcov
    endef
endif

# Senior Engineer Tip: GCC 13 and below support -std=c2x but not -std=c23
# (which was added in GCC 14). We auto-detect support and fallback to -std=c2x
# if -std=c23 is rejected, guaranteeing compilation on older compilers while
# keeping -std=c23 as the default.
STD = c23
AUTO_STD := $(shell $(CC) -std=c23 -E - < $(DEVNULL) > $(DEVNULL) 2>&1 && echo c23 || echo c2x)
ifneq ($(AUTO_STD),)
    STD = $(AUTO_STD)
endif

CFLAGS = -Wall -Wextra -Werror -std=$(STD) -O2

# Source and Object files
SRC = src-c/main.c src-c/md_parser.c src-c/html_serializer.c src-c/http.c src-c/file_writer.c src-c/platform.c src-c/tokenizer.c src-c/error_report.c
OBJ = $(SRC:.c=.o)

.PHONY: all single asan coverage fuzz commonmark test clean

# Default Target
all: mdview$(EXE)

mdview$(EXE): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o mdview$(EXE) $(LDFLAGS) $(NET_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Single Translation Unit Amalgamated Target
single: mdview_single$(EXE)

mdview_single$(EXE): src-c/mdview_single.c $(SRC)
	$(CC) $(CFLAGS) src-c/mdview_single.c -o mdview_single$(EXE) $(LDFLAGS) $(NET_LIBS)

# AddressSanitizer / UBSan Target for runtime auditing
asan: CFLAGS = -Wall -Wextra -Werror -std=$(STD) -fsanitize=address,undefined -g -O1 -DTEST_MODE
asan: DURATION = 10
asan: clean test fuzz_roundtrip$(EXE)
	@echo ===========================================
	@echo Running Fuzzing Smoke Test under AddressSanitizer
	@echo ===========================================
	./fuzz_roundtrip$(EXE)

# Coverage Analysis Target
coverage: CFLAGS = -Wall -Wextra -Werror -std=$(STD) -O0 -g --coverage -DTEST_MODE
coverage: clean test
	@echo ===========================================
	@echo Generating Coverage Reports
	@echo ===========================================
	-@lcov --capture --directory . --output-file coverage.info > $(DEVNULL) 2>&1 || true
	@gcov -o test_parser-md_parser.gcno src-c/md_parser.c | $(FILTER_COV)
	@gcov -o test_html_serializer-html_serializer.gcno src-c/html_serializer.c | $(FILTER_COV)
	@gcov -o test_platform-platform.gcno src-c/platform.c | $(FILTER_COV)
	@gcov -o test_file_writer-file_writer.gcno src-c/file_writer.c | $(FILTER_COV)
	@gcov src-c/http.c | $(FILTER_COV)
	@gcov src-c/main.c | $(FILTER_COV)

# Fuzzing target — DURATION= sets the budget in seconds (default 300 = 5 min)
fuzz: fuzz_roundtrip$(EXE)
	@echo ===========================================
	@echo Running Round-Trip Fuzzer \($(DURATION)s budget\)
	@echo ===========================================
	./fuzz_roundtrip$(EXE)

fuzz_roundtrip$(EXE): tests/fuzz_roundtrip.c src-c/md_parser.c src-c/html_serializer.c src-c/tokenizer.c src-c/error_report.c
	$(CC) $(CFLAGS) -DFUZZ_DURATION_SECS=$(DURATION) tests/fuzz_roundtrip.c src-c/md_parser.c src-c/html_serializer.c src-c/tokenizer.c src-c/error_report.c -o fuzz_roundtrip$(EXE) $(LDFLAGS)

# CommonMark target
commonmark: run_conformance$(EXE)

run_conformance$(EXE): tests/commonmark/run_conformance.c src-c/md_parser.c src-c/tokenizer.c src-c/error_report.c
	$(CC) $(CFLAGS) tests/commonmark/run_conformance.c src-c/md_parser.c src-c/tokenizer.c src-c/error_report.c -o run_conformance$(EXE) $(LDFLAGS)

# Unit testing targets
test: mdview$(EXE) parser_check$(EXE) html_serializer_check$(EXE) platform_check$(EXE) file_writer_check$(EXE) net_payload_check$(EXE)
	@echo ===========================================
	@echo Running Automated Unit Test Suites
	@echo ===========================================
	./parser_check$(EXE)
	./html_serializer_check$(EXE)
	./platform_check$(EXE)
	./file_writer_check$(EXE)
	./net_payload_check$(EXE)
	@echo ===========================================
	@echo Running Integration Test Suite
	@echo ===========================================
	$(RUN_INTEGRATION) $(NO_BUILD_FLAG)

parser_check$(EXE): tests/test_parser.c src-c/md_parser.c src-c/tokenizer.c src-c/error_report.c
	$(CC) $(CFLAGS) tests/test_parser.c src-c/md_parser.c src-c/tokenizer.c src-c/error_report.c -o parser_check$(EXE) $(LDFLAGS)

html_serializer_check$(EXE): tests/test_html_serializer.c src-c/html_serializer.c
	$(CC) $(CFLAGS) tests/test_html_serializer.c src-c/html_serializer.c -o html_serializer_check$(EXE) $(LDFLAGS)

platform_check$(EXE): tests/test_platform.c src-c/platform.c
	$(CC) $(CFLAGS) tests/test_platform.c src-c/platform.c -o platform_check$(EXE) $(LDFLAGS) $(NET_LIBS)

file_writer_check$(EXE): tests/test_file_writer.c src-c/file_writer.c
	$(CC) $(CFLAGS) -DTEST_MODE tests/test_file_writer.c src-c/file_writer.c -o file_writer_check$(EXE) $(LDFLAGS) $(NET_LIBS)

net_payload_check$(EXE): tests/test_http.c src-c/http.c src-c/md_parser.c src-c/html_serializer.c src-c/file_writer.c src-c/platform.c src-c/tokenizer.c src-c/error_report.c
	$(CC) $(CFLAGS) -DTEST_MODE tests/test_http.c src-c/http.c src-c/md_parser.c src-c/html_serializer.c src-c/file_writer.c src-c/platform.c src-c/tokenizer.c src-c/error_report.c -o net_payload_check$(EXE) $(LDFLAGS) $(NET_LIBS)

# Cleanup
clean:
	$(CLEAN_RECIPE)
