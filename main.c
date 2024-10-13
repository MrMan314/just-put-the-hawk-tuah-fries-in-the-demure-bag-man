#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define BUF_SIZE 1024

char *ip = "127.0.0.1";
int port = 65000;
int server_sock, result;
struct sockaddr_in server_addr, client_addr;

void* client_thread(void *vargp) {
	int *client_sock_ptr;
	client_sock_ptr = (int *) vargp;
	int client_sock = *client_sock_ptr;

	int len = 0;

	FILE *fptr;
	fptr = fopen("test.txt", "a");

	ioctl(client_sock, FIONREAD, &len);
	printf("to read: %d\n", len);

	do {
		char buf[BUF_SIZE];
		bzero(buf, sizeof(buf));
		int bytes_read = recv(client_sock, buf, sizeof(buf), 0);

		fwrite(buf, bytes_read, 1, fptr);

		printf("bytes read: %d\n", bytes_read);

		ioctl(client_sock, FIONREAD, &len);
		printf("remaining: %d\n", len);
	} while (len > 0);

	fclose(fptr);
	close(client_sock);

/*
	char buf[1024];
	bzero(buf, 1024);
	recv(client_sock, buf, sizeof(buf), 0);
	printf("%s\n", buf);

	bzero(buf, 1024);
	strcpy(buf, "HTTP/1.1 200 OK\r\n\r\n");
	send(client_sock, buf, strlen(buf), 0);

	bzero(buf, 1024);
	recv(client_sock, buf, sizeof(buf), 0);

	FILE *fptr;
	fptr = fopen("test", "w");
	fwrite(buf, sizeof(buf), 1, fptr);
	fclose(fptr);

	close(client_sock);
*/
}

int main() {
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		fprintf(stderr, "Error initializing socket.\r\n");
		return 1;
	}

	const int enable = 1;
	result = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	if (result < 0) {
		fprintf(stderr, "Error setting SO_REUSEADDR.\r\n");
		return 1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(ip);

	result = bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr));
	if (result < 0) {
		printf("%d\n", port);
		fprintf(stderr, "Bind error\r\n");
		return 1;
	}

	listen(server_sock, 5);


	while (1) {
		int client_socket;
		socklen_t client_socklen = sizeof(client_addr);
		client_socket = accept(server_sock, (struct sockaddr*) &client_addr, &client_socklen);

		pthread_t thread;
		pthread_create(&thread, NULL, client_thread, (void*) &client_socket);
	}

	close(server_sock);
}