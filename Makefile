# Compiler
CC := gcc

# Compiler flags
CFLAGS := -Wall -Isrc -O2

# Directories
SRCDIR := src
BUILDDIR := build

# Source files
SRCS := $(wildcard $(SRCDIR)/*.c)
LIBSRC := $(filter-out $(SRCDIR)/main.c, $(SRCS))
EXESRC := $(filter-out $(SRCDIR)/audio.c, $(SRCS))

# Object files
LIBOBJS := $(LIBSRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
EXEOBJS := $(EXESRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Target executables
TARGET := $(BUILDDIR)/main

# Target shared library
LIBRARY := $(BUILDDIR)/libaudio.so

# Libraries to link
LIBS := -lasound

all: $(TARGET)

$(LIBRARY): $(LIBOBJS)
	$(CC) -shared -o $@ $^ $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

$(TARGET): $(EXEOBJS) $(LIBRARY)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean
