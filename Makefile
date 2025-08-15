CC = gcc
CFLAGS = -Wall -Wextra -g -fsanitize=address
OBJS = main.o custommem.o rbtree.o rbtree_tmp.o
DEPS = custommem.h rbtree.h rbtree_tmp.h

main: $(OBJS)
	$(CC) $(CFLAGS) -o main $(OBJS)

main.o: main.c $(DEPS)
	$(CC) $(CFLAGS) -c main.c

custommem.o: custommem.c custommem.h rbtree.h
	$(CC) $(CFLAGS) -c custommem.c

rbtree.o: rbtree.c rbtree.h
	$(CC) $(CFLAGS) -c rbtree.c

rbtree_tmp.o: rbtree_tmp.c rbtree_tmp.h
	$(CC) $(CFLAGS) -c rbtree_tmp.c

.PHONY: clean
clean:
	rm -f *.o main
