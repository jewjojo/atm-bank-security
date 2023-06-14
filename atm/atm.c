#include "atm.h"
#include "ports.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// Our includes
#include <regex.h>
#include <unistd.h>
#include "../util/hash_table.h"

// encryption
#include <openssl/evp.h>
#include <openssl/aes.h>

ATM *atm_create()
{
    ATM *atm = (ATM *)malloc(sizeof(ATM));
    if (atm == NULL)
    {
        perror("Could not allocate ATM");
        exit(1);
    }

    // Set up the network state
    atm->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&atm->rtr_addr, sizeof(atm->rtr_addr));
    atm->rtr_addr.sin_family = AF_INET;
    atm->rtr_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    atm->rtr_addr.sin_port = htons(ROUTER_PORT);

    bzero(&atm->atm_addr, sizeof(atm->atm_addr));
    atm->atm_addr.sin_family = AF_INET;
    atm->atm_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    atm->atm_addr.sin_port = htons(ATM_PORT);
    bind(atm->sockfd, (struct sockaddr *)&atm->atm_addr, sizeof(atm->atm_addr));

    // Set up the protocol state
    // TODO set up more, as needed 
    // count network messages
    atm->messageSent = 0;
    atm->messageReceived = 0;

    // secure pin number attempts
    atm->pinTable = hash_table_create(10);

    // generate authentication code
    atm->auth = rand();

    // save code for the Bank to access locally
    FILE *cardFile = fopen("atm.auth", "w+");
    if (!cardFile)
    {
        printf("Error Generating Authentication File for ATM");
    }
    else
    {
        // write auth code to file and close file
        fprintf(cardFile, "%d", atm->auth);
        fclose(cardFile);
    }

    // collect the authentication variable from the bank
    // check if file exists
    if (access("bank.auth", F_OK) != 0)
    {
        // auth does not exist
        printf("Authentication From Bank Not Found!\n\n");
    }
    else
    {
        // code exists, read contents in from file
        FILE *bankAuthFile = fopen("bank.auth", "r");
        // try to open file
        if (bankAuthFile)
        {
            char bankCode[13];
            // save contents of code file into buffer
            fgets(bankCode, 11, bankAuthFile); // read in line
            // close the file
            fclose(bankAuthFile);
            // save the code to the atm
            atm->authBank = atoi(bankCode);
        }
        else
        {
            // cant open file
            printf("Unable to Read Authentication From Bank!\n\n");
        }
    }

    return atm;
}

void atm_free(ATM *atm)
{
    if (atm != NULL)
    {
        close(atm->sockfd);
        free(atm);
    }
}

ssize_t atm_send(ATM *atm, char *data, size_t data_len)
{
    // Returns the number of bytes sent; negative on error
    return sendto(atm->sockfd, data, data_len, 0,
                  (struct sockaddr *)&atm->rtr_addr, sizeof(atm->rtr_addr));
}

ssize_t atm_recv(ATM *atm, char *data, size_t max_data_len)
{
    // Returns the number of bytes received; negative on error
    return recvfrom(atm->sockfd, data, max_data_len, 0, NULL, NULL);
}

/* HELPERS START HERE */
// copied over helpers from bank.c
// replaced readCommands with strdup, was getting issues

// counts the number of user inputs
int countCommands(char *command)
{
    int count = 0, i;

    for (i = 0; command[i] != '\0'; i++)
    {
        // ignore double spaces
        if (command[i] == ' ' && command[i + 1] != ' ')
            count++;
    }
    // account for only one input
    return count + 1;
}

int checkRegEx(char *stringToCheck, char *regexValue)
{
    regex_t expression;

    regcomp(&expression, regexValue, REG_EXTENDED);

    if (regexec(&expression, stringToCheck, 0, NULL, 0) == 0)
    {
        // printf("Valid regex");
        // input matches regex
        return 1;
    }
    // printf("Invalid regex");
    //  input does not match regex
    puts(stringToCheck);
    return 0;
}

int encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *iv, unsigned char *ciphertext){
    EVP_CIPHER_CTX *ctx;

    int len;

    int ciphertext_len;

    if (!(ctx = EVP_CIPHER_CTX_new())) {
        return 0;
    }

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) {
        return 0;
    }

    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len)) {
        return 0;
    }

    ciphertext_len = len;

    if(1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) {
        return 0;
    }

    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;
}
// decrypt also from https://wiki.openssl.org/index.php/EVP_Symmetric_Encryption_and_Decryption
int decrypt(unsigned char *ciphertext, int ciphertext_len, unsigned char *key, unsigned char *iv, unsigned char *plaintext)
{
    EVP_CIPHER_CTX *ctx;

    int len;
    int plaintext_len;

    if (!(ctx = EVP_CIPHER_CTX_new()))
        return 0;

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv))
        return 0;

    if (1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len))
        return 0;
    plaintext_len = len;

    if (1 != EVP_DecryptFinal_ex(ctx, plaintext + len, &len))
        return 0;
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return plaintext_len;
}

// send balance request to bank
void balance(ATM *atm, char *user)
{
    char recvline[10000];
    int n;

    // count message being sent
    atm->messageSent += 1;

    // initialize to empty, was getting PIN at the front for some reason
    // 2 + 11 + 7 + 1 + 250 + 1 (count + _ + authCode + _ + command + _ + name + endChar)
    char command[272];
    bzero(command, sizeof(command));

    int temp = atm->messageSent;
    int count = 0;
    while (temp != 0)
    {
        temp = temp / 10;
        count++;
    }

    sprintf(command, "%d", atm->messageSent);
    sprintf(command + count, " %d", atm->auth);
    strcat(command, " balance "); // balance
    strcat(command, user);        // balance <username>

    // send the command to bank
    atm_send(atm, command, sizeof(command));
    bzero(recvline, sizeof(recvline));

    // received message
    n = atm_recv(atm, recvline, 10000);
    recvline[n] = 0;
    atm->messageReceived += 1;

    // check received message
    char *received = strdup(recvline);
    char *messageNumber = strsep(&received, " \n");
    char *authNumber = strsep(&received, " \n");

    // ensure messages were not duped or deleted
    if (atm->messageReceived != atoi(messageNumber))
    {
        printf("ATM SECURITY ERROR: MESSAGE DELETION OR DUPLICATION DETECTED!\n");
        fflush(stdout);
        exit(0);
    }

    // ensure messages come from a valid atm
    if (atm->authBank != atoi(authNumber))
    {
        printf("ATM SECURITY ERROR: INVALID MESSAGE SOURCE DETECTED!\n");
        fflush(stdout);
        exit(0);
    }

    fputs(received, stdout);

    return;
}

// send withdraw request to bank
void withdraw(ATM *atm, char *user, char *amount)
{
    char recvline[10000];
    int n;

    // count message being sent
    atm->messageSent += 1;

    // initialize to empty, was getting PIN at the front for some reason
    // 8 + 1 + 250 + 1 + 12 + 1 (count + _ + authCode + _ + command + _ + name + _ + 12 digit amt + endChar)
    char command[275];
    bzero(command, sizeof(command));

    int temp = atm->messageSent;
    int count = 0;
    while (temp != 0)
    {
        temp = temp / 10;
        count++;
    }

    sprintf(command, "%d", atm->messageSent);
    sprintf(command + count, " %d", atm->auth);
    strcat(command, " withdraw "); // withdraw
    strcat(command, user);         // withdraw <username>
    strcat(command, " ");          // withdraw <username>
    strcat(command, amount);       // withdraw <username> <amount>

    // send the command to bank
    atm_send(atm, command, sizeof(command));
    bzero(recvline, sizeof(recvline));

    // received message
    n = atm_recv(atm, recvline, 10000);
    recvline[n] = 0;
    atm->messageReceived += 1;

    // check received message
    char *received = strdup(recvline);
    char *messageNumber = strsep(&received, " \n");
    char *authNumber = strsep(&received, " \n");

    // ensure messages were not duped or deleted
    if (atm->messageReceived != atoi(messageNumber))
    {
        printf("ATM SECURITY ERROR: MESSAGE DELETION OR DUPLICATION DETECTED!\n");
        fflush(stdout);
        exit(0);
    }

    // ensure messages come from a valid atm
    if (atm->authBank != atoi(authNumber))
    {
        printf("ATM SECURITY ERROR: INVALID MESSAGE SOURCE DETECTED!\n");
        fflush(stdout);
        exit(0);
    }

    fputs(received, stdout);

    return;
}

// deny new session while session running
void beginSession2()
{
    printf("A user is already logged in\n\n");
    return;
}

// display logout
void endSession()
{
    printf("User logged out\n\n");
    return;
}

void beginSession(ATM *atm, char *args, int commandCount)
{
    char *username = strsep(&args, " \n");

    // check if commands are invalid
    if (checkRegEx(username, "^[a-zA-Z]+$") == 0 ||
        strlen(username) > 250 ||
        commandCount != 2)
    {
        printf("Usage: begin-session <user-name>\n\n");
        return;
    }

    // try to find user's input card info
    char cardPath[256];
    char cardPin[256];

    strcpy(cardPath, username); // <username>
    strcat(cardPath, ".card");  // <username>.card

    // check if file exists
    if (access(cardPath, F_OK) != 0)
    {
        // card does not exist
        printf("No such user\n\n");
        return;
    }
    else
    {
        // card exists, read contents in from file
        FILE *cardFile = fopen(cardPath, "r");
        // try to open file
        if (cardFile)
        {
            // save contents of card file into buffer
            fgets(cardPin, 256, cardFile); // read in line
            // close the file
            fclose(cardFile);
        }
        else
        {
            // cant open file
            printf("Unable to access %s's card\n\n", username);
            return;
        }
    }

    // prompt for PIN

    // check if pin is valid
    // if pin is wrong, not authorize and return to ATM prompt
    // length, save one extra char to see if length is greater than 4. 4 pin + 1 end char
    char inputPin[1000];
    printf("PIN? ");
    fflush(stdout);
    fgets(inputPin, 1000, stdin);

    // remove trailing newlines
    inputPin[strcspn(inputPin, " \n")] = 0;

    // check if user is in the pinTable, if not, initialize their pin counter to 0. else, track their attempt count.
    int pinCount = 0;
    if (hash_table_find(atm->pinTable, username) == NULL) {
        hash_table_add(atm->pinTable, username, 0);
    } else {
        pinCount = hash_table_find(atm->pinTable, username);
    }

    // 5 tries per username, then alert security
    if (pinCount >= 5) {
        printf("Too many previous failed sign in attempts on user: %s. Account is locked.\n\n", username);
        return;
    }

    // regex check the PIN input
    if (inputPin == NULL || checkRegEx(inputPin, "^[0-9][0-9][0-9][0-9]$") == 0 || strcmp(cardPin, inputPin) != 0)
    {
        // if invalid input, increase their error count by 1 for that user
        pinCount += 1;
        hash_table_del(atm->pinTable, username);
        hash_table_add(atm->pinTable, username, pinCount);
        printf("Not authorized\n\n");
        return;
    }

    // user allowed in
    // copy atm-main.c await input code
    char user_input[1000];
    printf("Authorized\n\nATM (%s):  ", username);

    // reset security PIN back to 0 after sucessful sign in
    hash_table_del(atm->pinTable, username);
    hash_table_add(atm->pinTable, username, 0);

    while (fgets(user_input, 10000, stdin) != NULL)
    {
        char *newArgs = strdup(user_input);
        char *newCommand = strsep(&newArgs, " \n");
        int newCommandCount = countCommands(user_input);

        // loop user input commands until session is over
        if (strcmp(newCommand, "begin-session") == 0)
        {
            beginSession2();
        }
        else if (strcmp(newCommand, "balance") == 0)
        {
            if (newCommandCount != 1)
            {
                printf("Usage: balance\n\n");
                printf("ATM (%s):  ", username);
                fflush(stdout);
                continue;
            }
            // forward request to bank
            fflush(stdout);
            balance(atm, username);
        }
        else if (strcmp(newCommand, "withdraw") == 0)
        {
            char *amount = strsep(&newArgs, " \n");
            if (newCommandCount != 2)
            {
                printf("Usage: withdraw <amt>\n\n");
                printf("ATM (%s):  ", username);
                fflush(stdout);
                continue;
            }
            // forward request to bank
            withdraw(atm, username, amount);
        }
        else if (strcmp(newCommand, "end-session") == 0)
        {
            // exit this function
            endSession();
            return;
        }
        else
        {
            printf("Invalid command\n\n");
        }
        printf("ATM (%s):  ", username);
        fflush(stdout);
    }

    return;
}
/* HELPERS END HERE */

void atm_process_command(ATM *atm, char *command)
{
    // TODO: Implement the ATM's side of the ATM-bank protocol

    /*
     * The following is a toy example that simply sends the
     * user's command to the bank, receives a message from the
     * bank, and then prints it to stdout.
     */

    /*
    char recvline[10000];
    int n;

    atm_send(atm, command, strlen(command));
    n = atm_recv(atm,recvline,10000);
    recvline[n]=0;
    fputs(recvline,stdout);
    */

    // Our code starts here
    // read in the arguments from the command line
    char *args = strdup(command);

    // save the first one to identify action, send rest to helper function
    // strsep = string splice
    char *firstCommand = strsep(&args, " \n");

    // count the number of commands to see if input is even possible
    int commandCount = countCommands(command);

    // check what the command does and try to do action
    // if no session in progress
    if (strcmp(firstCommand, "begin-session") == 0)
    {
        beginSession(atm, args, commandCount);
        return;
    }
    // can only do these commands while a user is logged in
    if (strcmp(firstCommand, "withdraw") == 0)
    {
        printf("No user logged in\n\n");
        return;
    }
    if (strcmp(firstCommand, "balance") == 0)
    {
        printf("No user logged in\n\n");
        return;
    }
    if (strcmp(firstCommand, "end-session") == 0)
    {
        printf("No user logged in\n\n");
        return;
    }
    else
    {
        printf("Invalid command\n\n");
    }
    return;
}
