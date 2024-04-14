#include <stdio.h>
#include <errno.h>
#include <sys/types.h> /* See NOTES */
#include <sys/socket.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/select.h>

#include "macro.h"
#define MAX_VAL (MAX_HDR)
#define MAX_URL 1024

static const char *g_rootDir = "./"; /* root directory */
const char *errMessage400 = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n";
const char *errMessageNotFound = "HTTP/1.0 404 Not Found\r\n\r\n";
const char *errMessage500 = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\n";

// Function to close the socket and exit
void close_socket(int sockfd)
{
    close(sockfd);
    exit(-1);
}

// Function to handle client requests
void handle_request(int client_sock)
{
    // Recieve the request from the client
    char *request = malloc(MAX_CONT + MAX_VAL + MAX_URL);
    int bytes_recieved = 0;
    int total_bytes_recieved = 0;

    while ((bytes_recieved = recv(client_sock, request + total_bytes_recieved, MAX_CONT + MAX_VAL + MAX_URL - total_bytes_recieved, 0)) > 0)
    {
        total_bytes_recieved += bytes_recieved;
        request[total_bytes_recieved] = '\0';

        if (bytes_recieved == 0 || strstr(request, "\r\n\r\n") != NULL)
        {
            // End of request
            break;
        }
    }

    if (total_bytes_recieved < 0)
    {
        TRACE("Error receiving request: %s\r\n", strerror(errno));
        close_socket(client_sock);
    }
    else if (total_bytes_recieved == 0)
    {
        TRACE("Client closed connection.\r\n");
        close_socket(client_sock);
    }

    printf("Request: %s\n", request);

    // Check if the request is well formed (Includes GET, a URL, HTTP/1.0 or HTTP/1.1 and a host header)
    char *get = strstr(request, "GET");
    if (get == NULL)
    {
        TRACE("Bad request.\r\n");
        send(client_sock, errMessage400, strlen(errMessage400), 0);
        close_socket(client_sock);
    }

    // Extract the URL from the request
    char *url = get + 4;
    char *end_url = strstr(url, " ");
    if (end_url == NULL)
    {
        TRACE("Bad request.\r\n");
        send(client_sock, errMessage400, strlen(errMessage400), 0);
        close_socket(client_sock);
    }

    // Check the HTTP version
    if (strstr(end_url, "HTTP/1.0") == NULL && strstr(end_url, "HTTP/1.1") == NULL)
    {
        TRACE("Bad request.\r\n");
        send(client_sock, errMessage400, strlen(errMessage400), 0);
        close_socket(client_sock);
    }

    char *http = strstr(end_url, "HTTP/1.");
    char *end_http = http + 7;

    // Turn the rest of the request into lowercase
    for (char *c = end_url; *c != '\0'; c++)
    {
        *c = tolower(*c);
    }

    // Check if the request contains a host header
    char *host = strstr(end_url, "host:");
    if (host == NULL)
    {
        TRACE("Bad request.\r\n");
        send(client_sock, errMessage400, strlen(errMessage400), 0);
        close_socket(client_sock);
    }

    int persistent = 0;
    if (*end_http == '0')
    {
        // HTTP/1.0, non-persistent connection
        persistent = 0;
    }
    else
    {
        // HTTP/1.1, persistent connection unless specified otherwise
        persistent = 1;

        char *connection = strstr(end_http, "connection:");
        if (connection != NULL)
        {
            char *end_connection = connection + 11;
            if (strstr(end_connection, "close") != NULL)
            {
                // Specify non-persistent connection
                persistent = 0;
            }
        }
    }

    // Extract the URL (appended to the root directory)
    char filepath[MAX_URL];
    memset(filepath, 0, MAX_URL);
    size_t url_length = end_url - url;

    // Construct the file path using snprintf to ensure null termination
    snprintf(filepath, sizeof(filepath), "%s%.*s", g_rootDir, (int)url_length, url);

    int file = open(filepath, O_RDONLY);
    if (file < 0)
    {
        TRACE("Error opening file: %s\r\n", strerror(errno));
        send(client_sock, errMessageNotFound, strlen(errMessageNotFound), 0);
        close(client_sock);
    }

    // Find the content-length
    struct stat file_stat;
    fstat(file, &file_stat);
    int content_length = file_stat.st_size;

    // Capture content
    char *content = malloc(content_length);
    read(file, content, content_length);

    // Send the response
    char *response = malloc(MAX_VAL + MAX_URL + content_length);
    int bytes_sent = 0;
    if (persistent == 0)
    {
        // Non-persistent connection
        snprintf(response, MAX_VAL + MAX_URL + content_length, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nConnection: Close\r\n\r\n%s", content_length, content);

        // send the response
        while (bytes_sent < strlen(response))
        {
            int current_bytes_sent = send(client_sock, response + bytes_sent, strlen(response) - bytes_sent, 0);
            if (current_bytes_sent < 0)
            {
                TRACE("Error sending response: %s\r\n", strerror(errno));
                close_socket(client_sock);
            }
            bytes_sent += current_bytes_sent;
        }
        close(client_sock);
    }
    else
    {
        // Persistent connection
        snprintf(response, MAX_VAL + MAX_URL + content_length, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s", content_length, content);

        // send the response
        while (bytes_sent < strlen(response))
        {
            int current_bytes_sent = send(client_sock, response + bytes_sent, strlen(response) - bytes_sent, 0);
            if (current_bytes_sent < 0)
            {
                TRACE("Error sending response: %s\r\n", strerror(errno));
                close_socket(client_sock);
            }
            bytes_sent += current_bytes_sent;
        }
    }

    if (bytes_sent < 0)
    {
        TRACE("Error sending response: %s\r\n", strerror(errno));
        close_socket(client_sock);
    }

    // Free memory
    free(request);
    free(content);
    free(response);
    close(file);
}

/*--------------------------------------------------------------------------------*/
static void
PrintUsage(const char *prog)
{
    printf("usage: %s -p port -d rootDirectory(optional) \n", prog);
}
/*--------------------------------------------------------------------------------*/
int main(const int argc, const char **argv)
{
    int i;
    int port = -1;

    /* argument parsing */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && (i + 1) < argc)
        {
            port = atoi(argv[i + 1]);
            i++;
        }
        else if (strcmp(argv[i], "-d") == 0 && (i + 1) < argc)
        {
            g_rootDir = argv[i + 1];
            i++;
        }
    }
    if (port <= 0 || port > 65535)
    {
        PrintUsage(argv[0]);
        exit(-1);
    }
    if (access(g_rootDir, R_OK | X_OK) < 0)
    {
        fprintf(stderr, "root dir %s inaccessible, errno=%d\n", g_rootDir, errno);
        PrintUsage(argv[0]);
        exit(-1);
    }

    /* ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    // create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        TRACE("Socket creation failed: %s\n", strerror(errno));
        exit(-1);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any IP
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        TRACE("Socket bind failed: %s\n", strerror(errno));
        close_socket(sockfd);
    }

    if (listen(sockfd, 5) < 0)
    {
        TRACE("Socket listen failed: %s\n", strerror(errno));
        close_socket(sockfd);
    }

    // Set the file descriptor to monitor
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds); // Add listening socket to read set
    int max_fd = sockfd;

    while (1)
    {
        printf("Waiting for connection\n");
        fd_set tmp_fds = read_fds; // Create a temporary copy of read_fds
        int activity = select(max_fd + 1, &tmp_fds, NULL, NULL, NULL);
        if (activity < 0)
        {
            TRACE("Select error: %s\n", strerror(errno));
            continue;
        }

        // Check for activity on file descriptors
        for (int fd = 0; fd <= max_fd; fd++)
        {
            if (FD_ISSET(fd, &tmp_fds))
            {
                if (fd == sockfd)
                {
                    // New connection
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_sock < 0)
                    {
                        if (errno != EWOULDBLOCK && errno != EAGAIN)
                        {
                            perror("accept");
                        }
                    }
                    else
                    {
                        // Add client socket to the set
                        FD_SET(client_sock, &read_fds);
                        if (client_sock > max_fd)
                        {
                            max_fd = client_sock;
                        }
                    }
                }
                else
                {
                    // Handle client request
                    handle_request(fd);
                    FD_CLR(fd, &read_fds); // Remove client socket from set
                }
            }
        }
    }
}