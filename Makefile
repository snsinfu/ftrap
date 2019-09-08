PRODUCT = ftrap

OPTFLAGS = \
  -O2

DEFINES = \
  -D_POSIX_C_SOURCE=200112L

CFLAGS = \
  -std=c99 \
  -pedantic \
  -Wall \
  -Wextra \
  -Wconversion \
  -Wsign-conversion \
  $(DEFINES) \
  $(OPTFLAGS)

OBJECTS = \
  src/main.o \
  src/ftrap.o \
  src/watch_list.o

ARTIFACTS = \
  $(PRODUCT) \
  $(OBJECTS)

.PHONY: all clean

all: $(PRODUCT)
	@:

clean:
	rm -f $(ARTIFACTS)

$(PRODUCT): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

src/main.o: src/ftrap.h src/watch_list.h
src/ftrap.o: src/ftrap.h src/watch_list.h
src/watch_list.o: src/watch_list.h
