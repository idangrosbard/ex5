#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>

#define MAX_BUFFER_SIZE 1024 * 1024


/// @brief Send contents of file to server in chunks
/// @param sockfd FD for the socket
/// @param file_path Path to the file to send
void send_file(int sockfd, char * file_path) {
    FILE * read_file;
    uint32_t N = 0, bytes_read = 0, bytes_sent = 0, curr_buffer_size = MAX_BUFFER_SIZE, net_N;
    int status = -1;
    char * buffer;

    // open file
    read_file = fopen(file_path, "r");
    if (read_file == NULL) {
        fprintf(stderr, "%s\n", strerror(errno));
        exit(1);
    }

    // get file size
    fseek(read_file, 0, SEEK_END);
    N = (uint32_t)ftell(read_file);
    fseek(read_file, 0, SEEK_SET);
    
    // send file size
    net_N = htonl(N);
    bytes_sent = write(sockfd, &net_N, sizeof(net_N));
    if (bytes_sent != sizeof(net_N)) {
        fprintf(stderr, "%s\n", strerror(errno));
        close(sockfd);
        exit(1);
    }

    // send file contents iteratively
    while (N > 0) {
        // set buffer size
        if (N < MAX_BUFFER_SIZE) {
            curr_buffer_size = N;
        }

        // create buffer
        buffer = malloc(sizeof(char) * curr_buffer_size);
        if (buffer == NULL) {
            fprintf(stderr, "%s\n", strerror(errno));
            close(sockfd);
            exit(1);
        }
        
        // read buffer
        bytes_read = fread(buffer, sizeof(char), curr_buffer_size, read_file);
        if (bytes_read != curr_buffer_size) {
            fprintf(stderr, "%s\n", strerror(errno));
            close(sockfd);
            exit(1);
        }

        // send to server
        bytes_sent = write(sockfd, buffer, bytes_read);
        
        if (bytes_sent != bytes_read) {
            fprintf(stderr, "%s\n", strerror(errno));
            close(sockfd);
            exit(1);
        }
        free(buffer);
        N -= bytes_sent;
    }
    fclose(read_file);
}

/// @brief Get from the server the number of printable characters in the file
/// @param sockfd FD for the socket
void get_printable_count(int sockfd) {
    uint32_t readable_count = 0;
    int bytes_read = 0;
    bytes_read = read(sockfd, &readable_count, sizeof(readable_count));
    
    if (bytes_read != sizeof(readable_count)) {
        fprintf(stderr, "%s\n", strerror(errno));
        close(sockfd);
        exit(1);
    }

    readable_count = ntohl(readable_count);
    printf("%d\n", readable_count);
}


/// @brief Connecting to server
/// @param server_ip The server IP
/// @param server_port The server port
/// @return the socket FD
int connect_to_server(char * server_ip, uint32_t server_port) {
    struct sockaddr_in serv_addr;
    int sockfd = 0, status = -1;

    // setup server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    status = inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);
    if (status <= 0) {
        if (status == 0) {
            fprintf(stderr, "%s\n", strerror(EINVAL));
        } else {
            fprintf(stderr, "%s\n", strerror(errno));
        }
        exit(1);
    }

    // create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        exit(1);
    }

    // connect to server
    status = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
    if(status < 0) {
        fprintf(stderror, "%s\n", strerror(errno));
        exit(1);
    }

    return sockfd;
}

int main(int argc, char** argv) {
    char* server_ip, * file_path;
    uint32_t server_port;
    int sockfd = 0;

    if (argc != 4) {
        fprintf(stderr, "%s\n", strerror(EINVAL));
        exit(1);
    }

    server_ip = argv[1];
    server_port = atoi(argv[2]);
    file_path = argv[3];

    sockfd = connect_to_server(server_ip, server_port);

    send_file(sockfd, file_path);

    get_printable_count(sockfd);
    
    close(sockfd);

    return 0;
}