CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
ARFLAGS ?= rcs

BUILD := build
LIB := $(BUILD)/libamiapi.a
EXAMPLE := $(BUILD)/hello

LIB_OBJS := \
	$(BUILD)/context.o \
	$(BUILD)/errors.o

.PHONY: all clean test

all: $(LIB) $(EXAMPLE)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/context.o: src/context.c include/amiapi/amiapi.h include/amiapi/errors.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/errors.o: src/errors.c include/amiapi/errors.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(EXAMPLE): examples/hello.c $(LIB) include/amiapi/amiapi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) examples/hello.c $(LIB) -o $@

test: all
	$(EXAMPLE)

clean:
	rm -rf $(BUILD)
