#define MAX_CLIENTS 1024
#define FILEBUF_MAXLEN 4096

typedef struct {
    int64_t file_size;
    int64_t block_count;
    u_int64_t name_len;
    char data[];
}file_packet_t;
#define FILE_PACKET_HEAD__SIZE 24

typedef struct {
    u_int64_t sock_type;   // 1 == listen_fd 0 == cli_fd
    int multifd;
    int sockfd;
    file_packet_t* pack;
    size_t pack_size;
}file_manage_t;

int s_file_trans_if(file_manage_t* fm);
int c_file_trans_if(int sockfd, const char* trans_type, const char* filepath);
void cleanup(file_manage_t* fm);
