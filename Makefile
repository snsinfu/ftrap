PRODUCT = ftrap

CFLAGS = \
  -std=c99 \
  -pedantic \
  -Wall \
  -Wextra \
  -Wconversion \
  -Wsign-conversion \
  -O2


.PHONY: all clean

all: $(PRODUCT)
	@:

clean:
	rm -f $(PRODUCT)
