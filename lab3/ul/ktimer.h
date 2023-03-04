// Name: Justin Sadler
// Date: 02-03-2023
// Description: Header file for helper functions for user-space program
#ifndef __KTIMER__H
#define __KTIMER__H

#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#define DEBUG 0

#if DEBUG
#	define	D(x) x
#else
#	define D(x) 
#endif

extern int sleeping;
extern char * timer_msg;

void printManPage(void); // Print error message
void sighandler(int); // Handle SIGNO signal from Linux kernel
void listTimers(void); // List the timers in the system
int checkIfTimerExists(const char * const msg); // Check if timer w/ messgae exists in the system
void writeToFile(int fd, const char * const msg); // Write a null-terminated string to a file descriptor
int isNumber(char * str); // Check if string is a decimal integer

#endif
