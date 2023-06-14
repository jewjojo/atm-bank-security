/* 
 * The main program for the ATM.
 *
 * You are free to change this as necessary. 
 */

#include "atm.h"
#include <stdio.h>
#include <stdlib.h>

static const char prompt[] = "ATM: ";

// copied same pattern from bank
int main(int argc, char **argv)
{
    char user_input[1000];

    // If <init-fname>.atm cannot be opened, print Error opening atm initialization file and return value 64.
    if (!fopen(argv[1], "r"))
    {
        printf("Error opening ATM initialization file \n");
        exit(64);
    }

    ATM *atm = atm_create();

    printf("%s", prompt);
    fflush(stdout);

    while (fgets(user_input, 10000,stdin) != NULL)
    {
        atm_process_command(atm, user_input);
        printf("%s", prompt);
        fflush(stdout);
    }
	return EXIT_SUCCESS;
}
