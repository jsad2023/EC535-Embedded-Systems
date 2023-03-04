/*
Name: Justin Sadler
Date: 26-02-2023
Description: Main source code file for user-space program
*/

#include "ktimer.h"

int sleeping = 1;
char * timer_msg;

int main(int argc, char **argv) {

	struct sigaction action;
	int oflags;
	int mytimer_fd;
	char user_msg[255]; // message to user
	char kernel_msg[255]; // message to kernel

	// Opens to device file
	mytimer_fd = open("/dev/mytimer", O_WRONLY);

	if (mytimer_fd < 0) {
		writeToFile(STDERR_FILENO, "mytimer module is not loaded\n");
		return 1;
	}

	// Listing timers
	if(argc == 2 && strcmp(argv[1], "-l") == 0) {
		// list timers 
		listTimers();

	// Changing max number of timers
	} else if(argc == 3 && strcmp(argv[1], "-m") == 0 && isNumber(argv[2])) {
		sprintf(kernel_msg, "%s %s", argv[1], argv[2]);
		writeToFile(mytimer_fd, kernel_msg);
	    //write(mytimer_fd, kernel_msg, strlen(kernel_msg) + 1);
	// Registering/updating a timer
	} else if(argc == 4 && strcmp(argv[1], "-s") == 0 && isNumber(argv[2])) {
		timer_msg = argv[3];
		
		// Check if timer already exists
		D(printf("Checking if timer already exists\n"));
		int updatingTimer = checkIfTimerExists(timer_msg);	
		sprintf(kernel_msg, "%s %s %s", argv[1], argv[2], timer_msg);
		D(printf("sending to kernel %s\n", kernel_msg));
		writeToFile(mytimer_fd, kernel_msg);
		//write(mytimer_fd, kernel_msg, strlen(kernel_msg) + 1);


		// Check if timer was created

		if(updatingTimer) {
			D(printf("Updated message\n"));
			sprintf(user_msg, "The timer %s was updated!\n", timer_msg);
			//write(STDOUT_FILENO, user_msg, strlen(user_msg) + 1);
			writeToFile(STDOUT_FILENO, user_msg);
			goto done;
		} 

		D(printf("Checking if timer was made\n"));
		int createdTimer = checkIfTimerExists(timer_msg);

		if(!createdTimer) {
			//write(STDOUT_FILENO, user_msg, strlen(user_msg) + 1);
			writeToFile(STDOUT_FILENO, "Cannot add another timer!\n");
			goto done;
		}

		D(printf("Creating timer\n"));

		// Setup signal handler
		memset(&action, 0, sizeof(action));
		action.sa_handler = sighandler;
		action.sa_flags = SA_SIGINFO;
		sigemptyset(&action.sa_mask);
		sigaction(SIGIO, &action, NULL);
		// Set the process ID or the group ID that will recieve SIGIO and SIGURG signals for events on mytimer_fd. Lets teh kernel know who to notify
		fcntl(mytimer_fd, F_SETOWN, getpid()); 
		// Get the file access mode and file status flags of mytimer_fd
		oflags = fcntl(mytimer_fd, F_GETFL);
		// Enable asynchronous notification for the file, calls fasync method in kernel module
		fcntl(mytimer_fd, F_SETFL, oflags | FASYNC); 


		sleeping = 1;
		D(printf("Sleeping\n"));
		
		while(sleeping) {
			pause();
		}

		writeToFile(STDOUT_FILENO, timer_msg);
		writeToFile(STDOUT_FILENO, "\n");


    } else if (argc == 2 && strcmp(argv[1], "-r") == 0) {
		D(printf("Resetting timers\n"));
		sprintf(kernel_msg, "-r");
		//write(mytimer_fd, kernel_msg, strlen(user_msg) + 1);
		writeToFile(mytimer_fd, kernel_msg);
	}else {
        // print man Pages   
        printManPage();
    }
	done:

    close(mytimer_fd);
    return 0;
}
