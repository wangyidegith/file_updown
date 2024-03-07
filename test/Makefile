CC = gcc
CFLAGS = -Wall -Wextra -g

SRCDIR = ../src
BUILDDIR = test_build
BINDIR = test_bin

all: fs fc

fs: $(BUILDDIR)/fserver.o $(BUILDDIR)/m_net.o $(BUILDDIR)/file_updown.o
	$(CC) -o $(BINDIR)/fs $^

fc: $(BUILDDIR)/fclient.o $(BUILDDIR)/m_net.o $(BUILDDIR)/file_updown.o
	$(CC) -o $(BINDIR)/fc $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/m_net/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(BUILDDIR)/%.o: test_src/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -rf $(BINDIR)/* $(BUILDDIR)/*

.PHONY: all clean
