#include <tcp_threads.h>

char *ip = "127.0.0.1";
int port = 6500;
int server_sock, new_socket, result;
struct sockaddr_in server_addr, client_addr;

int main() {
	#ifdef _WIN32
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) < 0) {
			printf("Error initializing Winsock: %d %s\r\n", errno, strerror(errno));
		}
	#else
		signal(SIGPIPE, SIG_IGN);
	#endif
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		fprintf(stderr, "Error initializing socket: %d %s\r\n", errno, strerror(errno));
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
		socklen_t client_socklen = sizeof(client_addr);
		new_socket = accept(server_sock, (struct sockaddr*) &client_addr, &client_socklen);
		if (new_socket < 0) {
			printf("error in accept(): %d (%s)\r\n", errno, strerror(errno));
			continue;
		}
		pthread_t thread;
		int *client_socket = malloc(sizeof(int));
		*client_socket = new_socket;
		if (pthread_create(&thread, NULL, client_thread, (void*) client_socket)) {
			printf("error in pthread_create: %d (%s)\r\n", errno, strerror(errno));
			free(client_socket);
			client_socket = NULL;
		}
	}

	close(server_sock);
}

