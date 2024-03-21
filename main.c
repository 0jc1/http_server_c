#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "server.h"

#define VERSION 23
#define PORT 8080 // default port
#define BUFFER_SIZE 8096

struct MimeType
{
    char *extension;
    char *type;
} mimeTypes[] = {
    {"gif", "image/gif"},
    {"jpg", "image/jpg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"css", "text/css"},
    {"ico", "image/ico"},
    {"zip", "image/zip"},
    {"gz", "image/gz"},
    {"tar", "image/tar"},
    {"htm", "text/html"},
    {"html", "text/html"},
};

enum HttpStatusCode {
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    SERVICE_UNAVAILABLE = 503
};

HTTP_Server http_server;

// Function to get MIME type based on file extension
const char *getMimeType(const char *fileExtension)
{
    if (fileExtension == NULL) {
        return NULL;
    }

    for (int i = 0; mimeTypes[i].extension != NULL; i++)
    {
        int len = strlen(mimeTypes[i].extension);
        if (len > 0 && strncmp(fileExtension, mimeTypes[i].extension, len) == 0)
        {
            return mimeTypes[i].type;
        }
    }
    return NULL;
}

void logMessage(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);

    va_list argCopy;
    va_copy(argCopy, arg);

    int len = vsnprintf(NULL, 0, format, argCopy);
    va_end(argCopy);

    if (len < 0)
    {
        return;
    }

    char buf[len + 1];
    len = vsnprintf(buf, sizeof buf, format, arg);
    if (len < 0)
    {
        return;
    }

    time_t t = time(NULL);
    if (t == -1)
    {
        return;
    }
    struct tm *tm = localtime(&t);
    if (tm == NULL)
    {
        return;
    }
    char time_str[len + 13]; // Assuming HH:MM:SS format
    sprintf(time_str, "%02d:%02d:%02d ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    strcat(time_str, buf);

    printf("%s\n", time_str);

    // output to log file
    /*
    int fd = open("server.log", O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR);

    if (fd == -1)
    {
        perror("Error opening file");
        return;
    }
    char newline = '\n';
    write(fd, &newline, 1); // write new line

    ssize_t bytesWritten = write(fd, time_str, strlen(time_str));
    if (bytesWritten == -1)
    {
        perror("Error writing to file");
    }

    // Close the log file
    close(fd);
    */
}

void cleanup(int sig) {
    
    printf("Cleaning up connections and exiting.\n");
    
    // try to close the listening socket
    if (close(http_server.socket) < 0) {
        fprintf(stderr, "Error calling close()\n");
        exit(EXIT_FAILURE);
    }
    
    // exit with success
    exit(EXIT_SUCCESS);
}

char * render_static_file(char * fileName, long * len) {
	FILE* file = fopen(fileName, "r");

	if (file == NULL) {
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long fsize = ftell(file);
    *len = fsize * sizeof(char);
	fseek(file, 0, SEEK_SET);

	char* temp = malloc(sizeof(char) * (fsize+1));
	char ch;
	int i = 0;
	while((ch = fgetc(file)) != EOF) {
		temp[i] = ch;
		i++;
	}
	fclose(file);
	return temp;
}

void *handle_request(void * client_fd)
{
    int client_socket = * ((int *)client_fd);
    char buffer[BUFFER_SIZE] = {0};

    // get message
    // parse the request
    // print out the correct header
    // print out the file

    size_t i = 0;

    int ret = read(client_socket, buffer, BUFFER_SIZE - 1);

    if (ret == 0 || ret == -1)
    {
        logMessage("failed to read client request");
        return NULL;
    }

    if (ret > 0 && ret < BUFFER_SIZE)
    {
        buffer[ret] = 0;
    }
    else
    {
        buffer[0] = 0;
    }

    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
    {
        logMessage("Only simple GET operation supported");
        return NULL;
    }

    for (i = 4; i < BUFFER_SIZE; i++)
    { // null terminate after the second space to ignore extra stuff
        if (buffer[i] == ' ')
        {
            buffer[i] = 0;
            break;
        }
    }

    logMessage("read request %s", buffer);

    enum HttpStatusCode statusCode = OK;
    char reasonPhrase[20] = "OK";

    for (size_t j = 0; j < i - 1; j++)
    {
        /* check for illegal parent directory use .. */
        if (buffer[j] == '.' && buffer[j + 1] == '.')
        {
            logMessage("Parent directory (..) path names not supported");
            statusCode = FORBIDDEN;
            strcpy(reasonPhrase, "Forbidden");
        }
    }
    //TODO show files in a directory 
    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6)) /* convert no filename to index file */
        (void)strcpy(buffer, "GET /index.html");

    /* work out the file type and check we support it */
    long len;
    const char *fstr = (char *)0;
    char *extension = 0;

    // Find the position of the dot
    char *dotPosition = strchr(&buffer[4], '.');

    if (dotPosition != NULL)
    {
        extension = dotPosition + 1;
    }

    fstr = getMimeType(extension);

    if (fstr == 0)
    {
        logMessage("file extension type not supported");
    }

    char * file_name = &buffer[5];
    char * file_data = render_static_file(file_name, &len);

    if (file_data == NULL) {
        logMessage("failed to open file %s", &buffer[5]);
        statusCode = NOT_FOUND;
        strcpy(reasonPhrase, "Not Found");
    }

    logMessage("SEND");                                                                                                                             /* lseek back to the file start ready for reading */
    sprintf(buffer, "HTTP/1.1 %d %s\r\nServer: nweb/%d.0\r\nContent-Length: %ld\r\nConnection: close\r\nContent-Type: %s\r\n\r\n", statusCode, reasonPhrase, VERSION, len, fstr); /* Header + a blank line */

    (void)send(client_socket, buffer, strlen(buffer), 0);

    (void)send(client_socket, file_data, len, 0);

    sleep(1); /* allow socket to drain before signalling the socket is closed */
    close(client_socket);

    return NULL;
}

int main(int argc, char *argv[])
{
    int port = PORT;
    char *docroot;

    // Parse the port number and doc root from the command-line argument
    if (argc >= 2)
    {
        port = atoi(argv[1]);
        if (argc >= 3)
        {
            docroot = argv[2];
            if (chdir(docroot) == -1)
            {
                (void)printf("ERROR: Can't Change to directory %s\n", docroot);
                exit(4);
            }
        }
    }

    if (docroot == NULL) {
        printf("error: docroot is null\n");
        exit(EXIT_FAILURE);
    }

    if (port <= 0 || port > 65535)
    {
        printf("Invalid port number: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    // set up signal handler for ctrl-c
    (void) signal(SIGINT, cleanup);

    struct sockaddr_in client_address;

    // initiate server
	init_server(&http_server, port);

    logMessage("Server listening on 127.0.0.1:%d...\n", port);

    while (1)
    {
        int *client_fd = malloc(sizeof(int));

        // Accept incoming connection
        if ((*client_fd = accept(http_server.socket, (struct sockaddr *)&client_address, &http_server.address_len)) < 0)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        logMessage("Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

        // Create a new thread to handle client request
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_request, (void *) client_fd);
        pthread_detach(thread_id);
    }

    return 0;
}