#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "helpers.h"

#pragma pack(1)

char *strremove(char *str, const char *sub) {
    size_t len = strlen(sub) + 1;
    if (len > 0) {
        char *p = str;
        while ((p = strstr(p, sub)) != NULL) {
            memmove(p, p + len, strlen(p + len) + 1);
        }
    }
    return str;
}

void usage(char *file)
{
	fprintf(stderr, "Usage: %s id server_address server_port\n", file);
	exit(0);
}

int main(int argc, char *argv[])
{
	int sockfd, n, ret;
	struct sockaddr_in serv_addr;
	char buffer[BUFLEN];

	if (argc < 4) {
		usage(argv[0]);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton");

	ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");
	ret = send_message(sockfd, argv[1], strlen(argv[1]));
	DIE(ret < 0, "ID");

	fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);
	FD_SET(0, &read_fds);
	FD_SET(sockfd, &read_fds);
	fdmax = sockfd;

	int flag = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	while (1) {

		tmp_fds = read_fds;
		ret = select(fdmax+1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");

		for(int i = 0; i<=fdmax; i++){
			if(FD_ISSET(i, &tmp_fds)){
				if(i == 0){
					// se citeste de la stdin
					memset(buffer, 0, sizeof(buffer));
					n = read(i, buffer, sizeof(buffer) - 1);
					//n = recv_message(i, buffer);
					DIE(n < 0, "read");

					if (strncmp(buffer, "exit", 4) == 0) {
						close(sockfd);
						FD_CLR(0, &read_fds);
						FD_CLR(sockfd, &read_fds);
						exit(0);
					}

					// se trimite mesaj la server
					if(strncmp(buffer, "subscribe", 9) == 0 || strncmp(buffer, "unsubscribe", 11) == 0){
						//n = send(sockfd, buffer, strlen(buffer), 0);
						n = send_message(sockfd, buffer, strlen(buffer));
						DIE(n < 0, "send");
					}
				}else{
					
					memset(buffer, 0, sizeof(buffer));
					//n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
					//char* result = strremove(buffer, "rcv:");
					n = recv_message(sockfd, buffer);
					DIE(n < 0, "read");

					if(n == 0){
						close(sockfd);
						exit(0);
					}else if(strcmp(buffer, "Subscribed to topic.") == 0 || strcmp(buffer, "Unsubscribed from topic.") == 0) {
						printf("%s\n", buffer);
					}else{
						sent_udp *rcv = (sent_udp*)buffer;
						printData(*rcv);
					}
				}
			}
		}
	}

	close(sockfd);
	free(buffer);
	FD_CLR(0, &read_fds);
	FD_CLR(sockfd, &read_fds);

	return 0;
}
