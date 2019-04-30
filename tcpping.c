/***************************************************************************************************
  TCP Ping
  Sequentially creates tcp connections to the specified host and measures the latency.
  Author: Alexander Tarasov aka oioki
  Change:20190430
    1. like ping output.
	2. quite mode
	3. help info
    Author: skinfiter
***************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <limits.h>


// I use it mostly for remote servers
#define DEFAULT_PORT 80
#define DEFAULT_COUNT 30
#define DEFAULT_INTEVAL 1
#define QUIET 1


// return time period between t1 and t2 (in milliseconds)
long int timeval_subtract(struct timeval *t2, struct timeval *t1)
{
    return (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
}

// sequence number
static int seq = 0;
static int cnt_successful = 0;

// aggregate stats
unsigned long int diffMin = ULONG_MAX;
unsigned long int diffAvg;
unsigned long int diffMax = 0;
unsigned long int diffSum = 0;
unsigned long int diffSum2 = 0;
unsigned long int diffMdev;

// address
struct sockaddr_in addrServer;

int running = 1;
int quiet = 0;


// one ping
int ping(char * ip, int port,float inteval)
{
    seq++;

    // creating new socket for each new ping
    int sfdInet = socket(PF_INET, SOCK_STREAM, 0);
    if ( sfdInet == -1)
    {
        fprintf(stderr, "Failed to create INET socket, error %d\n", errno);
        return 1;
    }

    // adjusting connection timeout = 1 second
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    int err = setsockopt (sfdInet, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    if ( err < 0 )
        fprintf(stderr, "Failed setsockopt SO_RCVTIMEO, error %d\n", errno);
    err = setsockopt (sfdInet, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    if ( err < 0 )
        fprintf(stderr, "Failed setsockopt SO_SNDTIMEO, error %d\n", errno);

    // note the starting time
    struct timeval tvBegin, tvEnd, tvDiff;
    gettimeofday(&tvBegin, NULL);

    // try to make connection
    err = connect(sfdInet, (struct sockaddr *) &addrServer, sizeof(struct sockaddr_in));
    if ( err == -1 )
    {
        switch ( errno )
        {
            case EMFILE:
                fprintf(stderr, "Error (%d): too many files opened", errno);
                break;
            case ECONNREFUSED:
                fprintf(stderr, "almost Connection refused (error %d) %s:%d, seq=%d\n", errno, ip, port, seq);
                err = close(sfdInet);
                break;
            case EHOSTUNREACH:
                fprintf(stderr, " ....  Host unreachable (error %d) while connecting %s:%d, seq=%d\n", errno, ip, port, seq);
                err = close(sfdInet);
                break;
            case EINPROGRESS:
                fprintf(stderr, " ....  Timeout (error %d) while connecting %s:%d, seq=%d\n", errno, ip, port, seq);
                err = close(sfdInet);
                break;
            default:
                fprintf(stderr, "Error (%d) while connecting %s:%d, seq=%d\n", errno, ip, port, seq);
        }

        // sleeping 1 sec until the next ping
   //     sleep(1);

        return 1;
    }

    // note the ending time and calculate the duration of TCP ping
    gettimeofday(&tvEnd, NULL);
    long int diff = timeval_subtract(&tvEnd, &tvBegin);
    int secs = diff / 1000000;
    if ( quiet != QUIET ){
        printf("  OK   Connected to %s:%d, seq=%d, time=%0.3lf ms\n", ip, port, seq, diff/1000.);
    }
    cnt_successful++;

    // changing aggregate stats
    if ( diff < diffMin ) diffMin = diff;
    if ( diff > diffMax ) diffMax = diff;
    diffSum  += diff;
    diffSum2 += diff*diff;

    // OK, closing the connection
    err = close(sfdInet);

    // sleeping until the beginning of the next second
    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = 1000 * 1000000*(inteval+secs) - 1000 * diff ;
    nanosleep(&ts, &ts);

    return 0;
}

void intHandler()
{
    running = 0;
}

void usage(char * name){
    printf("Usage: %s hostname [-p] [-c] [-i] [-q] [-h] [-v]\n", name);
    printf("\t-p port\n\t-c count\n\t-i inteval\n\t-q quiet\n\t-h show help\n\t-v show version\n");
}

int main(int argc, char * argv[])
{
    if ( argc == 1 )
    {
	usage(argv[0]);
        return 1;
    }
    extern char *optarg;
    char * host = argv[1];
    int port = DEFAULT_PORT;
    int count = DEFAULT_COUNT;
    float inteval = DEFAULT_INTEVAL;
    int opt=0;
    while((opt=getopt(argc,argv,"p:c:i:qhv"))!=-1){
        switch(opt){
          case 'p': port=atoi(optarg);continue;
          case 'c': count=atoi(optarg);continue;
          case 'i': inteval=atof(optarg);continue;
          case 'q': quiet=1;continue;
          case 'v': printf("Version:20190412\n");return 0;
          case 'h': usage(argv[0]);return 0;
          default: usage(argv[0]);return 0;
        }
    }

    //printf("%s %d %d %.2f %d\n",host,port,count,inteval,quiet);
    //return 1;

    // resolving the hostname
    struct hostent * he;
    extern h_errno;
    he = gethostbyname(host);
    if ( he == NULL )
    {
        fprintf(stderr, "tcpping: unknown host %s (error %d)\n", host, h_errno);
        return 1;
    }
    // filling up `sockaddr_in` structure
    memset(&addrServer, 0, sizeof(struct sockaddr_in));
    addrServer.sin_family = AF_INET;
    memcpy(&(addrServer.sin_addr.s_addr), he->h_addr, he->h_length);
    addrServer.sin_port = htons(port);

    // first IP address as the target
    struct in_addr ** addr_list = (struct in_addr **) he->h_addr_list;
    char ip[16];
    strcpy(ip, inet_ntoa(*addr_list[0]));
    // Ctrl+C handler
    signal(SIGINT, intHandler);

    // note the starting time
    struct timeval tvBegin, tvEnd, tvDiff;
    gettimeofday(&tvBegin, NULL);


    // main loop
    int i = 0;
    while (running == 1 && i < count){
        ping(ip, port,inteval);
        i++;
    }

    // note the ending time and calculate the duration of TCP ping
    gettimeofday(&tvEnd, NULL);
    long int diff = timeval_subtract(&tvEnd, &tvBegin);

    // summary
    if ( quiet == QUIET )
    {
//        printf("%d\n",cnt_successful);
        if ( cnt_successful > 0 ){
            diffAvg  = diffSum/cnt_successful;
            diffMdev = sqrt( diffSum2/cnt_successful - diffAvg*diffAvg );
            printf("%s|%s|%d %d %d%%|%.3lf %.3lf %.3lf %.3lf",host,ip,seq,cnt_successful,100-100*cnt_successful/seq,diffMin/1000.,diffAvg/1000.,diffMax/1000.,diffMdev/1000.);
        }else{
            printf("%s|%s|%d %d %d%%|",host,ip,seq,cnt_successful,100-100*cnt_successful/seq);
        }
    }else{
      printf ("\n--- %s tcpping statistics ---\n", host);
      printf ("%d packets transmitted, %d received, %d%% packet loss, time %ldms\n", seq, cnt_successful, 100-100*cnt_successful/seq, diff/1000);
      if ( cnt_successful > 0 )
      {
        diffAvg  = diffSum/cnt_successful;
        diffMdev = sqrt( diffSum2/cnt_successful - diffAvg*diffAvg );
        printf ("rtt min/avg/max/mdev = %0.3lf/%0.3lf/%0.3lf/%0.3lf ms\n", diffMin/1000.,diffAvg/1000.,diffMax/1000.,diffMdev/1000.);
      }
    }
    return 0;
}
