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

static uint32_t pcc_total[PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1];

static uint8_t continue_scan = 1;

void signal_handler(int signum) {
    continue_scan = 0;
}

/// @brief Scanning a buffer for readable characters, and updating the pcc_session parameters
/// @param buff Buffer full of input from the client
/// @param buff_size Size of the buffer (i.e. number of bytes)
/// @param pcc_session Array of counters for the current session
void scan_buffer(uint8_t* buff, uint32_t buff_size, uint32_t* pcc_session) {
    uint32_t i, readable_count = 0;
    for (i = 0; i < buff_size; i++) {
        if ((PRINT_CHAR_MIN <= buff[i]) && (buff[i] <= PRINT_CHAR_MAX)) {
            readable_count++;
            pcc_session[buff[i] - PRINT_CHAR_MIN]++;
        }
    }
}

/// @brief Scanning the entire input from a client
/// @param connfd FD for the connection with the client
/// @param N Total number of bytes to read from the client
/// @return Pointer to the array of counters for the current session. On error, returns NULL
uint32_t * scan_input(int connfd, int N) {
    uint32_t buff_size = MAX_BUFF_LEN, bytes_read = 0, total_printable = 0, i;
    uint32_t * pcc_session;
    uint8_t * read_buff;
    
    pcc_session = malloc(sizeof(uint32_t) * (PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1));
    if (pcc_session == NULL) {
        fprintf(stderr, "%s\n", stderror(errno));
        exit(1);
    }
    memset(pcc_session, 0, sizeof(uint32_t) * (PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1));

    // As long as there bytes in the input
    while(N > 0) {
        // Set buffer size
        if (buff_size > N) {
            buff_size = N;
        }
        // Read the buffer
        read_buff = malloc(buff_size);
        if (read_buff == NULL) {
            fprintf(stderr, "%s\n", stderror(errno));
            free(pcc_session);
            exit(1);
        }
        bytes_read = read(connfd, read_buff, buff_size);
        if (bytes_read != buff_size) {
            // If there was a TCP error we skip the current transaction
            fprintf(stderr, "%s\n", stderror(errno));
            free(read_buff);
            free(pcc_session);

            return NULL;
        }
        // Scan the buffer
        scan_buffer(read_buff, buff_size);
        free(read_buff);
        N -= bytes_read;
    }

    return pcc_session;
}

/// @brief Server loop, for an entire transaction with a client
/// @param listenfd FD for the socket to listen to for connections
void server_loop(int listenfd) {
    int connfd = -1;
    uint32_t N = 0, transaction_bytes = 0, total_printable = 0, i;
    uint32_t * pcc_session;
    
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
            // If there was a TCP error we skip the current transaction
            fprintf(stderr, "%s\n", stderror(errno));
            continue;
        }
        N = ntohl(N);
        
        // Read buffers from the input iteratively
        pcc_session = scan_input(connfd, N);

        if (pcc_session == NULL) {
            // If there was a TCP error we skip the current transaction
            continue;
        }
        // Update the total count
        for (i = 0; i < (PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1); i++) {
            total_printable += pcc_session[i];
        }

        // Send the total count to the client
        total_printable = htonl(total_printable);
        transaction_bytes = write(connfd, &total_printable, sizeof(total_printable));
        if (transaction_bytes != sizeof(total_printable)) {
            fprintf(stderr, "%s\n", stderror(errno));
            free(pcc_session);
            if ((errno != ETIMEDOUT) && (errno != ECONNRESET) && (errno != EPIPE)) {
                exit(1);
            }
        } else {
            // If everything went well, update the global count
            for (i = 0; i < (PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1); i++) {
                pcc_total[i] += pcc_session[i];
            }
            free(pcc_session);
        }

        close(connfd);
    }
}

/// @brief Print the total count of characters
void print_exit() {
    uint32_t i;
    for (i = 0; i < (PRINT_CHAR_MAX - PRINT_CHAR_MIN + 1); i++) {
        printf("char '%c' : %u times\n", i + PRINT_CHAR_MIN, pcc_total[i]);
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

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    // We set SO_REUSEADDR to allow the server to restart without waiting for
    // the port to be released by the OS.
    int enable = 1;
    if (0 != setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable))) {
        fprintf(stderr, "%s\n", stderror(errno));
        exit(1);
    }

    memset( &serv_addr, 0, addrsize );

    serv_addr.sin_family = AF_INET;
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

    memset(pcc_total, 0, sizeof(pcc_total));
    
    server_loop(listenfd);

    print_exit();
    
    return 0;
}