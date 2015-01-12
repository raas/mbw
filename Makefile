#CFLAGS=-O2 -Wall -g
NAME=mbw
TARFILE=${NAME}.tar.gz

mbw: mbw.c
	$(CC) -c -o mbw.o mbw.c
	$(CC) -o mbw mbw.o -lpthread 

clean:
	rm -f mbw mbw.o
	rm -f ${NAME}.tar.gz

${TARFILE}: clean
	 tar cCzf .. ${NAME}.tar.gz --exclude-vcs ${NAME} || true

rpm: ${TARFILE}
	 rpmbuild -ta ${NAME}.tar.gz 
