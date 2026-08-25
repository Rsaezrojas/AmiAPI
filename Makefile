PLATFORM ?= posix

CFLAGS ?= -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
ARFLAGS ?= rcs
PYTHON ?= python3

ifeq ($(PLATFORM),amiga)
ifeq ($(origin CC),default)
CC := m68k-amigaos-gcc
endif
ifeq ($(origin AR),default)
AR := m68k-amigaos-ar
endif
CFLAGS += -m68020
CPPFLAGS += -I/opt/amiga/AmiSSL/Developer/include
endif

BUILD := build
LIB := $(BUILD)/libamiapi.a
HELLO := $(BUILD)/hello
HTTPGET := $(BUILD)/httpget
REQUESTGET := $(BUILD)/requestget
REQUESTSEND := $(BUILD)/requestsend
TEST_JSON := $(BUILD)/test_json
JSONHTTP := $(BUILD)/jsonhttp
JSONSEND := $(BUILD)/jsonsend

ifeq ($(PLATFORM),posix)
TRANSPORT_SRC := src/transport_posix.c
LDLIBS += -lssl -lcrypto
else ifeq ($(PLATFORM),amiga)
TRANSPORT_SRC := src/transport_amiga.c
else
$(error Unknown PLATFORM '$(PLATFORM)'; use posix or amiga)
endif

TRANSPORT_OBJ := $(BUILD)/transport.o

LIB_OBJS := \
	$(BUILD)/context.o \
	$(BUILD)/errors.o \
	$(BUILD)/http.o \
	$(BUILD)/json.o \
	$(BUILD)/json_http.o \
	$(TRANSPORT_OBJ)

.PHONY: all clean test print-config

all: $(LIB) $(HELLO) $(HTTPGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/context.o: src/context.c src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/errors.o: src/errors.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/http.o: src/http.c src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/json.o: src/json.c include/amiapi/json.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/json_http.o: src/json_http.c include/amiapi/json_http.h include/amiapi/json.h include/amiapi/amiapi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TRANSPORT_OBJ): $(TRANSPORT_SRC) src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(TRANSPORT_SRC) -o $@

$(LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(HELLO): examples/hello.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(HTTPGET): examples/httpget.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(REQUESTGET): tests/requestget.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(REQUESTSEND): tests/requestsend.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(TEST_JSON): tests/test_json.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(JSONHTTP): tests/jsonhttp.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(JSONSEND): tests/jsonsend.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: all
ifeq ($(PLATFORM),posix)
	$(MAKE) $(REQUESTGET) $(REQUESTSEND) $(TEST_JSON) $(JSONHTTP) $(JSONSEND)
	$(HELLO)
	$(PYTHON) tests/test_http.py
	$(TEST_JSON)
else
	@echo "Host integration tests are only run with PLATFORM=posix."
endif

print-config:
	@echo "PLATFORM=$(PLATFORM)"
	@echo "CC=$(CC)"
	@echo "AR=$(AR)"
	@echo "TRANSPORT_SRC=$(TRANSPORT_SRC)"

clean:
	rm -rf $(BUILD)
