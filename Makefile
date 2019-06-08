CC=clang
CFLAGS=-O3 -fomit-frame-pointer -Isrc/libdivsufsort/include -Isrc/xxhash -Isrc
OBJDIR=obj
LDFLAGS=
STRIP=strip

$(OBJDIR)/%.o: src/../%.c
	@mkdir -p '$(@D)'
	$(CC) $(CFLAGS) -c $< -o $@

APP := lz4ultra

OBJS := $(OBJDIR)/src/lz4ultra.o
OBJS += $(OBJDIR)/src/dictionary.o
OBJS += $(OBJDIR)/src/expand_block.o
OBJS += $(OBJDIR)/src/expand_inmem.o
OBJS += $(OBJDIR)/src/expand_streaming.o
OBJS += $(OBJDIR)/src/frame.o
OBJS += $(OBJDIR)/src/lib.o
OBJS += $(OBJDIR)/src/matchfinder.o
OBJS += $(OBJDIR)/src/shrink_block.o
OBJS += $(OBJDIR)/src/shrink_context.o
OBJS += $(OBJDIR)/src/shrink_inmem.o
OBJS += $(OBJDIR)/src/shrink_streaming.o
OBJS += $(OBJDIR)/src/stream.o
OBJS += $(OBJDIR)/src/libdivsufsort/lib/divsufsort.o
OBJS += $(OBJDIR)/src/libdivsufsort/lib/divsufsort_utils.o
OBJS += $(OBJDIR)/src/libdivsufsort/lib/sssort.o
OBJS += $(OBJDIR)/src/libdivsufsort/lib/trsort.o
OBJS += $(OBJDIR)/src/xxhash/xxhash.o

all: $(APP)

$(APP): $(OBJS)
	@mkdir -p ../../bin/posix
	$(CC) $^ $(LDFLAGS) -o $(APP)
	$(STRIP) $(APP)

clean:
	@rm -rf $(APP) $(OBJDIR)

