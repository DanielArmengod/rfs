CC=gcc
CFLAGS=-g -pthread
LIBS=-lfuse3 -lnng

bins=rfsMaster rfsSlave

%.o: %.c
	$(CC) -c $(CFLAGS) $<

rfsMaster: rfsMaster.o rfsCommon.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

rfsSlave: rfsSlave.o rfsCommon.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: all
all: rfsMaster rfsSlave

.PHONY: clean
clean:
	rm *.o rfsMaster rfsSlave
