CC = gcc

DEBUGGING_FLAGS= -Wall -g -p
OPTIMIZING_FLAGS= -Ofast
FLAGS = -std=c99 -g -p -Ofast -Wall 

simpleISS: simpleISS.c 
	$(CC) $(FLAGS) $(DEBUGGING_FLAGS) $(OPTIMIZING_FLAGS) -o $@ $^ 
nonOptimized: simpleISS.c
	$(CC) $(FLAGS) $(DEBUGGING_FLAGS) -o $@ $^ 
	

clean:
	rm -f $(objects) simpleISS 
