#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "../../src/m_net/m_net.h"
#include "../../src/file_updown.h"

int main(int argc, char* argv[]) {
    // get args
    if (argc != 3) {
        fprintf(stderr, "Usage: <ip> <port>\n");
        return 1;
    }

    // create listen socket
    int listen_fd = createListenSocket(argv[1], (unsigned short)(atoi(argv[2])));
    if (listen_fd == -1) {
        return 1;
    }

    // set noblock
    if (makeSocketNonBlocking(listen_fd)) {
        close(listen_fd);
        return 1;
    }


    // create epoll
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        close(listen_fd);
        return 1;
    }

    // add listen_fd
    struct epoll_event ee;
    ee.events = EPOLLIN | EPOLLET;
    file_manage_t* fm = (file_manage_t*)malloc(sizeof(file_manage_t));
    bzero((void*)fm, sizeof(file_manage_t));
    fm->sock_type = 1;
    fm->multifd = epfd;
    fm->sockfd = listen_fd;
    size_t max_filename_len = pathconf("/", _PC_NAME_MAX);
    fm->pack_size = (FILE_PACKET_HEAD__SIZE + max_filename_len + 1 + FILEBUF_MAXLEN + 1) * sizeof(char);
    fm->pack = (file_packet_t*)malloc(fm->pack_size);
    bzero((void*)(fm->pack), fm->pack_size);
    ee.data.ptr = (void*)fm;
    if (epoll_ctl(fm->multifd, EPOLL_CTL_ADD, fm->sockfd, &ee)) {
        cleanup(fm);
        return 1;
    }

    // epoll main while
    int event_count;
    struct epoll_event ees[MAX_CLIENTS];
    socklen_t cli_len;
    struct sockaddr_in cli_addr;
    memset((void*)&cli_addr, 0x00, sizeof(struct sockaddr));
    int i, cli_fd;
    file_manage_t* cur_fm;
    while(1) {
        // wait
        event_count = epoll_wait(fm->multifd, ees, MAX_CLIENTS, -1);
        if (event_count == -1) {
            continue;
        }

        // epoll for while
        for (i = 0; i < event_count; i++) {
            cur_fm = (file_manage_t*)(ees[i].data.ptr);
            if (cur_fm->sockfd == listen_fd) {
                // accept
                cli_fd = accept(cur_fm->sockfd, (struct sockaddr*)&cli_addr, &cli_len);
                if (cli_fd == -1) {
                    perror("Error on accept");
                    continue;
                }   

                // set noblock
                if (makeSocketNonBlocking(cli_fd)) {
                    close(cli_fd);
                    continue;
                }

                // add cli_fd
                ee.events = EPOLLIN | EPOLLET;
                file_manage_t* fm = (file_manage_t*)malloc(sizeof(file_manage_t));
                bzero((void*)fm, sizeof(file_manage_t));
                fm->multifd = cur_fm->multifd;
                fm->sockfd = cli_fd;
                fm->pack_size = cur_fm->pack_size;
                fm->pack = cur_fm->pack;
                ee.data.ptr = (void*)fm;
                if (epoll_ctl(fm->multifd, EPOLL_CTL_ADD, fm->sockfd, &ee)) {
                    cleanup(fm);
                    continue;
                }
            } else {
                // file trans
                if (s_file_trans_if(cur_fm)) {
                    cleanup(cur_fm);
                    continue;
                }
            }
        }
    }

    // clean
    cleanup(fm);

    return 0;
}
