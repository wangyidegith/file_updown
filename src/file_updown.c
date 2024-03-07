#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#include "m_net/m_net.h"
#include "file_updown.h"

void cleanup(file_manage_t* fm) {
    if (fm->sock_type == 1) {
        close(fm->multifd);
        free((void*)(fm->pack));
    }
    close(fm->sockfd);
    free((void*)fm);
}

static char* getFileName(char* file_path) {
    char* fn = strrchr(file_path, '/');
    if (fn == NULL) {
        return file_path;
    } else if (strlen(fn) == 1) {
        file_path[(strlen(file_path) - 1) * sizeof(char)] = '\0';
        return getFileName(file_path);
    } else {
        return ++fn;
    }
}

static bool judgeFileExisted(const char* filepath) {
    if (access(filepath, F_OK) == 0) {
        return true;
    } else {
        return false;
    }
}

static int send_file(int sockfd, int filefd, file_packet_t* fpack) {
    // send data
    int bytes_read;
    char* sendbuf = fpack->data + fpack->name_len + 1;
    while ((bytes_read = read(filefd, sendbuf, FILEBUF_MAXLEN)) > 0) {
        if (writen(sockfd, sendbuf, bytes_read) < 0) {
            return -1;
        }
        bzero((void*)sendbuf, FILEBUF_MAXLEN);
    }

    return 0;
}

static int recv_file(int sockfd, int filefd, file_packet_t* fpack) {
    // the third recv file
    size_t last_block_len;
    int bytes_read;
    char* recvbuf = fpack->data + fpack->name_len + 1;
    int64_t i;
    for (i = 1; i <= fpack->block_count; i++) {
        if (i == fpack->block_count) {
            // last_block_len
            last_block_len = fpack->file_size - (fpack->block_count - 1) * FILEBUF_MAXLEN;
            bytes_read = readn(sockfd, recvbuf, last_block_len);
        } else {
            bytes_read = readn(sockfd, recvbuf, FILEBUF_MAXLEN);
        }
        // recv error
        if (bytes_read <= 0) {
            return -1;
        }
        write(filefd, recvbuf, bytes_read);
        bzero((void*)recvbuf, FILEBUF_MAXLEN);
    }

    return 0;
}

int s_file_trans_if(file_manage_t* fm) {
    // pre recvbuf
    bzero((void*)(fm->pack), fm->pack_size);

    // the first recv head
    int bytes_read;
    bytes_read = readn(fm->sockfd, (char*)(fm->pack), FILE_PACKET_HEAD__SIZE);
    // recv error
    if (bytes_read <= 0) {
        return -1;
    }

    // the second recv name
    char* filename = fm->pack->data;
    bytes_read = readn(fm->sockfd, filename, fm->pack->name_len);
    // recv error
    if (bytes_read <= 0) {
        return -1;
    }

    // judge that proto_type
    int filefd;
    if (fm->pack->file_size == -1 && fm->pack->block_count == -1) {
        // get
        // file not existed process
        bool flag;
        if (judgeFileExisted(filename)) {
            // file existed
            // fill fpackh : file_size, block_count, filename
            // get file state
            struct stat file_stat;
            if (stat(filename, &file_stat)) {
                return -1;
            }

            // file_size
            fm->pack->file_size = file_stat.st_size;
            // block_count
            fm->pack->block_count = file_stat.st_size / FILEBUF_MAXLEN + 1;
            // filename and name_len
            flag = true;
        } else {
            // send peer a packet to notify it "The file that you want to download is not existed."
            // file_size
            fm->pack->file_size = -1;
            // block_count
            fm->pack->block_count = -1;
            // name_len
            fm->pack->name_len = 0;
            flag = false;
        }

        // send fpackh
        if (writen(fm->sockfd, (char*)(fm->pack), FILE_PACKET_HEAD__SIZE) <= 0) {
            return -1;
        }

        // send file
        if (flag) {
            // open file
            filefd = open(filename, O_RDONLY, 0700);
            if (filefd == -1) {
                return -1;
            }
            // send file
            if (send_file(fm->sockfd, filefd, fm->pack)) {
                close(filefd);
                return -1;
            }
        }
    } else {
        // put
        // the same name process
        bool judgesame_flag = false;
        int count = 0;
        char* tmp;
        while (1) {
            if (count == 0) {
                if (!judgeFileExisted(filename)) {
                    break;;
                }
            } else {
                if (!judgeFileExisted(tmp)) {
                    break;;
                }
            }
            // 1 cover
            // 2 send peer a packet to notify it "File has existed."
            // 3 filename(1) filename(2) ......
            // at present choose 3
            size_t max_filename_tmp_len = pathconf("/", _PC_NAME_MAX);
            size_t actual_len = (max_filename_tmp_len + 20 + 1) * sizeof(char);
            tmp = (char*)malloc(actual_len);
            bzero((void*)tmp, actual_len);
            snprintf(tmp, actual_len, "%s%d", filename, ++count);
            judgesame_flag = true;
        }

        // open file
        if (judgesame_flag) {
            filename = tmp;
        }
        filefd = open(filename, O_CREAT | O_WRONLY, 0700);
        if (filefd == -1) {
            return -1;
        }
        if (judgesame_flag) {
            free(filename);
        }

        // recv file
        if (recv_file(fm->sockfd, filefd, fm->pack)) {
            close(filefd);
            return -1;
        }
    }

    close(filefd);
    return 0;
}

int c_file_trans_if(int sockfd, const char* trans_type, const char* filepath) {
    // according type to process
    // fill this fm
    // create a fm
    file_manage_t* fm = (file_manage_t*)malloc(sizeof(file_manage_t));
    bzero((void*)fm, sizeof(file_manage_t));
    // fill fm
    fm->sockfd = sockfd;
    size_t max_filename_len = pathconf("/", _PC_NAME_MAX);
    fm->pack_size = (FILE_PACKET_HEAD__SIZE + max_filename_len + 1 + FILEBUF_MAXLEN + 1) * sizeof(char);
    fm->pack = (file_packet_t*)malloc(fm->pack_size);
    bzero((void*)(fm->pack), fm->pack_size);
    int filefd;

    if (!strcmp(trans_type, "up")) {
        // existed
        if (!judgeFileExisted(filepath)) {
            fprintf(stderr, "Err: file is not existed.\n");
            return -1;
        }

        // put

        // fill this fm->pack
        // get file state
        struct stat file_stat;
        if (stat(filepath, &file_stat)) {
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }
        // fill file_size
        fm->pack->file_size = file_stat.st_size;
        // fill block_count
        fm->pack->block_count = file_stat.st_size / FILEBUF_MAXLEN + 1;
        // fill filename and name_len
        char* filepath_tmp = (char*)malloc((strlen(filepath) + 1) * sizeof(char));
        bzero((void*)filepath_tmp, (strlen(filepath) + 1) * sizeof(char));
        memcpy((void*)filepath_tmp, (void*)filepath, strlen(filepath) * sizeof(char));
        char* filename_tmp2 = getFileName(filepath_tmp);
        fm->pack->name_len = strlen(filename_tmp2) * sizeof(char);
        memcpy((void*)(fm->pack->data), (void*)filename_tmp2, fm->pack->name_len);
        free(filepath_tmp);

        // send fpackh
        if (writen(fm->sockfd, (char*)(fm->pack), FILE_PACKET_HEAD__SIZE + fm->pack->name_len) <= 0) {
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // send file
        // open file
        filefd = open(filepath, O_RDONLY, 0700);
        if (filefd == -1) {
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // send file
        if (send_file(fm->sockfd, filefd, fm->pack)) {
            close(filefd);
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // clean
        free((void*)fm->pack);
        free((void*)fm);
    } else if (!strcmp(trans_type, "down")) {
        // get

        // fill this fm->pack
        // fill file_size
        fm->pack->file_size = -1;
        // fill block_count
        fm->pack->block_count = -1;
        // fill filename and name_len
        fm->pack->name_len = strlen(filepath) * sizeof(char);
        memcpy((void*)(fm->pack->data), (void*)filepath, fm->pack->name_len);

        // send fpackh
        if (writen(fm->sockfd, (char*)(fm->pack), FILE_PACKET_HEAD__SIZE + fm->pack->name_len) <= 0) {
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // the first recv head
        int bytes_read;
        bytes_read = readn(fm->sockfd, (char*)(fm->pack), FILE_PACKET_HEAD__SIZE);
        // recv error
        if (bytes_read <= 0) {
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // judge server file whether exist
        if (fm->pack->file_size == -1 && fm->pack->block_count == -1) {
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // get filename
        char* filename_buf = (char*)malloc(max_filename_len + 1);
        bzero((void*)filename_buf, max_filename_len + 1);
        memcpy((void*)filename_buf, (void*)(fm->pack->data), strlen(fm->pack->data) * sizeof(char));
        char* filename = getFileName(filename_buf);
        free(filename_buf);

        // recv file
        // the same name process
        bool judgesame_flag = false;
        int count = 0;
        char* tmp;
        while (1) {
            if (count == 0) {
                if (!judgeFileExisted(filename)) {
                    break;
                }
            } else {
                if (!judgeFileExisted(tmp)) {
                    break;
                }
            }
            // 1 cover
            // 2 send peer a packet to notify it "File has existed."
            // 3 filename(1) filename(2) ......
            // at present choose 3
            size_t max_filename_tmp_len = pathconf("/", _PC_NAME_MAX);
            size_t actual_len = (max_filename_tmp_len + 20 + 1) * sizeof(char);
            tmp = (char*)malloc(actual_len);
            bzero((void*)tmp, actual_len);
            snprintf(tmp, actual_len, "%s%d", filename, ++count);
            judgesame_flag = true;
        }

        // open file
        if (judgesame_flag) {
            filename = tmp;
        }
        filefd = open(filename, O_CREAT | O_WRONLY, 0700);
        if (filefd == -1) {
            free(filename_buf);
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }
        if (judgesame_flag) {
            free(filename);
        }

        // recv file
        if (recv_file(fm->sockfd, filefd, fm->pack)) {
            close(filefd);
            free((void*)fm->pack);
            free((void*)fm);
            return -1;
        }

        // clean
        free((void*)fm->pack);
        free((void*)fm);
    } else {
        fprintf(stderr, "Unknown trans_type.\n");
        return -1;
    }

    close(filefd);

    return 0;
}
