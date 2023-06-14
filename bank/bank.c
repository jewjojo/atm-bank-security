#include "bank.h"
#include "ports.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// TODO includes

// uses hashtables for bank storage
#include "../util/hash_table.h"
// uses regex for checking valid input
#include <regex.h>
// need INT_MAX for balance input checking
#include <limits.h>
// encryption
#include <openssl/evp.h>
#include <openssl/aes.h>

Bank *bank_create()
{
    Bank *bank = (Bank *)malloc(sizeof(Bank));
    if (bank == NULL)
    {
        perror("Could not allocate Bank");
        exit(1);
    }

    // Set up the network state
    bank->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&bank->rtr_addr, sizeof(bank->rtr_addr));
    bank->rtr_addr.sin_family = AF_INET;
    bank->rtr_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bank->rtr_addr.sin_port = htons(ROUTER_PORT);

    bzero(&bank->bank_addr, sizeof(bank->bank_addr));
    bank->bank_addr.sin_family = AF_INET;
    bank->bank_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bank->bank_addr.sin_port = htons(BANK_PORT);
    bind(bank->sockfd, (struct sockaddr *)&bank->bank_addr, sizeof(bank->bank_addr));

    // Set up the protocol state
    // TODO set up more, as needed
    // encryption
    

    // set up hashtable to store user information
    bank->users = hash_table_create(10);
    // count network messages
    bank->messageSent = 0;
    bank->messageReceived = 0;

    // generate authentication code
    bank->auth = rand();

    // save code for the ATM to access locally
    FILE *cardFile = fopen("bank.auth", "w+");
    if (!cardFile)
    {
        printf("Error Generating Authentication File for Bank");
    }
    else
    {
        // write auth code to file and close file
        fprintf(cardFile, "%d", bank->auth);
        fclose(cardFile);
    }

    return bank;
}

void bank_free(Bank *bank)
{
    if (bank != NULL)
    {
        close(bank->sockfd);
        free(bank);
    }
}

ssize_t bank_send(Bank *bank, char *data, size_t data_len)
{
    // Returns the number of bytes sent; negative on error
    return sendto(bank->sockfd, data, data_len, 0,
                  (struct sockaddr *)&bank->rtr_addr, sizeof(bank->rtr_addr));
}

ssize_t bank_recv(Bank *bank, char *data, size_t max_data_len)
{
    // Returns the number of bytes received; negative on error
    return recvfrom(bank->sockfd, data, max_data_len, 0, NULL, NULL);
}

/* HELPERS STATR HERE */

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
    return 0;
}

// Local command create-user
void createNewUser(Bank *bank, char *args, int commandCount)
{
    char *username = strsep(&args, " ");
    char *pin = strsep(&args, " ");
    char *balance = strsep(&args, " \n");

    // check if commands are invalid
    if (checkRegEx(username, "^[a-zA-Z]+$") == 0 ||
        strlen(username) > 250 ||
        checkRegEx(pin, "^[0-9][0-9][0-9][0-9]$") == 0 ||
        checkRegEx(balance, "^[0-9]+$") == 0 ||
        strtoll(balance, NULL, 0) > INT_MAX ||
        commandCount != 4)
    {
        printf("Usage:  create-user <user-name> <pin> <balance>\n\n");
        return;
    }

    // check if user is in the system already
    if (hash_table_find(bank->users, username) != NULL)
    {
        printf("Error:  user %s already exists\n\n", username);
        return;
    }
    // add user to the system
    // see if card for user can be made, size = 250 name + 5 .card + 1 null
    char cardPath[256];

    strcpy(cardPath, username); // <username>
    strcat(cardPath, ".card");  // <username>.card

    FILE *cardFile = fopen(cardPath, "w+");
    // if new (or updated) card file cannot be written for some reason
    if (!cardFile)
    {
        printf("Error creating card file for user %s\n\n", username);
        return;
    }

    // write PIN to file and close file
    fputs(pin, cardFile);
    fclose(cardFile);

    // the user to the bank where bank.user = balance
    hash_table_add(bank->users, username, balance);
    printf("Created user %s\n\n", username);

    return;
}

void deposit(Bank *bank, char *args, int commandCount)
{
    char *username = strsep(&args, " ");
    char *amt = strsep(&args, " \n");

    // check valid commands
    if (checkRegEx(username, "^[a-zA-Z]+$") == 0 ||
        strlen(username) > 250 ||
        checkRegEx(amt, "^[0-9]+$") == 0 ||
        commandCount != 3)
    {
        printf("Usage:  deposit <user-name> <amt>\n\n");
        return;
    }

    // check user in the system
    if (hash_table_find(bank->users, username) == NULL)
    {
        printf("No such user\n\n");
        return;
    }

    // check amount is valid
    int currentBal = atoi(hash_table_find(bank->users, username));
    int toAdd = atoi(amt);

    if (strlen(amt) > 10 || currentBal > INT_MAX - toAdd)
    {
        printf("Too rich for this program\n\n");
        return;
    }

    // calculate new balance and store in memory (was losing it after add for some reason)
    int newBal = atoi(hash_table_find(bank->users, username)) + atoi(amt);
    char *buffer = malloc(12);
    snprintf(buffer, 12, "%d", newBal);

    // make deposit
    // maybe memory leak since not free() old buffer? NO! hash_table_del -> del_list -> free
    hash_table_del(bank->users, username);
    hash_table_add(bank->users, username, buffer);
    printf("$%s added to %s's account\n\n", amt, username);

    return;
}

char *balance(Bank *bank, char *args, int commandCount)
{
    char *username = strsep(&args, " \n");

    // check valid commands
    if (checkRegEx(username, "^[a-zA-Z]+$") == 0 ||
        strlen(username) > 250 ||
        commandCount != 2)
    {
        char *toReturn = malloc(sizeof(char) * 256);
        // write to ptr
        snprintf(toReturn, 100, "Usage:  balance <user-name>\n\n");
        return (toReturn);
    }

    // check user in the system
    if (hash_table_find(bank->users, username) == NULL)
    {
        return ("No such user\n\n");
    }

    // print user's current balance
    int currentBal = atoi(hash_table_find(bank->users, username));

    // int_max = 10 digits, $ = 1 digit, end of string char = 1 digit
    char *toReturn = malloc(sizeof(char) * 12);

    // write to ptr
    snprintf(toReturn, 13, "$%d", currentBal);

    return toReturn;
}

char *withdraw(Bank *bank, char *args, int commandCount)
{
    char *username = strsep(&args, " \n");
    char *amount = strsep(&args, " \n");

    // check valid commands
    if (checkRegEx(username, "^[a-zA-Z]+$") == 0 ||
        strlen(username) > 250 ||
        checkRegEx(amount, "^[0-9]+$") == 0 ||
        atoi(amount) > INT_MAX ||
        commandCount != 3)
    {
        char *toReturn = malloc(sizeof(char) * 256);
        // write to ptr
        snprintf(toReturn, 100, "Usage:  withdraw <amt>\n\n");
        return (toReturn);
    }

    // check withdraw amount is valid
    int currentBal = atoi(hash_table_find(bank->users, username));
    int toTake = atoi(amount);

    if (strlen(amount) > 10 || currentBal - toTake < 0)
    {
        return ("Insufficient funds\n\n");
    }

    // remove amount from account
    // calculate new balance and store in memory (was losing it after add for some reason)
    int newBal = atoi(hash_table_find(bank->users, username)) - atoi(amount);
    char *buffer = malloc(12);
    snprintf(buffer, 12, "%d", newBal);

    // maybe memory leak since not free() old buffer? NO! hash_table_del -> del_list -> free
    hash_table_del(bank->users, username);
    hash_table_add(bank->users, username, buffer);

    // 15 + 10 ($ 12DigitAmountMax dispensed<endChar>)
    char *returnBuffer = malloc(sizeof(char) * 25);
    snprintf(returnBuffer, sizeof(char) * 25, "$%s dispensed", amount);

    return returnBuffer;
}

/* HELPERS END HERE */

void bank_process_local_command(Bank *bank, char *command, size_t len)
{
    // TODO: Implement the bank's local commands

    // read in the arguments from the command line
    char *args = strdup(command);

    // save the first one to identify action, send rest to helper function
    // strsep = string splice
    char *firstCommand = strsep(&args, " \n");

    // count the number of commands to see if input is even possible
    int commandCount = countCommands(command);

    // check what the command does and try to do action
    if (strcmp(firstCommand, "create-user") == 0)
    {
        createNewUser(bank, args, commandCount);
    }
    else if (strcmp(firstCommand, "balance") == 0)
    {
        // balance must return char ptr, not void
        // using method for remote commands
        char *temp = balance(bank, args, commandCount);
        printf("%s\n\n", temp);
    }
    else if (strcmp(firstCommand, "deposit") == 0)
    {
        deposit(bank, args, commandCount);
    }
    else
    {
        printf("Invalid command\n\n");
    }
    return;
}

// code redone from https://wiki.openssl.org/index.php/EVP_Symmetric_Encryption_and_Decryption
// instead of handling errors, we return 0
int encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *iv, unsigned char *ciphertext)
{
    EVP_CIPHER_CTX *ctx;

    int len;

    int ciphertext_len;

    if (!(ctx = EVP_CIPHER_CTX_new()))
    {
        return 0;
    }

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv))
    {
        return 0;
    }

    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len))
    {
        return 0;
    }

    ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len))
    {
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

void bank_process_remote_command(Bank *bank, char *command, size_t len)
{
    // TODO: Implement the bank side of the ATM-bank protocol
    // save the ATM auth code
    // collect the authentication variable from the bank
    // check if file exists
    if (access("atm.auth", F_OK) != 0)
    {
        // auth does not exist
        printf("Authentication From ATM Not Found!\n\n");
    }
    else
    {
        // code exists, read contents in from file
        FILE *atmAuthFile = fopen("atm.auth", "r");
        // try to open file
        if (atmAuthFile)
        {
            char atmCode[13];
            // save contents of code file into buffer
            fgets(atmCode, 11, atmAuthFile); // read in line
            // close the file
            fclose(atmAuthFile);
            // save the code to the atm
            bank->authATM = atoi(atmCode);
        }
        else
        {
            // cant open file
            printf("Unable to Read Authentication From ATM!\n\n");
        }
    }

    // read in the arguments from the message, just like local commands
    char *args = strdup(command);
    char *messageNumber = strsep(&args, " \n");
    char *authNumber = strsep(&args, " \n");
    int commandCount = countCommands(args);
    char *firstCommand = strsep(&args, " \n");

    // count the messaged received
    bank->messageReceived += 1;

    // ensure messages were not duped or deleted
    if (bank->messageReceived != atoi(messageNumber))
    {
        printf("BANK SECURITY ERROR: MESSAGE DELETION OR DUPLICATION DETECTED!\n");
        fflush(stdout);
        exit(0);
    }

    // ensure messages come from a valid atm
    if (bank->authATM != atoi(authNumber))
    {
        printf("BANK SECURITY ERROR: INVALID MESSAGE SOURCE DETECTED!\n");
        fflush(stdout);
        exit(0);
    }

    // if it is a balance or withdraw, process it
    if (strcmp(firstCommand, "balance") == 0)
    {
        char *ret = balance(bank, args, commandCount);
        char response1[400];
        bzero(response1, sizeof(response1));

        // count the message being sent
        bank->messageSent += 1;

        // count how many digits to make room for in message
        int temp = bank->messageSent;
        int count = 0;
        while (temp != 0)
        {
            temp = temp / 10;
            count++;
        }
        int temp2 = bank->auth;
        int count2 = 0;
        while (temp2 != 0)
        {
            temp2 = temp2 / 10;
            count2++;
        }
        // add the counter to the front of the message
        sprintf(response1, "%d", bank->messageSent);
        sprintf(response1 + count, " %d", bank->auth);
        sprintf(response1 + count + 1 + count2, " %s\n\n", ret);
        fflush(stdout);

        bank_send(bank, response1, strlen(response1));
    }
    else if (strcmp(firstCommand, "withdraw") == 0)
    {
        char *ret = withdraw(bank, args, commandCount);
        char response1[400];
        bzero(response1, sizeof(response1));

        // count the message being sent
        bank->messageSent += 1;

        // count how many digits to make room for in message
        int temp = bank->messageSent;
        int count = 0;
        while (temp != 0)
        {
            temp = temp / 10;
            count++;
        }
        int temp2 = bank->auth;
        int count2 = 0;
        while (temp2 != 0)
        {
            temp2 = temp2 / 10;
            count2++;
        }
        // add the counter to the front of the message
        sprintf(response1, "%d", bank->messageSent);
        sprintf(response1 + count, " %d", bank->auth);
        sprintf(response1 + count + 1 + count2, " %s\n\n", ret);
        fflush(stdout);

        bank_send(bank, response1, strlen(response1));
    }
    return;
}