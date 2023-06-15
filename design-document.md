Steps to run: 
1. download and unzip the docker image "baseline" in the "baseline.zip"
2. navigate to the the general project folder and run: docker run --rm -ti -v "$(pwd):/opt" baseline
3. run the following commands to make the program: "cd opt", "make", "exit"
4. then navigate to the "bin" directory via "cd bin" and initialize with a custom file name: "./init customFileName"
5. open 3 terminals within the baseline image, one for "./bank ./customFileName.bank", one for "./router", and one for "./atm ./customFileName.atm"
6. follow the documentation for each program to use features
7. when finished, enter atm directory, remove the bin folder -> rm -r bin


Security Notes
Your protocol should be secure against an adversary who is not in
possession of a user's ATM card, even if the adversary knows the
user's PIN, and vice versa. The attacker is also in control of a
router on the path between the ATM and the bank, and can inspect,
modify, drop, and duplicate packets--as well as create wholly new
packets--to both ATM and bank.


Implementation
On creation, the bank and atm programs connect accross the router. Users an create accounts at the bank, which provides them with an encrypted atm card containing their PIN number for verification at ATM login. Users can make deposits and check their balance at the bank by using the commands specified in the bank.md file. At the atm, users must begin a session by providing their account name, card, and matching PIN number. After an authenticated login, users can process balance requests and withdraws until they end their session. The bank and atm communicate these messages over the router to ensure validity of requests. There are security measures in place to address found vulnerabilities pertaining to our protocol, which is described below within each issue. 


Vulnerabilites Found and Security implemented
1. One of the primary constraints of this system was that it "should be secure against an adversary who is not in possession of a user's ATM card, even if the adversary knows the user's PIN, and vice versa." We could not trust users to keep their cards secure. Our system requires both a user to know their PIN and their card to be present at an atm, and the system reads the PIN from the card. This works well against attackers at the ATM that do not have the user's card, even if they do know the pin. Unfortunately, the PIN was unsecured on the user's card, failing the security note of "vice versa" for when an attacker has the card. To help prevent the attacker from easily learning a user' PIN, we encrypt the contents of the card on creation, and decrypt when reading into the atm machine, providing some friction against an easy weak point.

2. One flaw with the local security is that there was unlimited access to pin attempts. Even if the cards are secure, there are only (10 digits ^ 4 numbers) = 10000 possible combinations for a pin number. An easy fix would be to limit the failed requests for sign on. One issue with this blanket fix is that an atm would lock after a total number of failed sign ons throughout the day, not just for a user. For example, the atm locks at 5 failed attempts. Person A walks up and takes three attempts to sign in. Person B walks up, and if they also three attempts, they will be locked out (failure counter => 3+3 => 6). Clearly, we would want this policy to be fair to all users, so to implement this, I had the atm create a table for all valid users (added when they provide their card, if they are not yet in the system), initialized to zero attempts. Once a username is known (via the card), the counter for that user goes up with each failed attempt, rather than being shared system wide. After five failed attempts, the system closed and prints a security alert. If the user does sign in before the counter reaches 5, the counter is reset back to zero for the next sign in attempt. Else, the account remains locked until "bank security" looks into fixing the situation. Even if the attacker manages to guess the right combination after 5+ failed attempts, the account remains locked (but they could try other accounts, but that is intentional, as we do not want to obstruct other users).

3. a. Another big flaw with the initial functionality implementation was that when the bank and atm communicate over the network, they send string requests. An example of such request from the ATM to the Bank is: "withdraw userID amount". In order to protect the privacy of transactions, I decided to encrypt and decrypt messages over the network. Messages are encrypted before being sent to the router, and decrypted upon delivery.

3. b. The other problem encrypting messages addresses is that if attackers learn how the ATM and Bank communicate, they can then forward their own messages to the system, or alter messages during transit. Encryption helps protect our communication syntax, and decryption of messages means that even if an attacker falsifies a valid plaintext message, they must use the same encryption method to forward a valid message.

4. Another network security issue related to the previous is that attackers in possession of the router might try to pretend to be a Bank or ATM receiving requests to forge replies to the ATM. To mitigate this problem, every Bank and ATM creates a random authentication value on initialization. This value is then shared locally by file (in a real world setting, it would be by whoever sets up the physical ATM) is created. When messages are sent across the network, the message is appended to the authentication value. If a Bank or ATM receives a message with an incorrect or missing security value, it alerts the Bank and ATM of a integrity failure and shuts down, preventing more tampering and allowing inspection of the machines by the owners. Essentially, the goal of this defense is to ensure messages are actually are coming from and created by a Bank or ATM and not someone pretending to be one.

5. While similar in effect to the previous attack, a more clever approach to message forgery would be for attackers in possession of the router to send save requests within a session to send to a different ATM and Bank pair in a different session rather then try to forge original messages. For example, say Fred creates an account with Bank and ATM A and withdraws some money. Later, Fred makes an account with Bank and ATM B, but does not withdraw any money. Then, the attacker uses the saved withdraw transaction to trick Bank B into allowing a withdraw request at ATM B. Our solution of paired authentication keys requires an attacker to know the true ID for a bank or atm in order to send a message.

6. The other component to message security is that attackers in control of the router can duplicate messages within a session. This attack gets around our security authentication because the messages really are valid requests coming from a valid host. In order to combat this security risk, our Bank and ATM, on creation, track how many messages they receive and send. When communicating between each other, they include what message number the current message is after the security value. When the Bank or ATM receives that message, they compare it to the expected result. If the message has the wrong number, the Bank or ATM alerts a security breach and shuts down in the same manner that an authentication failure dictates. This risk was important because even if an attacker could not forge a request, they could still duplicate valid transactions within a session, such as tricking the ATM to deposit more than once.

Extra notes (Not specific to the above vulnerabilities but relevant to the implementation)
1. The system was initially weak to both bank and atm inputs where the deposit or withdraw request were ridiculously high number values. While the regular expression constraints for the project prevented negative numbers and the INT_MAX specification was supposed to prevented values higher than said value, when the numbers could not be compared it was able to bypass our system. To mitigate the issue, I implemented a simple character length check to ensure the input did not exceed INT_MAX's length before comparing the actual value. So far, this seems to have covered the cases we have tested.

2. The current implementation has been left unencrypted intentionally to easily example router communication, but the baseline image supports openSSL's encryption library (found within lcrypto).

Thank you.
