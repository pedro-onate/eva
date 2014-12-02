release : eva.h eva.c
	gcc -O3 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-label -o eva eva.c main.c

debug : eva.h eva.c
	gcc -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-label -o eva eva.c main.c

profile : eva.h eva.c
	gcc -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-label -o eva eva.c main.c -pg

lib : eva.h eva.c

check : src/eva.c src/eva.h tests/check.c
	clang -I ./src -Wall -o tests/check src/eva.c src/parse.c tests/check.c

test : check
	tests/check

clean :
	rm bin/eva3