CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11 -pedantic -D_POSIX_C_SOURCE=200809L
INCLUDES = -Iinclude
CHECKPATCH ?= scripts/checkpatch.pl

SRC = src/main.c src/model.c src/parser.c src/rng.c src/term.c
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
	@$(CHECKPATCH) --no-tree --strict --terse --max-line-length=80 src/*.c include/*.h

.PHONY: all clean lint
