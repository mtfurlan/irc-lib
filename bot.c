#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>

int conn;
char sbuf[512];

char *nick = "test";
char *channel = "#test";
char *host = "localhost";
char *port = "6667";

void raw(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sbuf, 512, fmt, ap);
    va_end(ap);
    printf("<< %s", sbuf);
    write(conn, sbuf, strlen(sbuf));
}

void intHandler(int dummy) {
    raw("QUIT :coming back someday\r\n");
    exit(0);
}

void processMessage(char* from, char* where, char* target, char* message){
    if(!strncmp(message, nick, strlen(nick)) && message[strlen(nick)] == ':') {
        char* action = NULL;
        char* args = NULL;
        //Addressed to us (in format 'nick: some message'
        //strip out our name
        message += strlen(nick)+1;
        //There might be a space after :
        if(message[0] == ' ') {
            message++;
        }
        action = args = message;
        while(args[0] != ' ' && args[0] != '\0') {
            args++;
        }
        if(args[0] != '\0') {
            args[0] = '\0';
            args++;
        } else {
            args = NULL;
        }
        //action now is the first word, args is the action arguments or null
        //Now we have the message addressed at us.
        printf("action: [%s]; args: [%s]\n", action, args);

        raw("PRIVMSG %s :%s\r\n", target, args);
    }
}

int main() {

    char *user, *command, *where, *message, *sep, *target;
    int i, j, l, sl, o = -1, start, wordcount;
    char buf[513];
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(host, port, &hints, &res);
    conn = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    connect(conn, res->ai_addr, res->ai_addrlen);

    signal(SIGINT, intHandler);

    raw("USER %s 0 0 :%s\r\n", nick, nick);
    raw("NICK %s\r\n", nick);

    while ((sl = read(conn, sbuf, 512))) {
        for (i = 0; i < sl; i++) {
            //We read till one message is done, and process that buffer.
            o++;
            buf[o] = sbuf[i];
            if ((i > 0 && sbuf[i] == '\n' && sbuf[i - 1] == '\r') || o == 512) {
                buf[o + 1] = '\0';
                l = o;
                o = -1;
                //We hit message length, or \n\r, so we have a full message. Process it.

                printf(">> %s", buf);

                if (!strncmp(buf, "PING", 4)) {
                    //Is "PING and some message" we return "PONG and some message"
                    buf[1] = 'O';
                    raw(buf);
                } else if (buf[0] == ':') {
                    // there is a prefix, so we handle it.
                    wordcount = 0;
                    user = command = where = message = NULL;
                    //process message
                    for (j = 1; j < l; j++) {
                        if (buf[j] == ' ') {
                            buf[j] = '\0';
                            wordcount++;
                            switch(wordcount) {
                                case 1: user = buf + 1; break;
                                case 2: command = buf + start; break;
                                case 3: where = buf + start; break;
                            }
                            if (j == l - 1) continue;
                            start = j + 1;
                        } else if (buf[j] == ':' && wordcount == 3) {
                            if (j < l - 1) message = buf + j + 1;
                            break;
                        }
                    }//end process message

                    if (wordcount < 2) continue;

                    if (!strncmp(command, "001", 3) && channel != NULL) {
                        raw("JOIN %s\r\n", channel);
                    } else if (!strncmp(command, "PRIVMSG", 7) ) {
                        if (where == NULL || message == NULL) {
                            printf("where or message null");
                            continue;
                        }
                        if ((sep = strchr(user, '!')) != NULL) {
                            user[sep - user] = '\0';
                        }
                        //If it is one of the 4 channel types, where is the channel, else it's the user
                        if (where[0] == '#' || where[0] == '&' || where[0] == '+' || where[0] == '!') {
                            target = where;
                        }else{
                            target = user;
                        }
                        //process \n\r out of message
                        //printf("strlen: %d, -1: %d, -2: %d\n", strlen(message), message[strlen(message)-1], message[strlen(message)-2]);
                        if(strlen(message) > 2) {
                            message[strlen(message)-2] = '\0';
                        }
                        printf("[from: %s] [where: %s] [reply-to: %s] %s\n", user, where, target, message);
                        processMessage(user, where, target, message);
                    }
                }//else the message isn't PING, and has no prefix. Not sure messages like this exist in the wild
            }//end if we have entire thing in buffer
        }//end for
    }//end while we read (forever)
    return 0;
}
