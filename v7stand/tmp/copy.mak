copy: M.o conf.o copy.o ubmapset.o libsa.a
	ld -X  M.o conf.o copy.o ubmapset.o libsa.a -lkern -lc
	strip a.out
	dd if=a.out of=copy bs=16 skip=1
	rm -f a.out
