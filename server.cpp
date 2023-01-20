#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helpers.h"
#include <string>
#include <cstdlib>
#include <map>
#include <iostream>

#pragma pack(1)

using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}

int main(int argc, char *argv[])
{
	int sockTCP, sockUDP, auxfd, portno;
	char* buffer = (char*)calloc(BUFLEN, sizeof(char));
	char* auxbuffer = (char*)calloc(BUFLEN, sizeof(char));
	struct sockaddr_in serv_addr, cli_addr;
	int n, i, ret;
	map<string, client> users;
	map<int, string> fdtoid;
	map<string, int> idtofd;
	socklen_t clilen;
	fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

	if (argc < 2) {
		usage(argv[0]);
	}

	int flag = 1;
	setsockopt(sockTCP, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

	// se goleste multimea de descriptori de citire (read_fds) si multimea temporara (tmp_fds)
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	sockTCP = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockTCP < 0, "socket");
	sockUDP = socket(AF_INET, SOCK_DGRAM, 0);
	DIE(sockUDP < 0, "socket");

	portno = atoi(argv[1]);
	DIE(portno == 0, "atoi");

	memset((char *) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portno);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(sockTCP, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
	DIE(ret < 0, "bind");
	ret = bind(sockUDP, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
	DIE(ret < 0, "bind");

	ret = listen(sockTCP, MAX_CLIENTS);
	DIE(ret < 0, "listen");


	// se adauga noul file descriptor (socketul pe care se asculta conexiuni) in multimea read_fds
	FD_SET(sockTCP, &read_fds);
	FD_SET(sockUDP, &read_fds);
	FD_SET(0, &read_fds);

	fdmax = (sockTCP > sockUDP)? sockTCP : sockUDP;
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	while (1) {
		tmp_fds = read_fds; 
		
		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");

		for (i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &tmp_fds)) {
				if (i == sockTCP) {
					// a venit o cerere de conexiune pe socketul inactiv (cel cu listen),
					// pe care serverul o accepta
					clilen = sizeof(cli_addr);
					auxfd = accept(sockTCP, (struct sockaddr *) &cli_addr, &clilen);
					DIE(auxfd < 0, "accept");
					memset(buffer, 0, BUFLEN);
					n = recv_message(auxfd, buffer);
					DIE(n < 0, "id");
					// se adauga noul socket intors de accept() la multimea descriptorilor de citire
					FD_SET(auxfd, &read_fds);
					if (auxfd > fdmax) { 
						fdmax = auxfd;
					}
					if(users.count(buffer) == 0){
						client newclient = newClient(inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
						newclient.online = true;
						users.insert(pair<string, client>(buffer, newclient));
						setsockopt(auxfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
					}else{
						if(users[buffer].online){
							// conexiunea se inchide
							printf("Client %s already connected.\n", buffer);
							close(auxfd);
							// se scoate din multimea de citire socketul inchis 
							FD_CLR(auxfd, &read_fds);
							continue;
						}else{
							updateClient(users, idtofd, buffer, inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
							for(std::map<string, topic>::iterator it = users[buffer].topics.begin(); it != users[buffer].topics.end(); it++){
								if((!it->second.notifications.empty()) && it->second.SF == 1){
									while(!it->second.notifications.empty()){
										ret = send_message(auxfd, (char*)&(it->second.notifications.front()), sizeof(sent_udp));
										DIE(ret < 0, "send");
										it->second.notifications.erase(it->second.notifications.begin());
									}
								}
							}
							setsockopt(auxfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
						}
					}
					printf("New client %s connected from %s:%d.\n", buffer, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
					fdtoid.insert(pair<int, string>(auxfd, buffer));
					idtofd.insert(pair<string, int>(buffer, auxfd));
					continue;

				}else if (i == sockUDP) {
					memset(buffer, 0, BUFLEN);
					clilen = sizeof(cli_addr);
					n = recvfrom(i, buffer, BUFLEN, 0, (struct sockaddr*)&cli_addr, &clilen);
					DIE(n < 0, "recv");
					processUDP(users, idtofd, buffer, cli_addr);
				}else if (i == 0){
					// se citeste de la stdin
					memset(buffer, 0, sizeof(buffer));
					n = read(i, buffer, sizeof(buffer) - 1);
					//n = recv_message(i, buffer);
					DIE(n < 0, "read");

					if (strncmp(buffer, "exit", 4) == 0) {
						close(sockTCP);
						close(sockUDP);
						for(map<int, string>::iterator it = fdtoid.begin(); it != fdtoid.end(); it++){
							close(it->first);
							FD_CLR(it->first, &read_fds);
						}
						exit(0);
					}
				}else{
					// s-au primit date pe unul din socketii de client,
					// asa ca serverul trebuie sa le receptioneze
					memset(buffer, 0, BUFLEN);
					//n = recv(i, buffer, sizeof(buffer) - 1, 0);
					n = recv_message(i, buffer);
					DIE(n < 0, "recv");

					if (n == 0) {
						// conexiunea s-a inchis
						printf("Client %s disconnected.\n", fdtoid[i].c_str());
						users[fdtoid[i]].online = false;
						idtofd.erase(fdtoid[i]);
						fdtoid.erase(i);
						close(i);
						// se scoate din multimea de citire socketul inchis 
						FD_CLR(i, &read_fds);
						continue;
					}else{
						string topic;
						int SF;
						int param = 0;
						char *token = strtok(buffer, " \n");
						while(token!=NULL){
							if(param == 1){
								topic = token;
							}
							if(param == 2){
								SF = atoi(token);
							}
							param++;
							token = strtok(NULL, " \n");
						}
						if(strncmp(buffer, "subscribe", 9) == 0 && (SF == 0 || SF == 1) && param == 3){
							if(users[fdtoid[i]].topics.count(topic) == 0){
								users[fdtoid[i]].topics.emplace(topic, newTopic(SF));
								n = send_message(i, (char*)"Subscribed to topic.", strlen("Subscribed to topic."));
								DIE(n < 0, "send");
							}else{
								users[fdtoid[i]].topics[topic].SF = SF;
							}
						}
						if(strncmp(buffer, "unsubscribe", 11) == 0 && users[fdtoid[i]].topics.count(topic) > 0){
							users[fdtoid[i]].topics.erase(topic);
							n = send_message(i, (char*)"Unsubscribed from topic.", strlen("Unsubscribed from topic."));
							DIE(n < 0, "send");
						}
					}
				}
			}
		}
	}

	close(sockTCP);
	close(sockUDP);
	close(auxfd);
	users.clear();
	idtofd.clear();
	fdtoid.clear();
	free(auxbuffer);
	free(buffer);
	for(map<int, string>::iterator it = fdtoid.begin(); it != fdtoid.end(); it++){
							close(it->first);
							FD_CLR(it->first, &read_fds);
						}
	return 0;
}
