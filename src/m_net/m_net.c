#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <time.h>

#include "m_net.h"


int createUdpSocket() {
	int sock_fd;
	sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd < 0) {
		perror("socket(udp)");
		return -1;
	}
	return sock_fd;
}

int createUdpBindSocket(const char* ip, int port) {
	int sock_fd;
	struct sockaddr_in local_addr;
	sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd < 0) {
		perror("socket(udp local)");
		return -1;
	}
	memset((void*)(&local_addr), 0x00, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	if (ip == NULL) {
		local_addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		if (inet_pton(AF_INET, ip, &(local_addr.sin_addr)) <= 0) {
			perror("inet_pton");
			return -1;
		}
	}
	local_addr.sin_port = htons(port);
	int reuse = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
		perror("setsockopt addr");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
		perror("setsockopt port");
		return -1;
	}
	if (bind(sock_fd, (const struct sockaddr *)(&local_addr), sizeof(local_addr)) < 0) {
		perror("bind(udp)");
		return -1;
	}
	return sock_fd;
}

int createUdpBroadcastSocket() {
	int sock_fd;
	sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd < 0) {
		perror("socket(udp broadcast remote)");
		return -1;
	}
	int optval = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST | SO_REUSEADDR, &optval, sizeof(int)) == -1) {
		perror("setsockopt(SO_BROADCAST)");
		return -1;
	}
	return sock_fd;
}

int getLocalIPv4WithMaskAddresses(char ips[][2 * IPV4STRSIZE], int* count) {
	struct ifaddrs *ifaddr, *ifa;
	int family, s;
	// get netif list
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		return -1;
	}
	int i = 0;
	char ip[INET_ADDRSTRLEN];
	memset((void*)ip, 0x00, sizeof(ip));
	char mask[INET_ADDRSTRLEN] = "\0";
	memset((void*)mask, 0x00, sizeof(mask));
	for (ifa = ifaddr; ifa != NULL && i < MAX_LOCAL_IP_COUNT; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		family = ifa->ifa_addr->sa_family;
		// only IPv4
		if (family == AF_INET) {
			s = getnameinfo(ifa->ifa_addr,
					sizeof(struct sockaddr_in),
					ip, INET_ADDRSTRLEN,
					NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				fprintf(stderr, "getnameinfo() failed: %s\n", gai_strerror(s));
				return -1;
			}
			s = getnameinfo(ifa->ifa_netmask,
					sizeof(struct sockaddr_in),
					mask, INET_ADDRSTRLEN,
					NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				fprintf(stderr, "getnameinfo() failed: %s\n", gai_strerror(s));
				return -1;
			}
			size_t ip_len = strlen(ip);
			memcpy((void*)ips[i], (void*)ip, ip_len);
			ips[i][ip_len] = '/';
			size_t mask_len = strlen(mask);
			memcpy((void*)(ips[i] + ip_len + 1), (void*)mask, mask_len);
			i++;
		}
	}
	*count = i;
	freeifaddrs(ifaddr);
	return 0;
}

uint32_t ipv4Str2Uint(const char* ip) {
	int ip_bin = 0;
	int value = 0;
	int shift = 24;
	int i;
	for (i = 0; ip[i] != '\0'; i++) {
		if (ip[i] == '.') {
			ip_bin |= value << shift;
			value = 0;
			shift -= 8;
		} else if (ip[i] >= '0' && ip[i] <= '9') {
			value = value * 10 + (ip[i] - '0');
		} else {
			fprintf(stderr, "Invalid IP address format.\n");
			return -1;
		}
	}
	ip_bin |= value << shift;
	return ip_bin;
}

int getBrdcAddr(char* broadcast_ip, const char* ip, const char* subnetmask) {
	uint32_t ip_bin = ipv4Str2Uint(ip);
	if (ip_bin == (uint32_t)-1) {
		return -1;
	}
	uint32_t mask_bin = ipv4Str2Uint(subnetmask);
	if (mask_bin == (uint32_t)-1) {
		return -1;
	}
	uint32_t reverse_mask_bin = ~mask_bin;
	uint32_t broadcast_ip_bin = ip_bin | reverse_mask_bin;
	sprintf(broadcast_ip, "%d.%d.%d.%d", (broadcast_ip_bin >> 24) & 255, (broadcast_ip_bin >> 16) & 255, (broadcast_ip_bin >> 8) & 255, broadcast_ip_bin & 255);
	return 0;
}

int getIPFromDomain(const char* domain, char* ip, int max_len) {
	struct addrinfo hints, *res, *p;
	int status;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if ((status = getaddrinfo(domain, NULL, &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return -1;
	}
	void* addr;
	p = res;
	if (p != NULL) {
		if (p->ai_family == AF_INET) {
			if (max_len < INET_ADDRSTRLEN) {
				fprintf(stderr, "Error : ip address buf length too small.\n");
				return -1;
			}
			struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
			addr = &(ipv4->sin_addr);
		} else {
			if (max_len < INET6_ADDRSTRLEN) {
				fprintf(stderr, "Error : ip address buf length too small.\n");
				return -1;
			}
			struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
			addr = &(ipv6->sin6_addr);
		}
		inet_ntop(p->ai_family, addr, ip, max_len);
	} else {
		return -1;
	}
	freeaddrinfo(res);
	return 0;
}

int createListenSocket(const char* ip, unsigned short port) {
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket(tcp)");
		return -1;
	}
	struct sockaddr_in addr;
	memset((void*)&addr, 0x00, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	if (ip == NULL) {
		addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		if (inet_pton(AF_INET, ip, &(addr.sin_addr)) <= 0) {
			perror("inet_pton");
			return -1;
		}
	}
	addr.sin_port = htons(port);
	// option :
	int reuse = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
		perror("setsockopt addr");
		return -1;
	}
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
		perror("setsockopt port");
		return -1;
	}

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
		perror("bind(tcp)");
		return -1;
	}
	if (listen(listen_fd, LISTEN_QUEUE) < 0) {
		perror("listen");
		return -1;
	}
	return listen_fd;
}

int createConnectSocket(const char* server_ip, int server_port) {
    int conn_fd;
    struct sockaddr_in server_addr;
    if ((conn_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &(server_addr.sin_addr)) <= 0) {
        perror("inet_pton");
        return -1;
    }
    if (connect(conn_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        return -1;
    }
    return conn_fd;
}

int makeSocketNonBlocking(int sockfd) {
	int flags, s;
	flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl(F_GETFL)");
		return -1;
	}
	flags |= O_NONBLOCK;
	s = fcntl(sockfd, F_SETFL, flags);
	if (s == -1) {
		perror("fcntl(F_SETFL)");
		return -1;
	}
	return 0;
}

unsigned short getOneRandomPort() {
	srand(time(NULL));
	int port = rand() % (65536) + 1024;
	return port;
}

ssize_t readn(const int sock_fd, char* buffer, const size_t n) {
	ssize_t nLeft, nread, offset;
	nLeft = n;
	offset = 0;
	while(nLeft > 0) {
		if ((nread = recv(sock_fd, buffer + offset, nLeft, 0)) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				continue;
			} else {
				return nread;
			}
		}
		else if (nread == 0) {
			return nread;
		}
		offset += nread;
		nLeft -= nread;
	}
	return n;
}

ssize_t writen(const int sock_fd, char* buffer, const size_t n) {
	int nLeft, nwrite, offset;
	nLeft = n;
	offset = 0;
	while(nLeft > 0) {
		if ((nwrite = write(sock_fd, buffer + offset, nLeft)) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				continue;
			} else {
				return nwrite;
			}
		}
		offset += nwrite;
		nLeft -= nwrite;
	}
	return n;
}

int getPeerIpAndPortFromSocket(int peer_socket, char* peer_ip, int* peer_port) {
	struct sockaddr_in peer_addr;
	socklen_t addr_len = sizeof(peer_addr);
	if (getpeername(peer_socket, (struct sockaddr *)&peer_addr, &addr_len) == -1) {
		perror("getpeername");
		return -1;
	}
	if (inet_ntop(AF_INET, (void*)&(peer_addr.sin_addr), peer_ip, INET_ADDRSTRLEN) == NULL) {
		perror("inet_ntop failed");
		return -1;
	}
	peer_ip[INET_ADDRSTRLEN - 1] = '\0';
	*peer_port = ntohs(peer_addr.sin_port);
	return 0;
}

int getSelfIPFromSocket(int sock_fd, char* ip) {
	struct sockaddr_in cli_addr;
	socklen_t len;
	if (getsockname(sock_fd, (struct sockaddr *)&cli_addr, &len) == -1) {
		perror("getsockname failed");
		return -1;
	}
	if (inet_ntop(AF_INET, (void*)&(cli_addr.sin_addr), ip, INET_ADDRSTRLEN) == NULL) {
		perror("inet_ntop failed");
		return -1;
	}
	return 0;
}

unsigned short getSelfPortFromSocket(int sock_fd) {
	struct sockaddr_in cli_addr;
	socklen_t len;
	if (getsockname(sock_fd, (struct sockaddr *)&cli_addr, &len) == -1) {
		perror("getsockname failed");
		return -1;
	}
	return ntohs(cli_addr.sin_port);
}
