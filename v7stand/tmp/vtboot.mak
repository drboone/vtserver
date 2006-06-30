vtboot: vtboot.o
	strip $@.o
	dd if=$@.o of=vtboot bs=16 skip=1
	rm -f $@.o
