/*****  TCP SEQUENTIAL SERVER   *****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "../errlib.h"
#include "../sockwrap.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>

/* CONSTANTS */

#define BUFLEN 128                                                          /* Receiver Buffer length */
#define MAXBUFLEN 1000                                                      /* Transmitter Buffer Length */
#define TIMEOUT 15                                                          /* timeout is 15 seconds */
#define TRANSFER_APPROVAL 0                                                 /* If you want to activate the approval mechanism for every single transfer, set this constant 1 */

/* FUNCTION PROTOTYPES */

void err_sys (const char *fmt, ...);
int fstat(int fildes, struct stat *buf);

/* GLOBAL VARIABLES */

struct  timeval tval;
struct  stat fileStat;
char    *prog_name;
char    ackMsg[5] = "+OK\r\n";
int     socketAbnormalTermination;


void setPromptColor(char *colorName)
{
    if(colorName == "default")
        printf("\033[0m");
    else if(colorName == "red")
        printf("\033[1;31m");
    else if(colorName == "green")
        printf("\033[1;32m");
    if(colorName == "yellow")
        printf("\033[1;33m");
    else if(colorName == "blue")
        printf("\033[1;34m");
    else if(colorName == "magenta")
        printf("\033[1;35m");
    else if(colorName == "cyan")
        printf("\033[1;36m");
}

void sigPipeHandler(int signal)
{
    if(signal == SIGPIPE)
    {
        setPromptColor("red");
        printf("\rTransfer Failure! Socket has been closed!\n");
        setPromptColor("default");
        socketAbnormalTermination = 1;
    }
}

void timeoutHandler(int signal)                                              /* Timeout for in case of Approval mechanism. It is deactivated by default.*/
{
    if(signal == SIGALRM)
    {
        setPromptColor("red");
        printf("Timeout has expired!\n");
        exit(EXIT_FAILURE);
    }
}

void sendErrorMessage(int socket)
{
    char msgError[6] = "-ERR\r\n";

    size_t msgLen = strlen(msgError);

    if( writen(socket, msgError, msgLen) == msgLen )
    {
        setPromptColor("green");
        printf("Error message has been successfully sent!\n");
        setPromptColor("default");
    }
    else
    {
        setPromptColor("red");
        printf("Error message failure!\n");
        setPromptColor("default");
    }
}

char *getRequestedFileName(char *msg)
{
    char *tmp = NULL;
    tmp = memchr(msg, ' ', strlen(msg));
    tmp += 1;
    return tmp;
}

int getFileStats(char *fileName)
{
    int res = open(fileName, O_RDONLY);

    if(fstat(res, &fileStat) < 0)
    {
        setPromptColor("red");
        perror("Error in fstat");
        setPromptColor("default");
        return 1;
    }
    close(res);

    return 0;
}

int transferFile(char *fileName, int socket)
{
    uint32_t fSize = 0;
    uint32_t fLastMod = 0;
    FILE    *fptr = NULL;
    char    *tbuf;
    size_t  newLen = 0;
    long    tmpFileSize;
    long    transmittedSize = 0;
    int     n;

    tbuf = malloc(MAXBUFLEN * (sizeof *tbuf));

    signal(SIGPIPE, sigPipeHandler);

    fileName = strtok(fileName, "\r");                                              /* "fileName.txt\r\n" --> "fileName.txt" */

    if((fptr = fopen(fileName, "rb")) == NULL)
    {
        setPromptColor("red");
        fprintf(stderr," An error occured while opening %s\n", fileName);
        setPromptColor("default");
        return 1;
    }

    if(getFileStats(fileName) == 0)
    {
        fSize       = htonl((uint32_t)fileStat.st_size);
        fLastMod    = htonl((uint32_t)fileStat.st_mtime);
    }
    else
        return 1;

    if(socketAbnormalTermination == 1)
        return 1;

    if(writen(socket, ackMsg, strlen(ackMsg)) != strlen(ackMsg))
        return 1;

    if(writen(socket, &fSize, sizeof(uint32_t)) != sizeof(uint32_t))
        return 1;

    tmpFileSize = fileStat.st_size;

    if(tmpFileSize <= MAXBUFLEN)
    {
        newLen = fread(tbuf, sizeof(char), MAXBUFLEN, fptr);

        if (ferror(fptr) != 0)
        {
            setPromptColor("red");
            fputs("Error in reading file", stderr);
            setPromptColor("default");
            return 1;
        }
        else
        {
            tbuf[newLen++] = '\0';

            if(socketAbnormalTermination == 1)
                return 1;

            if(send(socket, tbuf, strlen(tbuf), 0) != strlen(tbuf))
                return 1;
        }
    }
    else
    {
        setPromptColor("cyan");

        while(tmpFileSize > 0)
        {
            newLen = fread(tbuf, sizeof(char), MAXBUFLEN, fptr);

            if (ferror(fptr) != 0)
            {
                setPromptColor("red");
                fputs("Error in reading file", stderr);
                setPromptColor("default");
                return 1;
            }
            else
            {
                tbuf[newLen++] = '\0';
                n = send(socket, tbuf, MAXBUFLEN, 0);
            }

            if(n == -1)
                break;

            transmittedSize += n;
            tmpFileSize     -= n;

            printf("\rSENDING: %c%ld", '%', (transmittedSize*100/(long)fileStat.st_size));
            fflush(stdout);
        }

        setPromptColor("default");
    }

    if(writen(socket, &fLastMod, sizeof(uint32_t)) != sizeof(uint32_t))
        return 1;

    strcpy(tbuf, "");
    fclose(fptr);
    free(tbuf);
    return 0;
}

void service(int s)
{
    char	rbuf[BUFLEN];		                                                /* Receiver buffer */
    int     n;
    size_t  m;
    fd_set  set;
    FD_ZERO(&set);
    FD_SET(s, &set);

    for (;;)
    {
        m = select(FD_SETSIZE, &set, NULL, NULL, &tval);

        if(m > 0)
        {
            if(socketAbnormalTermination == 1)
                break;

            n = recv(s, rbuf, BUFLEN-1, 0);

            if (n < 0)                                                          /* In case of only one of the each sides calls reset() in order to close the connection */
            {
                setPromptColor("red");
                printf("Read error! Connection is being terminated\n");
                setPromptColor("default");

                sendErrorMessage(s);

                close(s);
                break;
            }
            else if(n==0)                                                       /* recv returns zero if there is an agreement on handshake from both sides */
            {
                close(s);
                break;
            }
            else
            {
                setPromptColor("green");
                printf("\n\nReceived data from socket %03d :\n", s);
                printf("\rReceived message is: %s\n", rbuf);
                setPromptColor("default");

                if(TRANSFER_APPROVAL == 1)                                      /* If you want to activate the approval mechanism for every single transfer, set this constant 0 above. */
                {
                    printf("Do you approve the transfer of ");		            /* Approval mechanism for transfer request. If client does not press ENTER in 15 seconds, connection is get aborted */
                    printf("%s", getRequestedFileName(rbuf));
                    printf("to the client? ( Press ENTER )\n");
                    signal(SIGALRM, timeoutHandler);
                    alarm(15);
                    while(1)
                    {
                        if(getchar() == '\n' || getchar() == EOF)
                            break;
                    }
                    Signal(SIGALRM, SIG_IGN);
                    printf("File transfer request has been approved!\n");
                }

                m = select(FD_SETSIZE, NULL, &set, NULL, &tval);

                if(m > 0)
                {
                    if(socketAbnormalTermination == 1)
                        break;

                    if( transferFile(getRequestedFileName(rbuf), s) == 0 )
                    {
                        setPromptColor("green");
                        printf("\n\n<==================================================>\n");
                        printf("\r%s has been successfully transferred!", getRequestedFileName(rbuf));
                        printf("\n<==================================================>");
                        setPromptColor("default");

                        strcpy(rbuf, "");
                    }
                    else
                    {
                        setPromptColor("red");
                        printf("Transfer failure! Connection is being terminated\n");
                        setPromptColor("default");

                        if(socketAbnormalTermination == 1)
                            break;

                        sendErrorMessage(s);
                        close(s);
                        break;
                    }
                }
                else
                {
                    setPromptColor("red");
                    printf("Timeout has expired!\n");
                    setPromptColor("default");

                    sendErrorMessage(s);
                    close(s);
                    break;
                }
            }
        }
        else
        {
            setPromptColor("red");
            printf("Timeout has expired!\n");
            setPromptColor("default");

            sendErrorMessage(s);
            close(s);
            break;
        }
    }
}

int main (int argc, char *argv[])
{
    int		    conn_request_skt;	                                                /* passive socket */
    uint16_t 	lport_n, lport_h;                                                   /* port used by server (net/host ord.) */
    int		    bklog = 2;		                                                    /* Maximum length of pending requests queue */
    int	 	    s;			                                                        /* connected socket */
    socklen_t 	addrlen;
    struct      sockaddr_in saddr, caddr, sladdr, sraddr;	                        /* server and client addresses */

    tval.tv_sec = TIMEOUT;                                                          /* Timeout for any client connection after the server has been initiated */
    tval.tv_usec = 0;

    prog_name = argv[0];

    if (argc != 2)                                                                  /* To verify correctness of the arguments */
    {
        setPromptColor("red");
        printf("Invalid amount of arguments!\n Usage: ./server1_main <port number>\n");
        exit(EXIT_FAILURE);
    }

    if (sscanf(argv[1], "%" SCNu16, &lport_h)!=1)                                   /* get server port number from command line */
    {
        setPromptColor("red");
        err_sys("Invalid port number!\n");
    }

    lport_n = htons(lport_h);

    /* Socket Creation */
    setPromptColor("cyan");
    printf("\nCreating socket...\n");
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);                                  /* Socket in internet family, type stream and protocol TCP */
    setPromptColor("green");
    printf("Done, socket number %u\n", s);
    setPromptColor("default");

    /* Binding the socket to any local IP address */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family      = AF_INET;
    saddr.sin_port        = lport_n;
    saddr.sin_addr.s_addr = INADDR_ANY;                                             /* INADDR_ANY means all zeros */
    setPromptColor("cyan");
    showAddr("Binding to address", &saddr);
    setPromptColor("default");
    Bind(s, (struct sockaddr *) &saddr, sizeof(saddr));                             /* In this part, server has a generic socket that has been bounded to an address. */
    setPromptColor("green");
    printf("Binding has been completed.\n");
    setPromptColor("default");

    /* Listening the socket */
    setPromptColor("cyan");
    printf ("Listening at socket %d with backlog = %d\n",s,bklog);
    Listen(s, bklog);                                                               /* Generic socket becomes a passive socket */
    setPromptColor("green");
    printf("Done\n");
    setPromptColor("default");

    conn_request_skt = s;

    for (;;)                                                                        /* Main server loop  this part, server should never stop */
    {
        fd_set cset;
        FD_ZERO(&cset);
        FD_SET(s, &cset);

        addrlen = sizeof(struct sockaddr_in);                                       /* Accepting next connection.*/

        setPromptColor("cyan");
        printf("\n<==========================>\n");
        printf("Waiting for a new connection...\n");
        printf("<==========================>\n\n");
        setPromptColor("default");

        s = Accept(conn_request_skt, (struct sockaddr *) &caddr, &addrlen);         /*  Every time "Accept" is called, a new socket is created (Socket for each client)
                                                                                        Server calls "accept" and "accept" blocks because there is no request in the queue at the moment.
                                                                                        Server can call "accept" even if there are no connection requests. As soon as request comes, if it is possible, server accepts it. */
        socketAbnormalTermination = 0;

        setPromptColor("green");
        printf("<=========================================>\n");
        showAddr("Accepted connection from: ", &caddr);
        printf("New socket: %u\n",s);
        printf("<=========================================>\n\n");
        setPromptColor("default");

        service(s);                                                                 /* Serving the client on socket s */
    }
}
