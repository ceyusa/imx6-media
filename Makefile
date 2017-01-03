CC = arm-linux-gcc

CFLAGS := -O0 -ggdb -Wall -Wextra -Wno-unused-parameter
override CFLAGS += -Wmissing-prototypes -ansi -std=gnu99 -D_GNU_SOURCE

SEND_CFLAGS := $(shell pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0)
SEND_LIBS := $(shell pkg-config --libs gstreamer-1.0 gstreamer-video-1.0)

all:

send_sources := sender.c

send: $(patsubst %.c, %.o, $(send_sources))
send: override CFLAGS += $(SEND_CFLAGS)
send: override LIBS += $(SEND_LIBS)
bins += send

all: $(bins)

%.o:: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -o $@ -c $<

$(bins):
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(bins) *.o *.d

-include *.d

