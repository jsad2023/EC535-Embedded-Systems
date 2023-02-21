#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

int isNumber(char * str);

void printManPage(void);
void readFromMyTimer(FILE * fptr);

int main(int argc, char **argv) {
	char mytimerInput[256];
	
	FILE * pFile;
	
	pFile = fopen("/dev/mytimer", "r+");
	if (pFile==NULL) {
		fputs("mytimer module isn't loaded\n", stderr);
		return -1;
	}
	
	if(argc == 2 && strcmp(argv[1], "-l") == 0) {
	    fputs("-l\n", pFile); // send "-l" to kernel module
		readFromMyTimer(pFile);

	} else if(argc == 3 && strcmp(argv[1], "-m") == 0 && isNumber(argv[2])) {
	    sprintf(mytimerInput, "%s %s", argv[1], argv[2]);
	    fputs(mytimerInput, pFile);
		readFromMyTimer(pFile);
	} else if(argc == 4 && strcmp(argv[1], "-s") == 0 && isNumber(argv[2])) {
	    sprintf(mytimerInput, "%s %s %s", argv[1], argv[2], argv[3]);
	    fputs(mytimerInput, pFile);
		readFromMyTimer(pFile);
    } else {
        // print man Pages   
        printManPage();
    }
    
    fclose(pFile);
    return 0;
}

int isNumber(char * str) {
    unsigned int i = 0;
    for(i = 0; i < strlen(str); i++) {
        if(!isdigit(str[i])) {
            return 0;
        }
    }
    
    return 1;
    
}
void printManPage() {
	printf("Error: invalid use.\n");
}

void readFromMyTimer(FILE * fptr) {
	char mytimerOutput[1024];

	// reset file pointer to beginning of file
	rewind(fptr);

	fread(mytimerOutput, 1024, 1, fptr);
	printf("%s", mytimerOutput);
}
