#include    <time.h>
#include    <fcntl.h>
#include    <errno.h>
#include    <stdlib.h>
#include    <string.h>
#include    <sys/time.h>
#include    <inttypes.h>
#include    "../errlib.h"
#include    "../sockwrap.h"

#define BUFLEN	  128                                   /* Buffer Length */
#define MAXBUFLEN 1000                                  /* Buffer Length for file content chunks */
#define TIMEOUT   15                                    /* timeout is 15 seconds */

/* GLOBAL VARIABLES */

char    *prog_name;
char    ackMsg[5] = "+OK\r\n";
struct  timeval tval;
int     activeSocket;                                   /* In order to use in signal handler */


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

void sigIntHandler(int signal)
{
    if(signal == SIGINT)
    {
        setPromptColor("red");
        printf("\nConnection has been aborted by client! Socket is being closed!\n");

        close(activeSocket);
        exit(EXIT_FAILURE);
    }
}

void printTransferInfo(char *fileName, uint32_t fileSize, uint32_t fileLastMod)
{
    off_t tmp_size      = fileSize;
    time_t tmp_lastMod  = fileLastMod;

    struct tm lt;
    char timbuf[80];

    localtime_r(&tmp_lastMod, &lt);
    strftime(timbuf, sizeof(timbuf), "%c", &lt);

    setPromptColor("green");
    printf("\n\n     ===========================================================\n");
    setPromptColor("default");
    printf("     NAME OF FILE:\t\t\t%s\n", fileName);
    printf("     SIZE OF FILE:\t\t\t%lu bytes\n", tmp_size);
    printf("     LAST MODIFICATION OF FILE:\t\t%s", timbuf);
    setPromptColor("green");
    printf("\n     ===========================================================\n");
}

int fileTransmission(int socket, char *fileName)
{
    char *rbuf;
    int fileDesc;
    int n;
    uint32_t fileSize;
    long    tmpFileSize;
    long    transmittedSize = 0;
    uint32_t fileLastMod;

    rbuf = malloc(MAXBUFLEN * (sizeof *rbuf));

    if((readn(socket, rbuf, strlen(ackMsg)) == strlen(ackMsg)) && (strcmp(rbuf, ackMsg) == 0))  /* To verify that the first 5 bytes are equal to "+OK\r\n

                                                                                                   IN CASE OF RECEIVING "-ERR\r\n" MESSAGE (FILE NOT FOUND etc.), THIS BLOCK WILL BE DISCARDED AND FUNCTION WILL RETURN 1 */
    {
        strcpy(rbuf, "");                                                                       /* Just a precaution */

        if((fileDesc = open(fileName, O_WRONLY | O_CREAT, 0777)) == -1)                         /* To create the file */
        {
            setPromptColor("red");
            printf("File has not been created! Error Number: % d\n", errno);
            setPromptColor("default");
            return 1;
        }

        if(readn(socket, &fileSize, sizeof (uint32_t)) == sizeof (uint32_t))                     /* To read fileSize  */
        {
            fileSize = ntohl(fileSize);

            tmpFileSize = fileSize;

            if(fileSize <= MAXBUFLEN)
            {
                if(recv(socket, rbuf, fileSize, 0) == fileSize)
                {
                    if(write(fileDesc, rbuf, fileSize) != fileSize)
                    {
                        setPromptColor("red");
                        printf("File has not been created! Error Number: % d\n", errno);
                        setPromptColor("default");
                        return 1;
                    }
                }
                else
                    return 1;
            }
            else
            {
                setPromptColor("cyan");

                while(tmpFileSize > 0)
                {
                    n = recv(socket, rbuf, MAXBUFLEN, 0);

                    if(n == 0)
                    {
                        setPromptColor("red");
                        printf("\nTransfer Error! Connection has been either aborted or harmed\n");
                        close(socket);
                        exit(EXIT_FAILURE);
                    }

                    if(write(fileDesc, rbuf, n) != n)
                    {
                        setPromptColor("red");
                        printf("File has not been created! Error Number: % d\n", errno);
                        setPromptColor("default");
                        return 1;
                    }

                    transmittedSize += n;
                    tmpFileSize -= n;

                    printf("\rRECEIVING: %c%ld", '%', (transmittedSize*100/(long)fileSize));
                    fflush(stdout);
                }

                setPromptColor("default");
            }

            if(readn(socket, &fileLastMod, sizeof (uint32_t)) == sizeof (uint32_t))                             /* To read file last modification date */
                fileLastMod = ntohl(fileLastMod);
            else
                return 1;
        }
        else
            return 1;
    }
    else
        return 1;

    printTransferInfo(fileName, fileSize, fileLastMod);

    strcpy(rbuf, "");
    free(rbuf);
    return 0;
}

int main(int argc, char *argv[])
{
    char        tbuf[BUFLEN];		                                            /* transmission buffer */
    uint16_t    tport_n, tport_h;	                                            /* server port number (network byte/host byte order) */
    int		    s;
    int		    res;
    struct      sockaddr_in	saddr;		                                        /* server address structure */
    struct      in_addr	sIPaddr; 	                                            /* server IP addr. structure */

    prog_name = argv[0];

    signal(SIGINT, sigIntHandler);

    tval.tv_sec = TIMEOUT;                                                      /* Timeout for any client connection after the server has been initiated */
    tval.tv_usec = 0;

    if (argc < 4)                                                               /* To verify correctness of the arguments */
    {
        setPromptColor("red");
        printf("Invalid amount of arguments!\n Usage: ./client1_main <IP Addr> <Port> <File1> <File2> ...\n");
        exit(EXIT_FAILURE);
    }

    /* input IP address and port of server */
    res = inet_aton(argv[1], &sIPaddr);                                         /* Convertion from dotted to undotted and saves in sIPaddr. Returns 1 in success */
    if (!res)
    {
        setPromptColor("red");
        err_quit("Invalid address");
    }

    /* We get port number as a string using mygetline and parse it using sscanf. We dont know that is int equals 16 or  32 bits in every system. "SCNu16" macro provides 16 bits unsigned int in every system */
    if (sscanf(argv[2], "%" SCNu16, &tport_h)!=1)
    {
        setPromptColor("red");
        err_quit("Invalid port number");
    }

    tport_n = htons(tport_h);                                                   /* To be sure in saving port number in network byte order. */

    /* Socket Creation */
    setPromptColor("cyan");
    printf("Creating socket\n");
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setPromptColor("green");
    printf("Done, socket number %u\n", s);
    setPromptColor("default");

    activeSocket = s;

    /* prepare address structure */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = tport_n;
    saddr.sin_addr   = sIPaddr;

    /* connect */
    setPromptColor("cyan");
    showAddr("Connecting to target address", &saddr);
    setPromptColor("default");

    Connect(s, (struct sockaddr *) &saddr, sizeof(saddr));

    setPromptColor("green");
    printf("Done.\n");
    setPromptColor("default");

    /* Client Main Loop */
    for(int i=3; i<argc; i++)
    {
        fd_set cset;
        FD_ZERO(&cset);
        FD_SET(s, &cset);

        size_t n;
        size_t msgLength;

        strcpy(tbuf, "GET ");
        strcat(tbuf, argv[i]);
        strcat(tbuf, "\r\n");                                                   /* tbuf = "GET <fileName>CRLF" */

        msgLength = strlen(tbuf);

        n = select(FD_SETSIZE, NULL, &cset, NULL, &tval);                       /* We call "select" and select will block until s is ready to read or until timeout expires */

        if(n > 0)
        {
            if(writen(s, tbuf, msgLength) != (msgLength))
            {
                setPromptColor("red");
                printf("Error in sending the message!\n");
                setPromptColor("default");

                break;
            }
            else
            {
                setPromptColor("cyan");
                printf("Waiting...\n\n");
                setPromptColor("default");

                strcpy(tbuf, "");
            }

            n = select(FD_SETSIZE, &cset, NULL, NULL, &tval);

            if(n > 0)                                                           /* If file transfer request is neither approved nor declined by server in 15 seconds, connection is closed by server side */
            {
                if(fileTransmission(s, argv[i]) != 0)                           /* In case of receiving "-ERR\r\n" message or etc. */
                {
                    setPromptColor("red");
                    printf("Transmission has failed! Program is terminated! \n");
                    close(s);
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                setPromptColor("red");
                printf("Timeout has expired!\n");
                close(s);
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            setPromptColor("red");
            printf("Timeout has expired!\n");
            close(s);
            exit(EXIT_FAILURE);
        }
    }

    close(s);
    exit(EXIT_SUCCESS);
}
