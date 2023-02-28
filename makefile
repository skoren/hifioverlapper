GPP=$(CXX)
CPPFLAGS=-Wall -Wextra -std=c++17 -O3 -g -Izstr/src -Iparallel-hashmap/parallel_hashmap/ -Icxxopts/include -Wno-unused-parameter `pkg-config --cflags zlib` -IMBG/src -Iconcurrentqueue

ODIR=obj
BINDIR=bin
SRCDIR=src

LIBS=`pkg-config --libs zlib`

_DEPS = MatchIndex.h MinimizerIterator.h
DEPS = $(patsubst %, $(SRCDIR)/%, $(_DEPS))

_OBJ = MatchIndex.o MinimizerIterator.o
OBJ = $(patsubst %, $(ODIR)/%, $(_OBJ))

LINKFLAGS = $(CPPFLAGS) -Wl,-Bstatic $(LIBS) -Wl,-Bdynamic -Wl,--as-needed -lpthread -pthread -static-libstdc++

VERSION := Branch $(shell git rev-parse --abbrev-ref HEAD) commit $(shell git rev-parse HEAD) $(shell git show -s --format=%ci)

$(shell mkdir -p bin)
$(shell mkdir -p obj)

$(BINDIR)/main: $(OBJ) $(ODIR)/main.o MBG/lib/mbg.a
	$(GPP) -o $@ $^ $(LINKFLAGS)

$(ODIR)/main.o: $(SRCDIR)/main.cpp $(DEPS) $(OBJ)
	$(GPP) -c -o $@ $< $(CPPFLAGS) -DVERSION="\"$(VERSION)\""

$(ODIR)/%.o: $(SRCDIR)/%.cpp $(DEPS)
	$(GPP) -c -o $@ $< $(CPPFLAGS)

MBG/lib/mbg.a:
	$(MAKE) -C MBG lib

all: $(BINDIR)/main

clean:
	rm -f $(ODIR)/*
	rm -f $(BINDIR)/*
	$(MAKE) -C MBG clean
