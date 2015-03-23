CPPFLAGS=-DENABLE_REPL
CFLAGS=-std=c99 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-label
INC=eva.h
SRC=eva.c
OUTPUT=eva

release : $(INC) $(SRC)
	$(CC) -O3 $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(OUTPUT)

debug : $(INC) $(SRC)
	$(CC) -g $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(OUTPUT)

check : $(INC) $(SRC) tests/check.c
	$(CC) -I . $(CFLAGS) tests/check.c -o tests/check

test : check
	tests/check

clean :
	rm -rf eva eva.dSYM