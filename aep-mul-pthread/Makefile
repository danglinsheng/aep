CC = gcc
CFLAGS += -g -O0 -Wall

all: aep client


client: client.c
	${CC} ${CFLAGS} -o client client.c
aep: ae.c anet.c aep.c  
	${CC} ${CFLAGS} -o aep ae.c anet.c aep.c -lpthread 

clean:
	rm -rf aep client *.o
