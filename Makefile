CC = gcc

ifeq ($(CC),clang)
  STACK_FLAGS = -fno-stack-protector -Wl,-allow_stack_execute
else
  STACK_FLAGS = -fno-stack-protector -z execstack
endif

CFLAGS = ${STACK_FLAGS} -Wall -Iutil -Iatm -Ibank -Irouter -I/usr/include/openssl -I.

all: bin bin/atm bin/bank bin/router bin/dupingRouter bin/init

bin:
	mkdir -p bin

bin/atm : util/list.c util/hash_table.c atm/atm-main.c atm/atm.c
	${CC} ${CFLAGS} util/list.c util/hash_table.c atm/atm.c atm/atm-main.c -o bin/atm -lcrypto

bin/bank : util/list.c util/hash_table.c bank/bank-main.c bank/bank.c
	${CC} ${CFLAGS} util/list.c util/hash_table.c bank/bank.c bank/bank-main.c -o bin/bank -lcrypto

bin/router : router/router-main.c router/router.c
	${CC} ${CFLAGS} router/router.c router/router-main.c -o bin/router

bin/dupingRouter : router/duping-router-main.c router/router.c
	${CC} ${CFLAGS} router/router.c router/duping-router-main.c -o bin/duping-router

bin/init :
	cp init bin
	chmod +x bin/init

test : util/list.c util/list_example.c util/hash_table.c util/hash_table_example.c
	${CC} ${CFLAGS} util/list.c util/list_example.c -o bin/list-test
	${CC} ${CFLAGS} util/list.c util/hash_table.c util/hash_table_example.c -o bin/hash-table-test

clean:
	cd bin && rm -f atm bank router list-test hash-table-test init duping-router
	