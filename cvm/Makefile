# Pseudocode C VM - Makefile
# Optimized for maximum performance

CC = gcc
CFLAGS = -O3 -march=native -flto -fomit-frame-pointer -DNDEBUG
CFLAGS += -Wall -Wextra
CFLAGS += -std=gnu11  # Use gnu11 to allow computed gotos

# Debug build
DEBUG_FLAGS = -O0 -g -DDEBUG -fsanitize=address,undefined

SOURCES = main.c lexer.c compiler.c vm.c memory.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = pseudo

.PHONY: all clean debug release

all: release

release: CFLAGS += -O3 -march=native -flto
release: $(TARGET)

debug: CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -std=c11
debug: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Fast single-file compile for development
fast:
	$(CC) -O2 -o $(TARGET) $(SOURCES) -lm

clean:
	rm -f $(TARGET) $(OBJECTS)

# Profile-guided optimization
pgo-generate:
	$(CC) $(CFLAGS) -fprofile-generate -o $(TARGET) $(SOURCES) -lm
	./$(TARGET) ../examples/fibonacci.pseudo
	./$(TARGET) ../examples/primes.pseudo

pgo-use:
	$(CC) $(CFLAGS) -fprofile-use -o $(TARGET) $(SOURCES) -lm

# Run benchmarks
bench: release
	@echo "=== Fibonacci Benchmark ==="
	@./$(TARGET) ../examples/fibonacci.pseudo

test: release
	@echo "=== Running all examples ==="
	@for f in ../examples/*.pseudo; do \
		echo "--- $$f ---"; \
		./$(TARGET) "$$f" | head -10; \
	done
