#CFLAGS=-O2 -Wall -g
NAME=mbw

mbw: mbw.c

clean:
	rm -f mbw
	rm -f ${NAME}.tar.gz

rpm: clean
	 tar cCzf .. ${NAME}.tar.gz ${NAME}
	 rpmbuild -ta ${NAME}.tar.gz 
