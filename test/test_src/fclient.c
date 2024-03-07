#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "../../src/m_net/m_net.h"
#include "../../src/file_updown.h"

int main(int argc, char* argv[]) {
    // get args
    if (argc != 5) {
        fprintf(stderr, "Usage: <server_ip> <server_port> <trans_type> <file_path>\n");   // trans_type : up or down
        return 1;
    }

    // create socket
    int sockfd = createConnectSocket(argv[1], (unsigned short)(atoi(argv[2])));
    if (sockfd < 0) {
        return 1;
    }

    // set noblock
    if (makeSocketNonBlocking(sockfd)) {
        close(sockfd);
        return 1;
    }

    // file trans
    if (c_file_trans_if(sockfd, argv[3], argv[4])) {
        close(sockfd);
        return -1;
    }

    // close socket
    close(sockfd);

    return 0;
}
