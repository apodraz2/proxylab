/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Adam Podraza, apodra86@gmail.com
 *     
 * This is a multi-threaded proxy server that uses a "threadArgs" struct to pas
 * s necessary information. Volatile operations are kept stable using semaphore
 * . The server only takes GET requests and returns an error on other requests.
 * The server successfully passes HTTP/1.0. 
 */ 

#include "csapp.h"

//pointer to logfile
FILE *logfile;

//struct to pass thread arguments
struct threadArgs{
  int fd;
  int threadIdent;
  struct sockaddr_in clientAddress;
  struct hostent *hostp;
  int connport;
  char *hap;
};

/*
 * Function prototypes
 */

void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void getHTTP(int connfd, int port, struct sockaddr_in *sockaddr, int threadIdent);
void *threadCode(void *vargp);

/*Rio wrappers*/
ssize_t Rio_readnb_s(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readn_s(int fd, void *usrbuf, size_t n);
void Rio_writen_s(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_s(rio_t *rp, void *usrbuf, size_t n);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
  int clientfd, clientlen, port, id = 0;
  struct sockaddr_in clientAddress;

  pthread_t tid;

    /* Check arguments */
    if (argc != 2) {
      //need to specify port number 
	fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	exit(0);
    }
    port = atoi(argv[1]);
    
    clientfd = Open_listenfd(port);

    Signal(SIGPIPE, SIG_IGN);

    while (1){

      struct threadArgs *thread;

      //call to malloc to initialize separate memory for thread struct
      thread = Malloc(sizeof(struct threadArgs));
      clientlen = sizeof(clientAddress);

      //assigning accepted filedescriptor to corresponding struct slot
      thread->fd = Accept(clientfd, (SA *)&clientAddress, (socklen_t *)&clientlen);

      //passing client port
      thread->connport = port;

      //passing clientaddress struct
      thread->clientAddress = clientAddress;

      //passing host
      sem_t mutex;
      sem_init(&mutex, 0, 1);
      P(&mutex);
      thread->hostp = Gethostbyaddr((const char*)&clientAddress.sin_addr.s_addr, sizeof(clientAddress.sin_addr.s_addr), AF_INET);

      //increment the id number for each thread, used for debugging
      thread->threadIdent = id++;
      V(&mutex);
      //passing address
      thread->hap = inet_ntoa(clientAddress.sin_addr);

      //create thread with pthread_t id declared earlier, the function threadCode for threads to execute, and a struct containing the thread arguments
      Pthread_create(&tid, NULL, threadCode, thread);
    }
    Close(clientfd);
    exit(0);
}

/* This is the function to implement thread code
 * Takes info from passed thread struct so thread can use as parameters in getH * TTP
 */
void *threadCode(void *vargp){
  //new thread is created

  //variables to pass in the needed info from the thread struct
  int connfd, port, threadIdent;
  char *hap;
  struct hostent *hostp;
  struct sockaddr_in clientAddress;
  struct threadArgs *thread = ((struct threadArgs *) vargp);

  clientAddress = thread->clientAddress;


  connfd = thread->fd;

   port = thread->connport;

   hap = thread->hap;
  
   hostp = thread->hostp; 

   threadIdent = thread->threadIdent;
   
   Free(vargp);
   pthread_detach(pthread_self());
   printf("Thread %d received request from %s\n", threadIdent, hap);


   getHTTP(connfd, port, &clientAddress, threadIdent);
   Close(connfd);
   return NULL;
}


/* This is the given URI parser
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;
    
    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
   
    *port = 80; /* default */
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(pathname, pathbegin);
    }
    return 0;
}

/* This is the given formatter
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}


/* 
 * getHTTP: this function takes as parameters the client file descriptor, the h * ost port and an address to connect with. These are called by threads.
 * See notes in code for further desciptions.
 */
void getHTTP(int connfd, int port, struct sockaddr_in *sockaddr, int threadIdent)
{
  int i, size, parse, newPort;
  char buf[MAXLINE];
 
  char method[MAXLINE]; 
  char uri[MAXLINE];
  char *vers = "HTTP/1.0\r\n";
  char host[MAXLINE], path[MAXLINE];
  char log[MAXLINE];
  //  char *buf1;
  rio_t rio;
  char version[MAXLINE];
  struct sockaddr_in *address = sockaddr;
  //  char buf2[MAXLINE];
  Rio_readinitb(&rio, connfd);

  int hostfd = 0;
  rio_t riohost;
  int client = 0;

  while(1){
    //reads in request from client and parses 
    if((i = Rio_readlineb_s(&rio, buf, MAXLINE)) != 0){
   
      sscanf(buf, "%s %s %s", method, uri, version);
      printf("Method: %s\n", method);
      if(strcasecmp(method, "GET")){
        printf("Invalid Request. Can only handle GET.\n");
        return;
      }
      printf("URI: %s\n", uri);
      

      //Reenter info into buf array with HTTP/1.0 as version
      //Not sure why, but proxy will only load the class page with if this compare function is utilized. Also empties out the buffer.
      if(strcasecmp(uri, "reed.cs.depaul.edu/lperkovic/csc407/")){
	bzero(buf, MAXLINE);
	sprintf(buf, "%s %s %s\n", method, uri, vers);
      }
      printf("Request: %s\n", buf);

      //call to parse_uri
      parse = parse_uri(uri, host, path, &newPort);
      printf("Host: %s\n", host);
      printf("Path: %s\n", path);

      //sets hostfd as the host file descriptor and checks for errors
      if ((hostfd = open_clientfd(host, newPort)) < 0)
      {
	break;
      }

      //initialize connection to host
      Rio_readinitb(&riohost, hostfd);
      Rio_writen_s(hostfd, buf, strlen(buf));
      client = 0;
    
      while(!client && ((i = Rio_readlineb_s(&rio, buf, MAXLINE)) != 0))
	{
	  printf("Thread %d: Forwarding request to host\n", threadIdent);
	  Rio_writen_s(hostfd, buf, i);
	  client = (buf[0] == '\r');
	}

      printf("*** End of Request From Client ***\n");

      //while loop that reads in bytes from host and writes input from host to output to client
	while ((i = Rio_readnb_s(&riohost, buf, MAXLINE)) != 0)
	  {
	    printf("Thread %d: Forwarding response from host\n", threadIdent);
	    //	    if(strcasecmp(uri, "reed.cs.depaul.edu/lperkovic/407/"))
	    //printf("Host response: %s", buf);
	    Rio_writen_s(connfd, buf, i);

	    size+=i;
	    bzero(buf, MAXLINE);
	  }
	printf("*** End of Request**\n");
	printf("Thread %d: Forwarded %d bytes from end server to client\n", threadIdent, size);

	//format the requests to print to logfile
	  format_log_entry(log, address, uri, size);

	  //initialize semaphore
	  sem_t mutex2;
	  sem_init(&mutex2, 0, 1);

	  //call to P(s)
	  P(&mutex2);

	  //opens the logfile
	  logfile = fopen("proxy.log", "a");

	  //print each request into the logfile
	  fprintf(logfile, "%s %d\n", log, size);
	  
	  fflush(logfile);
	  
	  //call to V(s) 
	  V(&mutex2);

	  //close host file descriptor
	  Close(hostfd);
	  break;
	  }
      else
	{
	  break;

	}
  }
}

/*Rio Wrappers*/
ssize_t Rio_readnb_s(rio_t *rp, void *buf, size_t i)
{
  size_t bytes;
  if((bytes = rio_readnb(rp, buf, i))<0)
    {
      printf("readnb failed\n");
      return 0;
    }
  return bytes;
}

ssize_t Rio_readn_s(int fd, void *buf, size_t i)
{
  size_t bytes;
  if ((bytes = rio_readn(fd, buf, i)) < 0)
    {
      printf("readn failed\n");
      return 0;
    }
  return bytes;
}

void Rio_writen_s(int fd, void *buf, size_t i)
{
  if(rio_writen(fd, buf, i) != i)
    {
      printf("writen failed\n");
      return;
    }
}

ssize_t Rio_readlineb_s(rio_t *rp, void *buf, size_t i)
{
  ssize_t rc;
  if((rc = rio_readlineb(rp, buf, i)) < 0)
    {
      printf("readlineb failed\n");
      return 0;
    }
  return rc;
}
