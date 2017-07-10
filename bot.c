#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>

#include "jsmn/jsmn.h"

int conn;
char sbuf[512];

typedef struct config {
    char *host;
    char *port;
    char *nick;
    char *channel;
} config;

char *host = "localhost";
char *port = "6667";
char *nick = "test";
char *channel = "#test";
config conf;

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
    if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
            strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
        return 0;
    }
    return -1;
}

int saveToConfigIfMatch(char* name, char** configDest, char* configBuf, jsmntok_t* tokens, int token) {
    int length = tokens[token].end - tokens[token].start;
    if ((int)strlen(name) == length && strncmp(configBuf + tokens[token].start, name, length) == 0) {
        printf("- %s: %.*s\n", name, tokens[token+1].end-tokens[token+1].start, configBuf + tokens[token+1].start);
        *configDest = strndup(configBuf + tokens[token+1].start, tokens[token+1].end-tokens[token+1].start);
        return 1;
    }
    return 0;
}

int readConfig(){
    int r;
    int i;
    jsmn_parser p;
    jsmntok_t tokens[42]; /* We expect no more than 42 tokens */

    char *configBuf = 0;
    long length;
    FILE *f = fopen("config.json", "rb");
    if(!f){
        printf("failed to open 'config.json'\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    configBuf = malloc(length);
    if(!configBuf){
        fclose(f);
        printf("failed to malloc configBuf for config\n");
        return 1;
    }
    if(fread(configBuf, 1, length, f) <= 0) {
        fclose(f);
        printf("failed to read config\n");
        return 1;
    }
    fclose(f);

    //we have read config into configBuf

    jsmn_init(&p);
    r = jsmn_parse(&p, configBuf, strlen(configBuf), tokens, sizeof(tokens)/sizeof(tokens[0]));
    if (r < 0) {
        printf("Failed to parse JSON: %d\n", r);
        return 1;
    }
    printf("parsed\n");


    /* Assume the top-level element is an object */
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        printf("config expected to be object\n");
        return 1;
    }

    for (i = 1; i < r; i++) {
        if(tokens[i].type == JSMN_STRING || tokens[i].type == JSMN_PRIMITIVE) {
            if (saveToConfigIfMatch("host", &conf.host, configBuf, tokens, i)) {
                i++;
                continue;
            }
            if (saveToConfigIfMatch("port", &conf.port, configBuf, tokens, i)) {
                i++;
                continue;
            }
            if (saveToConfigIfMatch("nick", &conf.nick, configBuf, tokens, i)) {
                i++;
                continue;
            }
            if (saveToConfigIfMatch("channel", &conf.channel, configBuf, tokens, i)) {
                i++;
                continue;
            }
        } else {
            printf("not string or primitive; key: %.*s\n", tokens[i].end-tokens[i].start,
                    configBuf + tokens[i].start);
        }
    }
}
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

    readConfig();
    printf("*host: %s\n", conf.host);
    printf("*port: %s\n", conf.port);
    printf("*nick: %s\n", conf.nick);
    printf("*channel: %s\n", conf.channel);
    exit(0);

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
