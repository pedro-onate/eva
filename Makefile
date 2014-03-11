debug : eva.c eva.h
	gcc -Wall -g -o eva eva.c

release : eva.c eva.h
	gcc -O3 -o eva eva.c

clean :
	rm eva