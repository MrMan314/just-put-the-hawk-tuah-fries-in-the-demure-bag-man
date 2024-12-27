#include <tcp_threads.h>

char *ip = "127.0.0.1";
int port = 6500;
int server_sock, new_socket;
struct sockaddr_in server_addr, client_addr;

int main(int argc, char *argv[]) {
	if (argc != 3 && argc != 1) {
		fprintf(stderr, "Usage: %s [ip] [port]\r\n", argv[0]);
		return 1;
	} if (argc == 3) {
		ip = argv[1];
		port = atoi(argv[2]);
	}

	#ifdef _WIN32
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) < 0) {
			fprintf(stderr, "Error initializing Winsock: %d (%s)\r\n", errno, strerror(errno));
		}
	#else
		signal(SIGPIPE, SIG_IGN);
	#endif

	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		fprintf(stderr, "Error initializing socket: %d (%s)\r\n", errno, strerror(errno));
		return errno;
	}

	const char enable = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		fprintf(stderr, "Error setting SO_REUSEADDR: %d (%s)\r\n", errno, strerror(errno));
		return errno;
	}

	struct addrinfo hints, *res, *result;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int addrinfo = getaddrinfo(ip, NULL, &hints, &result);

	if (addrinfo < 0) {
		fprintf(stderr, "error in getaddrinfo(): %d: (%s)\r\n", addrinfo, strerror(addrinfo));
		return errno;
	}

	res = result;

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);

	char *addrstr = malloc(BUF_SIZE);

	while (res) {
		inet_ntop(res->ai_family, &((struct sockaddr_in *) res->ai_addr)->sin_addr, addrstr, BUF_SIZE);
		server_addr.sin_addr.s_addr = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;

		printf("Listening on %s:%d\r\n", addrstr, port);
		free(addrstr);
		addrstr = NULL;
		if (bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
			fprintf(stderr, "Bind error: %d (%s)\r\n", errno, strerror(errno));
			res = res->ai_next;
			continue;
//			return errno;
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
		return 0;
	}
	fprintf(stderr, "Could not listen on any sockets.\r\n");
	return -1;
}
