CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11 -pedantic -D_POSIX_C_SOURCE=200809L
INCLUDES = -Iinclude
CHECKPATCH ?= scripts/checkpatch.pl
CLANG_FORMAT ?= clang-format
CHECKPATCH_TYPES = \
	TRAILING_WHITESPACE,WHITESPACE_AFTER_LINE_CONTINUATION, \
	QUOTED_WHITESPACE_BEFORE_NEWLINE,DOS_LINE_ENDINGS, \
	LONG_LINE,LONG_LINE_COMMENT,LONG_LINE_STRING

SRC = src/main.c src/app.c src/runner.c src/log.c src/model.c src/parser.c src/rng.c src/term.c
OBJ = $(SRC:.c=.o)
BIN = bin/cram

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

lint:
	@command -v $(CHECKPATCH) >/dev/null 2>&1 || { echo "checkpatch.pl not found"; exit 1; }
	@$(MAKE) fmt-check
	@$(CHECKPATCH) --no-tree --root=. --strict --terse --show-types --max-line-length=80 \
		--types="$(CHECKPATCH_TYPES)" \
		--file src/*.c include/*.h

fmt:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@$(CLANG_FORMAT) -i src/*.c include/*.h

fmt-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@$(CLANG_FORMAT) -n -Werror src/*.c include/*.h

.PHONY: all clean lint fmt fmt-check
