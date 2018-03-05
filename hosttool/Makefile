DEBUGFLAGS	= -g
CFLAGS		= -I../main $(DEBUGFLAGS)
TARGETS 	= irtx
OBJS		= irtx.o irslib.o

all: $(TARGETS)

irtx: $(OBJS)
	$(CC) -o $(LFLAGS) $@ $(OBJS)

clean:
	rm -f $(TARGETS) $(OBJS)
