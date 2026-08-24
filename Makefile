CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
ARFLAGS ?= rcs
PYTHON ?= python3

BUILD := build
LIB := $(BUILD)/libamiapi.a
HELLO := $(BUILD)/hello
HTTPGET := $(BUILD)/httpget

LIB_OBJS := \
	$(BUILD)/context.o \
	$(BUILD)/errors.o \
	$(BUILD)/http.o \
	$(BUILD)/transport_posix.o

.PHONY: all clean test

all: $(LIB) $(HELLO) $(HTTPGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(HELLO): examples/hello.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@

$(HTTPGET): examples/httpget.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@

test: all
	$(HELLO)
	$(PYTHON) tests/test_http.py

clean:
	rm -rf $(BUILD)
