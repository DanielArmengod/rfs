CC=gcc
CFLAGS=-g -pthread
LIBS=-lfuse3 -lnng

bins=rfsMaster rfsSlave

%.o: %.c
	$(CC) -c $(CFLAGS) $<

.PHONY: all
all: rfsMaster rfsSlave

rfsMaster: rfsMaster.o rfsCommon.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

rfsSlave: rfsSlave.o rfsCommon.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean setup teardown sync
clean:
	rm -f *.o rfsMaster rfsSlave

setup:
	sudo mount -t tmpfs none ./ladoA
	sudo mount -t tmpfs none ./ladoB
	sudo chown daniel: ladoA ladoB

teardown:
	sudo umount ./ladoA
	sudo umount ./ladoB

sync:
	find ladoA ladoB -mindepth 1 -maxdepth 1 -exec rm -rf {} \;
