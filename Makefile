debug : eva.c eva.h
	gcc -o eva eva.c

release : eva.c eva.h
	gcc -O3 -o eva eva.c

clean :
	rm eva