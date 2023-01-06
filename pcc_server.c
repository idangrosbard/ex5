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

#define PRINT_CHAR_MIN 32
#define PRINT_CHAR_MAX 126
#define MAX_BUFF_LEN 1024 * 1024

struct pcc_total {
    uint32_t char_count[PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1];
}

static struct pcc_total pcc_global;

static uint8_t continue_scan = 1;

void signal_handler(int signum) {
    continue_scan = 0;
}

/// @brief Scanning a buffer for readable characters, and updating the total count
/// @param buff Buffer full of input from the client
/// @param buff_size Size of the buffer (i.e. number of bytes)
/// @return count of total readable characters in the buffer
int scan_buffer(uint8_t* buff, uint32_t buff_size) {
    uint32_t i, readable_count = 0;
    for (i = 0; i < buff_size; i++) {
        if ((PRINT_CHAR_MIN <= buff[i]) && (buff[i] <= PRINT_CHAR_MAX)) {
            readable_count++;
            pcc_global->char_count[buff[i] - PRINT_CHAR_MIN]++;
        }
    }

    return readable_count;
}

/// @brief Scanning the entire input from a client
/// @param connfd FD for the connection with the client
/// @param N Total number of bytes to read from the client
/// @return Total number of printable characters in the input
int scan_input(int connfd, int N) {
    uint32_t buff_size = MAX_BUFF_LEN, bytes_read = 0, total_printable = 0;
    uint8_t read_buff;
    
    // As long as there bytes in the input
    while(N > 0) {
        // Set buffer size
        if (buff_size > N) {
            buff_size = N;
        }
        // Read the buffer
        read_buff = malloc(buff_size);
        bytes_read = read(connfd, &read_buff, buff_size);
        if (bytes_read != buff_size) {
            fprintf(stderr, "%s\n", stderror(errno));
        }
        // Scan the buffer
        total_printable += scan_buffer(read_buff, buff_size);
        N -= bytes_read;
    }
    return total_printable;
}

/// @brief Server loop, for an entire transaction with a client
/// @param listenfd FD for the socket to listen to for connections
/// @return 
void server_loop(int listenfd) {
    int connfd = -1;
    uint32_t N = 0, transaction_bytes = 0, total_printable = 0;
    
    while(continue_scan) {

        // Connect with the client
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        if (connfd < 0) {
            fprintf(stderr, "%s\n", stderror(errno));
            if ((errno != ETIMEDOUT) && (errno != ECONNRESET) && (errno != EPIPE)) {
                exit(1);
            }
        }
        
        // Get N
        transaction_bytes = read(connfd, &N, sizeof(N));
        if (transaction_bytes != sizeof(N)) {
            fprintf(stderr, "%s\n", stderror(errno));
            continue;
        }
        N = ntohl(N);
        
        // Read buffers from the input iteratively
        total_printable = scan_input(connfd, N);

        // Send the total count to the client
        total_printable = htonl(total_printable);
        transaction_bytes = write(connfd, &total_printable, sizeof(total_printable));
        if (transaction_bytes != sizeof(total_printable)) {
            fprintf(stderr, "%s\n", stderror(errno));
            if ((errno != ETIMEDOUT) && (errno != ECONNRESET) && (errno != EPIPE)) {
                exit(1);
            }
        }
        
        close(connfd);
    }
}

int main(int argc, char** argv) {
    uint16_t port;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    if argc != 2 {
        fprintf(stderr, "%s\n", stderror(EINVAL));
        exit(1);
    }

    port = atoi(argv[1]);
    if (port <= 1024) {
        fprintf(stderr, "%s\n", stderror(EINVAL));
        exit(1);
    }

    listenfd = socket(AF_UNIX, SOCK_STREAM, 0);

    // We set SO_REUSEADDR to allow the server to restart without waiting for
    // the port to be released by the OS.
    int enable = 1;
    if (0 != setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable))) {
        fprintf(stderr, "%s\n", stderror(errno));
        exit(1);
    }

    memset( &serv_addr, 0, addrsize );

    serv_addr.sin_family = AF_UNIX;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (0 != bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
        fprintf(stderr, "%s\n", stderror(errno));
        exit(1);
    }

    if (0 != listen(listenfd, 10)) {
        fprintf(stderr, "%s\n", stderror(errno));
        exit(1);
    }

    // handling sigint
    struct sigaction handler = {
        .sa_handler = signal_handler
    };
    sigaction(SIGINT, &handler, NULL);
    
    server_loop(listenfd);
}