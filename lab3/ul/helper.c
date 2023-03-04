/*
Name: Justin Sadler
Date: 02-03-2023
Description: Source code file with helper functions for user-space program
Sources:
	https://stackoverflow.com/questions/9655202/how-to-convert-integer-to-string-in-c
*/

#include "ktimer.h"

// Reads /proc/mytimer.
// Assumes is a valid character array of an appropriate size
static void readProcFS(char * buf, unsigned int size) {
    int proc_fd;
    // Open /proc/mytimer
	proc_fd = open("/proc/mytimer", O_RDONLY);
	if(proc_fd < 0) {
		writeToFile(STDERR_FILENO, "Error: Cannot open /proc/mytimer\n");
		//write(STDERR_FILENO, error_msg, strlen(error_msg) + 1);
		exit(-1);
	}

    // Read /proc/mytimer into buf
	read(proc_fd, buf, size);
	close(proc_fd);
}

// Assumes fd is a valid file descriptor to write to.
// Assynes msg is a null-terminated string
void writeToFile(int fd, const char * const msg) {
	write(fd, msg, strlen(msg) + 1);
}

void listTimers(void) {
	D(printf("Listing timers\n"));
	char buf[1024];
	char * line;

    readProcFS(buf, sizeof(buf));

	// Go through each line in buf and search for timer information
	line = strtok(buf, "\n\t");
	do  {
        char msg[129]; // Timer message
        unsigned int seconds; // 

		D(printf("line: %s\n", line));

		if (sscanf(line, "[TIMER]: %128[^<] <%u s>", msg, &seconds) == 2) {

            // Write to user output
            char output[256];
            sprintf(output, "%s %u\n", msg, seconds);

            writeToFile(STDOUT_FILENO, output);
        }
	} while((line = strtok(NULL, "\n\t")));
}


	
int checkIfTimerExists(const char * const msg) { 
	D(printf("Checking if timer %s exists\n", msg));
	char buf[1024];
	char * line;

    readProcFS(buf, sizeof(buf));

	// Go through each line in /proc file and see if msg is in it 
	line = strtok(buf, "\n\t");
	do  {
		// Search for "[TIMER]: %s %u" substring in line

		D(printf("line: %s\n", line));

		char string[129];

		// scan line for timer message and see if it matches
		if (sscanf(line, "[TIMER]: %128[^<] <%*u s> ", string) == 1 && strcmp(string, msg) == 0) {
			return 1;
		}
	} while((line = strtok(NULL, "\n\t")));
	return 0;
}

void sighandler(int signo) {
	D(printf("process %u in signal handler\n", getpid()));
	// if timer is still active go back to sleep
	if(!checkIfTimerExists(timer_msg)) {
		sleeping = 0;
	}
}


int isNumber(char * str) {
	D(printf("Checking if string is a number\n"));

    unsigned int i = 0;
    for(i = 0; i < strlen(str); i++) {
        if(!isdigit(str[i])) {
            return 0;
        }
    }
    return 1;
}

void printManPage() {
	writeToFile(STDOUT_FILENO, "ERROR: INVALID USE\n");       
}
