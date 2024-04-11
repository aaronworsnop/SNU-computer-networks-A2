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
const char *errMessage500 = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\n";

// Function to close the socket and exit
void close_socket(int sockfd)
{
    close(sockfd);
    exit(-1);
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
                    // Activity on the listening socket (new connection)
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_sockfd < 0)
                    {
                        TRACE("Socket accept failed: %s\n", strerror(errno));
                        continue;
                    }

                    // Add new client socket to the set
                    FD_SET(client_sockfd, &read_fds);
                    if (client_sockfd > max_fd)
                    {
                        max_fd = client_sockfd;
                    }
                }
                else
                {
                    // Handle HTTP request
                }
            }
        }
    }
}