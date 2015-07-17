CPPFLAGS=-DENABLE_REPL
CFLAGS=-std=c99 -Wall
CFLAGS2=-Wno-unused-label -Wno-unused-function -Wno-unused-variable
CFLAGS += $(CFLAGS2)
INC=eva.h
SRC=eva.c
OUTPUT=eva

debug : $(INC) $(SRC)
	$(CC) -g $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(OUTPUT)

release : $(INC) $(SRC)
	$(CC) -O3 $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(OUTPUT)

check : $(INC) $(SRC) test.c
	$(CC) $(CFLAGS) test.c -o check

test : check
	./check && rm ./check

clean :
	rm -rf eva eva.dSYM check