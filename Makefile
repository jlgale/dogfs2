
.PHONEY: all clean

JIMCORE = ../jimcore
DEFINES = _FILE_OFFSET_BITS=64 SIZE_STATS
LIBS = libfuse libjimcore
INCLUDE = $(JIMCORE)
LINK = $(JIMCORE)

OFILES = \
	acache.o \
	chmod.o \
	chown.o \
	cleanup.o \
	create.o \
	dogfs.o  \
	file.o \
	fsync.o \
	getattr.o \
	icache.o \
	mkdir.o \
	open.o \
	readdir.o \
	read.o \
	readi.o \
	readlink.o \
	release.o \
	rename.o \
	resolve.o \
	rmdir.o \
	symlink.o \
	truncate.o \
	unlink.o \
	utime.o \
	write.o \
	writei.o \
#

all: dogfs2

dogfs2: $(OFILES)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) dogfs2 *.o

include $(JIMCORE)/rules.mk
