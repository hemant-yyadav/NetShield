#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_BYTES 4096 // max allowed size of request/response

#define MAX_CLIENTS 400 // max number of client requests served at a time

#define MAX_SIZE 200 * (1 << 20)        // size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element cache_element; // to save time writing struct cache_element again and again

// creating LRU cache based on time using linkedlist

struct cache_element
{
    char *data;            // data stores response that we had previously time from server
    int len;               // length of data i.e.. sizeof(data)...
    char *url;             // url stores the request
    time_t lru_time_track; // lru_time_track stores the latest time the element is  accesed
    cache_element *next;   // pointer to next element
};

// methods for lru implementation
cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port_number = 8080;     // Default Port
int proxy_socketId;         // socket descriptor of proxy server
pthread_t tid[MAX_CLIENTS]; // array to store the thread ids of clients bc of multithreading env
sem_t semaphore;            // if client requests exceeds the max_clients this seamaphore puts the
                 // waiting threads to sleep and wakes them when traffic on queue decreases
// sem_t cache_lock;

pthread_mutex_t lock; // lock is used for locking and unlocking the lru cache

cache_element *head; // pointer to the cache
int cache_size;      // cache_size denotes the current size of the cache

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

int connectRemoteServer(char *host_addr, int port_num)
{
    // Creating Socket for remote server ---------------------------

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }

    // Get host by the name or ip address provided

    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");
        return -1;
    }

    // inserts ip address and port number of host in struct `server_addr`
    struct sockaddr_in server_addr;

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    //This line of code copies the IP address from the hostent structure into a sockaddr_in structure

    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to Remote server ----------------------------------------------------

    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }
    // free(host_addr);
    return remoteSocket;
}

int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq)
{
    // Build a new HTTP request
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    /*
        Step 4: Why set "Connection: close" at all?
By default, HTTP/1.1 uses persistent connections (keep-alive). That means:
Server may leave the TCP socket open after sending a response.
Your proxy’s recv() loop could hang waiting for more data.
By forcing "Connection: close", you tell the server:
“Send me the response, then close the socket.”
Your proxy can then stop reading once recv() returns 0 (EOF).
    */

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("set header key not work\n");
    }

    // Checks whether the client sent a Host header.
    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
        }
    }

    //adding headers into buffer
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
    {
        printf("unparse failed\n");
        // return -1;				// If this happens Still try to send request without header
    }

    int server_port = 80; // Default Remote Server Port - the server we will send rewuest to 
    if (request->port != NULL)
        server_port = atoi(request->port);

    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if (remoteSocketID < 0)
        return -1;

        //sending to server

    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

    bzero(buf, MAX_BYTES);

    //receiving from server 
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    //creating new buffer
    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES); // temp buffer
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_send > 0)
    {
        //forwarding received data to client 
        bytes_send = send(clientSocket, buf, bytes_send, 0);

        //save response from server to store in cahce for future purpose  

        for (int i = 0; i < bytes_send / sizeof(char); i++)
        {
            temp_buffer[temp_buffer_index] = buf[i];
            // printf("%c",buf[i]); // Response Printing
            temp_buffer_index++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);

        if (bytes_send < 0)
        {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);

        bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buf);
    //adding response to lru
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    printf("Done\n");
    free(temp_buffer);

    close(remoteSocketID);
    return 0;
}

int checkHTTPversion(char *msg)
{
    int version = -1;

    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1; // Handling this similar to version 1.1
    }
    else
        version = -1;

    return version;
}

void *thread_fn(void *socketNew) 
{
    sem_wait(&semaphore); //checks semaphore value , if it is negative it waits if not , it increments and move on  
    int p;
    sem_getvalue(&semaphore, &p);
    printf("semaphore value:%d\n", p);
    int *t = (int *)(socketNew);
    int socket = *t;            // Socket is socket descriptor of the connected Client
    int bytes_send_client, len; // Bytes Transferred

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // Creating buffer of 4kb for a client

    bzero(buffer, MAX_BYTES);                               // Making buffer zero
     
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server

    while (bytes_send_client > 0)
    {
        len = strlen(buffer);
        // loop until u find "\r\n\r\n" in the buffer
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }

    // printf("--------------------------------------------\n");
    // printf("%s\n",buffer);
    // printf("----------------------%d----------------------\n",strlen(buffer));

    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    // tempReq, buffer both store the http request sent by client
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }

    // checking for the request in cache
    struct cache_element *temp = find(tempReq);

    if (temp != NULL)
    {
        // request found in cache, so sending the response to client from proxy's cache
        int size = temp->len / sizeof(char); 
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {  
            bzero(response, MAX_BYTES);
            for (int i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrived from the Cache\n\n");
        printf("%s\n\n", response);
        // close(socketNew);
        // sem_post(&seamaphore);
        // return NULL;
    }

    //REQUEST NOT FOUND IN CACHE 

    else if (bytes_send_client > 0) //to check. if we have successfully receuved request from client 
    {
        len = strlen(buffer);
        // Parsing the request
        struct ParsedRequest *request = ParsedRequest_create();

        // ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
        //  the request
        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Parsing failed\n");
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {

                if (request->host && request->path && (checkHTTPversion(request->version) == 1))
                {
                    bytes_send_client = handle_request(socket, request, tempReq); // Handle GET request
                    if (bytes_send_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                        //If forwarding failed → send HTTP 500 Internal Server Error response back to client. 
                    }
                }
                else
                    sendErrorMessage(socket, 500); // 500 Internal Error
            }  
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
        }
        // freeing up the request pointer
        ParsedRequest_destroy(request);
    }

    else if (bytes_send_client < 0)
    {
        perror("Error in receiving from client.\n");
    }
    else if (bytes_send_client == 0)
    {
        printf("Client disconnected!\n");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    

    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value:%d\n", p);
    free(tempReq);
    sem_post(&semaphore);
    return NULL;
}

int main(int argc, char *argv[])
{

    int client_socketId;                         // client_socketId == to store the client socket id
    int client_len;                              // client url length request len
    struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned ; adress contains ip adress and port number to bind

    sem_init(&semaphore, 0, MAX_CLIENTS); // Initializing seamaphore and lock
    pthread_mutex_init(&lock, NULL);      // Initializing lock for cache

    if (argc == 2) // checking whether two arguments are received or not
    {
        // argv[0] is program name and argv[1] is port no
        // ./proxy 8080
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments\n");
        exit(1); // exiting the program
    }

    printf("Setting Proxy Server Port : %d\n", port_number);

    // creating the main proxy socket

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    /* 
        sockfd = socket(domain , type, protocol)
        domain = AFINET for ipv4 ; 
        TYPE: SOCK_STREAM: TCP(reliable, connection-oriented) AND SOCK_DGRAM: UDP(unreliable, connectionless)
        protocol: Protocol value for Internet Protocol(IP), which is 0.

     */

     

    if (proxy_socketId < 0) //ERROR if fail to create socket
    {
        perror("Failed to create socket.\n");
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed\n");

    /* 
        setsockopt 
        It’s a system call in C used to change settings (options) on a socket.
        ARGUMENTS:
        int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

        level: Which “layer” the option belongs to.
        Common values:
        SOL_SOCKET → generic socket-level options.
        IPPROTO_TCP → TCP-specific options.
        IPPROTO_IP → IP-level options.

        optname: Which option you want to set.
        SO_REUSEADDR → allow reusing the same address/port.

        const void *optval : It wants a generic pointer to some memory that holds the option’s value.


    
    */

    bzero((char *)&server_addr, sizeof(server_addr)); //cleaning up garbage value that was stored in struct 


    server_addr.sin_family = AF_INET; //what kind of addresses (IPv4 or IPv6) 
    server_addr.sin_port = htons(port_number); 
    /*
        sin_port is the port number your server listens on.
        htons() means host-to-network short.
        Computers use different byte orders (endianness).
        TCP/IP requires network byte order = big-endian.
        htons() converts the int from your CPU’s order (host order) into the required network order.
        Example:
        On little-endian machine, port 8080 in memory is 90 1F.
        htons(8080) rearranges it to 1F 90 so it matches network standard.
    */
     
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Any available ip adress assigned ,will accept connections on any IP address assigned to my machine

    // Binding the socket
    /*
        bind(sockfd, sockaddr *addr, socklen_t addrlen);
        the bind() function binds the socket to the address and port number specified in addr(custom data structure). In the example code, we bind the server to the localhost, hence we use INADDR_ANY to specify the IP address.
    */
    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port is not free\n");
        exit(1);
    }
    printf("Binding on port: %d\n", port_number);

     
    // Proxy socket listening to the requests
    //listen(sockfd, backlog);
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);

    if (listen_status < 0)
    {
        perror("Error while Listening !\n");
        exit(1);
    }


    int i = 0;                           // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
    int Connected_socketId[MAX_CLIENTS]; // This array stores socket descriptors of connected clients

    // Infinite Loop for accepting connections
    while (1)
    {

        bzero((char *)&client_addr, sizeof(client_addr)); // Clears struct client_addr
        client_len = sizeof(client_addr);

        // Accepting the connections
        //new_socket= accept(sockfd, sockaddr *addr, socklen_t *addrlen);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len); // Accepts connection
        if (client_socketId < 0)
        {
            fprintf(stderr, "Error in Accepting connection !\n");
            exit(1);
        }
        else
        {
            Connected_socketId[i] = client_socketId; // Storing accepted client into array
        }
 
        // Getting IP address and port number of client
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr; //creating copy of adress
        struct in_addr ip_addr = client_pt->sin_addr;  //Extract the IP address from the client’s socket info.
        char str[INET_ADDRSTRLEN]; 
        /*
            INET_ADDRSTRLEN is a constant (defined in <netinet/in.h>).
            It’s the maximum length of a string that can hold an IPv4 address in dotted-decimal form (e.g., "255.255.255.255\0" = 16 chars).
            So str is a buffer big enough to hold any IPv4 string representation.
        */
        
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        /*
            inet_ntop() converts a binary IP address → into a string.
            AF_INET → we’re converting an IPv4 address.
            &ip_addr → the raw binary address we got earlier.
            str → the buffer to store the result (human-readable IP).
            INET_ADDRSTRLEN → buffer size.
        */

        printf("Client is connected with port number: %d and ip address: %s \n", ntohs(client_addr.sin_port), str);
        //ntohs() (network-to-host short) converts it back into host order (little-endian if needed).
        // printf("Socket values of index %d in main function is %d\n",i, client_socketId);
        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]); // Creating a thread for each client accepted
        /*
            int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
            &tid[i] → thread ID is stored here.
            NULL → default thread attributes.
            thread_fn → the function that will run in this thread.
            (void *)&Connected_socketId[i] → pass the connected client’s socket FD as the argument (casted to void *).

        */
        

        i++;
    }
    close(proxy_socketId); // Close socket
    return 0; 
}

cache_element *find(char *url)
{

    // Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element *site = NULL;
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
    if (head != NULL)
    {
        site = head;
        while (site != NULL)
        {
            if (!strcmp(site->url, url))
            {
                printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
                // Updating the time_track
                site->lru_time_track = time(NULL);
                printf("LRU Time Track After : %ld", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }
    else
    {
        printf("\nurl not found\n");
    }
    // sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
    return site;
}

void remove_cache_element()
{
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element *p;    // Cache_element Pointer (Prev. Pointer)
    cache_element *q;    // Cache_element Pointer (Next Pointer)
    cache_element *temp; // Cache element to remove
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
    if (head != NULL)
    { // Cache != empty
        for (q = head, p = head, temp = head; q->next != NULL;
             q = q->next)
        { // Iterate through entire cache and search for oldest time track
            if (((q->next)->lru_time_track) < (temp->lru_time_track))
            {
                temp = q->next;
                p = q;
            }
        }
        if (temp == head)
        {
            head = head->next; /*Handle the base case*/
        }
        else
        {
            p->next = temp->next;
        }
        cache_size = cache_size - (temp->len) - sizeof(cache_element) -
                     strlen(temp->url) - 1; // updating the cache size
        free(temp->data);
        free(temp->url); // Free the removed element
        free(temp);
    }
    // sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
}

int add_cache_element(char *data, int size, char *url)
{
    // Adds element to the cache
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size = size + 1 + strlen(url) + sizeof(cache_element); // Size of the new element which will be added to the cache
    if (element_size > MAX_ELEMENT_SIZE)
    {
        // sem_post(&cache_lock);
        //  If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        // free(data);
        // printf("--\n");
        // free(url);
        return 0;
    }
    else
    {
        while (cache_size + element_size > MAX_SIZE)
        {
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }  
        cache_element *element = (cache_element *)malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data = (char *)malloc(size + 1);                                // Allocating memory for the response to be stored in the cache element
        strcpy(element->data, data);
        element->url = (char *)malloc(1 + (strlen(url) * sizeof(char))); // Allocating memory for the request to be stored in the cache element (as a key)
        strcpy(element->url, url);
        element->lru_time_track = time(NULL); // Updating the time_track
        element->next = head;
        element->len = size;
        head = element;
        cache_size += element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        // sem_post(&cache_lock);
        //  free(data);
        //  printf("--\n");
        //  free(url);
        return 1;
    }
    return 0;
}