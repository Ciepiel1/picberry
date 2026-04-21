#
# picberry Makefile
#

CC = $(CROSS_COMPILE)clang++
CFLAGS = -Wall -std=c++14 -Wpedantic -l wiringPi
LINKING = -pthread
TARGET = picberry
PREFIX = /usr
BINDIR = $(PREFIX)/bin
SRCDIR = src
BUILDDIR = build
MKDIR = mkdir -p

DEVICES = $(BUILDDIR)/devices/dspic33e.o \
		  $(BUILDDIR)/devices/dspic33f.o \
		  $(BUILDDIR)/devices/pic10f322.o \
		  $(BUILDDIR)/devices/pic16f178x.o \
		  $(BUILDDIR)/devices/pic18fj.o \
		  $(BUILDDIR)/devices/pic24fjxxxga0xx.o \
		  $(BUILDDIR)/devices/pic24fjxxxga3xx.o \
		  $(BUILDDIR)/devices/pic24fjxxga1xx_gb0xx.o \
		  $(BUILDDIR)/devices/pic24fjxxxga1_gb1.o \
		  $(BUILDDIR)/devices/pic24fjxxxga2_gb2.o \
		  $(BUILDDIR)/devices/pic24fxxka1xx.o\
		  $(BUILDDIR)/devices/pic32.o $(BUILDDIR)/devices/pic32_pe.o \

default: prepare picberry

prepare:
	$(MKDIR) $(BUILDDIR)/devices

picberry: $(BUILDDIR)/inhx.o $(DEVICES) $(BUILDDIR)/picberry.o
	$(CC) $(CFLAGS) -o $(TARGET) $(BUILDDIR)/inhx.o $(DEVICES) $(BUILDDIR)/picberry.o $(LINKING)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/devices/%.o: $(SRCDIR)/devices/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

install:
	install -m 0755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	$(RM) $(BINDIR)/$(TARGET)

clean:
	$(RM) $(TARGET) *.o $(BUILDDIR)/*.o $(BUILDDIR)/devices/*.o
