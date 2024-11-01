#include <stdio.h>
#include <stdlib.h>
// #include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#ifdef _WIN32
	#include <winsock2.h>
//	#include <windows.h>
	#include <ws2tcpip.h>
	#define bzero(buf, len) memset(buf, 0, len)
	#define ioctl(s, cmd, argp) ioctlsocket(s, cmd, argp)
	#define close(s) closesocket(s)
#else
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <sys/ioctl.h>
#endif

#define BUF_SIZE 1024

void *client_thread(void *vargp);
void *client_tx_thread(void *vargp);
void *client_rx_thread(void *vargp);

char *ip = "127.0.0.1";
int port = 65000;
int server_sock, result;
struct sockaddr_in server_addr, client_addr;

struct socks_t {
	int *client_sock_ptr;
	int *host_sock_ptr;
	pthread_t tx_thread;
	pthread_t rx_thread;
};

int main() {
	#ifdef _WIN32
		WSADATA wsa;
		result = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (result < 0) {
			printf("Error initializing Winsock: %d %s\r\n", result, strerror(result));
		}
	#else
		signal(SIGPIPE, SIG_IGN);
	#endif
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		fprintf(stderr, "Error initializing socket: %d %s\r\n", server_sock, strerror(server_sock));
		return 1;
	}

	const char enable = 1;
	result = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	if (result < 0) {
		fprintf(stderr, "Error setting SO_REUSEADDR: %d %s\r\n", result, strerror(result));
		return 1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(ip);

	result = bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr));
	if (result < 0) {
		printf("%d\n", port);
		fprintf(stderr, "Bind error: %d %s\r\n", result, strerror(result));
		return 1;
	}

	listen(server_sock, 5);

	while (1) {
		int client_socket;
		socklen_t client_socklen = sizeof(client_addr);
		client_socket = accept(server_sock, (struct sockaddr*) &client_addr, &client_socklen);
		if (client_socket < 0) {
			printf("%s\n", strerror(client_socket));
			continue;
		}
		printf("new spawn\r\n");
		pthread_t thread;
		pthread_create(&thread, NULL, client_thread, (void*) &client_socket);
	}

	close(server_sock);
}

void* client_thread(void *vargp) {
	int client_sock = *((int *) vargp);

	long unsigned int len = 0;
	char method[BUF_SIZE], host[BUF_SIZE], buf[BUF_SIZE];

	ioctl(client_sock, FIONREAD, &len);
	printf("to read: %lu\r\n", len);

	bzero(buf, sizeof(buf));
	int bytes_read = recv(client_sock, buf, sizeof(buf), 0);

	if (bytes_read < 0) {
		fprintf(stderr, "error reading bytes\r\n");
		goto kys;
	}

	sscanf(buf, "%10s %259s", method, host);
	if (strcasecmp(method, "CONNECT")) {
		bzero(buf, sizeof(buf));
		strcpy(buf, "HTTP/1.1 405 METHOD NOT ALLOWED\r\n\r\n<h1>Error 405: Method not allowed.</h1>\r\n<p>This proxy only supports CONNECT requests.</p>\r\n");
		int sent = send(client_sock, buf, strlen(buf), 0);
		fprintf(stderr, "invalid request\r\n");
		goto kys;
	}

	printf("im totally connecting to: %s\r\n", host);

	char *host_token = strtok(host, ":");
	if (host_token == NULL) {
		fprintf(stderr, "error reading host\r\n");
		goto kys;
	}
	char *hostname = host_token, *end_ptr;
	strcpy(hostname, host_token);
	host_token = strtok(NULL, ":");
	if (host_token == NULL) {
		fprintf(stderr, "error reading port\r\n");
		goto kys;
	}
	int host_port = strtol(host_token, &end_ptr, 10);
	if (end_ptr == host_token || *end_ptr || host_port < 0 || host_port > 65535) {
		fprintf(stderr, "error parsing port\r\n");
		goto kys;
	}

	struct addrinfo hints, *res, *result;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int err_code = getaddrinfo(hostname, NULL, &hints, &result);
	if (err_code < 0) {
		fprintf(stderr, "error in getaddrinfo(): %d: %s\r\n", err_code, strerror(err_code));
		goto kys;
	}

	res = result;

	char addrstr[BUF_SIZE];
	void *ptr;
	pthread_t rx_thread, tx_thread;
	struct sockaddr_in host_addr;
	int host_sock;


	while (res) {
		inet_ntop(res->ai_family, res->ai_addr->sa_data, addrstr, BUF_SIZE);
		ptr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
		inet_ntop(res->ai_family, ptr, addrstr, BUF_SIZE);
		printf("%s: %s\n", hostname, addrstr);

		bzero(&host_addr, sizeof(host_addr));

		host_addr.sin_family = AF_INET;
		host_addr.sin_addr.s_addr = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;
		host_addr.sin_port = htons(host_port);

		host_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (host_sock < 0) {
			fprintf(stderr, "Error initializing server socket.\r\n");
			break;
		}

		err_code = connect(host_sock, (struct sockaddr*) &host_addr, sizeof(host_addr));
		if (err_code < 0) {
			fprintf(stderr, "error connecting: %d: %s\r\n", err_code, strerror(err_code));
			res = res ->ai_next;
			continue;
		}
		printf("connected!!🪤\r\n");

		struct socks_t sock;
		sock.client_sock_ptr = (int *) vargp;
		sock.host_sock_ptr = &host_sock;
		sock.tx_thread = tx_thread;
		sock.rx_thread = rx_thread;

		pthread_create(&tx_thread, NULL, client_tx_thread, (void*) &sock);
		pthread_create(&rx_thread, NULL, client_rx_thread, (void*) &sock);

		pthread_join(tx_thread, NULL);
		pthread_join(rx_thread, NULL);

		break;
	}

	kys:
		printf("im killing myself\r\n");
		freeaddrinfo(result);
		res = NULL;
		result = NULL;
//		free(hostname);
		hostname = NULL;
		close(host_sock);
		close(client_sock);
		pthread_exit(NULL);
		printf("im killing myself 2\r\n");
}

void *client_tx_thread(void *args) {
	printf("in my connecting era 1.5\r\n");
	struct socks_t *sock = args;

	int host_sock = *(sock->host_sock_ptr), client_sock = *(sock->client_sock_ptr);

	pthread_t other_thread = sock->rx_thread;

	long unsigned int len = 0, sent = 0;
	char buf[BUF_SIZE];
	bzero(buf, sizeof(buf));
	strcpy(buf, "HTTP/1.1 200 OK\r\n\r\n");
	sent = send(client_sock, buf, strlen(buf), 0);
	printf("in my connecting era 2\r\n");
	while (1) {
		len = read(host_sock, buf, BUF_SIZE);
		if (len < 1) {
			printf("(host) recv: %d (%s)\r\n", errno, strerror(errno));
			pthread_cancel(other_thread);
			pthread_exit(NULL);
		}
		sent = send(client_sock, buf, len, 0);
		if (sent != len) {
			pthread_cancel(other_thread);
			pthread_exit(NULL);
		}
	}
}

void *client_rx_thread(void *args) {
	printf("in my connecting era 1.5\r\n");
	struct socks_t *sock = args;

	int host_sock = *(sock->host_sock_ptr), client_sock = *(sock->client_sock_ptr);

	pthread_t other_thread = sock->tx_thread;

	long unsigned int len = 0, sent = 0;
	char buf[BUF_SIZE];
	printf("in my connecting era 2\r\n");
	while (1) {
		len = read(client_sock, buf, BUF_SIZE);
		if (len < 1) {
			printf("(client) recv: %d (%s)\r\n", errno, strerror(errno));
			pthread_cancel(other_thread);
			pthread_exit(NULL);
		}
		sent = send(host_sock, buf, len, 0);
		if (sent != len) {
			pthread_cancel(other_thread);
			pthread_exit(NULL);
		}
	}
}

