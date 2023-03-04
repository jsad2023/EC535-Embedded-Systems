ktimer: ktimer.c helper.c ktimer.h
	arm-linux-gnueabihf-gcc -static -o tmp ktimer.c helper.c 
	arm-linux-gnueabihf-strip -S -o $@ tmp
	rm tmp
clean:
	rm -f ktimer
