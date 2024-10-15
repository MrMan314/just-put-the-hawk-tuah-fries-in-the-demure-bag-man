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
	char method[BUF_SIZE], host[BUF_SIZE], buf[BUF_SIZE];

	FILE *fptr;
	fptr = fopen("test.txt", "a");

	ioctl(client_sock, FIONREAD, &len);
	printf("to read: %d\n", len);

	bzero(buf, sizeof(buf));
	int bytes_read = recv(client_sock, buf, sizeof(buf), 0);

	if (bytes_read < 0) {
		goto kys;
	}

	sscanf(buf, "%s %s", method, host);
	printf("im totally connecting to: %s\n", host);
	if (strcasecmp(method, "CONNECT")) {
		bzero(buf, sizeof(buf));
		strcpy(buf, "HTTP/1.1 405 METHOD NOT ALLOWED\r\n\r\nwomp womp\r\n");
		int sent = send(client_sock, buf, strlen(buf), 0);
		goto kys;
	}

	fwrite(buf, bytes_read, 1, fptr);

	printf("bytes read: %d\n", bytes_read);

	ioctl(client_sock, FIONREAD, &len);
	printf("remaining: %d\n", len);
	
	bzero(buf, sizeof(buf));
	strcpy(buf, "HTTP/1.1 200 OK\r\n\r\n");
	int sent = send(client_sock, buf, strlen(buf), 0);

	kys:
		fclose(fptr);
		close(client_sock);
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