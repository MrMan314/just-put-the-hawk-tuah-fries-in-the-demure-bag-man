#include <tcp_threads.h>
#include <tls.h>

#define BUF_SIZE 1024

void* client_thread(void *vargp) {
	int client_sock = *((int *) vargp);

	long unsigned int len = 0;
	char method[BUF_SIZE], host[BUF_SIZE], buf[BUF_SIZE];

	ioctl(client_sock, FIONREAD, &len);

	bzero(buf, sizeof(buf));
	int bytes_read = recv(client_sock, buf, sizeof(buf), 0);

	if (bytes_read < 0) {
		fprintf(stderr, "error reading bytes: %d (%s)\r\n", errno, strerror(errno));
		goto kys;
	}

	sscanf(buf, "%10s %259s", method, host);
	if (strcasecmp(method, "CONNECT")) {
		fprintf(stderr, "invalid request: %s\r\n", buf);
		bzero(buf, sizeof(buf));
		strcpy(buf, "HTTP/1.1 405 METHOD NOT ALLOWED\r\n\r\n<h1>Error 405: Method not allowed.</h1>\r\n<p>This proxy only supports CONNECT requests.</p>\r\n");
		int sent = send(client_sock, buf, strlen(buf), 0);
		goto kys;
	}

	char *host_token = strtok(host, ":");
	if (host_token == NULL) {
		fprintf(stderr, "error reading host\r\n");
		goto pre_hostname_kys;
	}
	char *hostname = host_token, *end_ptr;
	strcpy(hostname, host_token);
	host_token = strtok(NULL, ":");
	if (host_token == NULL) {
		fprintf(stderr, "error reading port\r\n");
		goto post_hostname_kys;
	}
	int host_port = strtol(host_token, &end_ptr, 10);
	if (end_ptr == host_token || *end_ptr || host_port < 0 || host_port > 65535) {
		fprintf(stderr, "error parsing port\r\n");
		goto post_hostname_kys;
	}

	struct addrinfo hints, *res, *result;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int err_code = getaddrinfo(hostname, NULL, &hints, &result);
	if (err_code < 0) {
		fprintf(stderr, "(%s) error in getaddrinfo(): %d: %s\r\n", hostname, err_code, strerror(err_code));
		goto post_hostname_kys;
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

		bzero(&host_addr, sizeof(host_addr));

		host_addr.sin_family = AF_INET;
		host_addr.sin_addr.s_addr = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;
		host_addr.sin_port = htons(host_port);

		host_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (host_sock < 0) {
			fprintf(stderr, "(%s) Error initializing server socket.\r\n", hostname);
			break;
		}

		err_code = connect(host_sock, (struct sockaddr*) &host_addr, sizeof(host_addr));
		if (err_code < 0) {
			fprintf(stderr, "(%s) error connecting: %d: %s\r\n", hostname, err_code, strerror(err_code));
			res = res ->ai_next;
			continue;
		}
		printf("(%s) connected!!🪤\r\n", hostname);

		pthread_cond_t death = PTHREAD_COND_INITIALIZER;
		pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

		struct socks_t sock;
		sock.client_sock_ptr = (int *) vargp;
		sock.host_sock_ptr = &host_sock;
		sock.hostname = hostname;
		sock.death = &death;

		pthread_create(&tx_thread, NULL, client_tx_thread, (void*) &sock);
		pthread_create(&rx_thread, NULL, client_rx_thread, (void*) &sock);

		pthread_mutex_lock(&lock);
		pthread_cond_wait(&death, &lock);
		pthread_mutex_unlock(&lock);

		break;
	}

	pthread_cancel(tx_thread);
	pthread_cancel(rx_thread);
	printf("(%s) should be killed.\r\n", hostname);
	post_hostname_kys:
		hostname = NULL;
		free(hostname);
	pre_hostname_kys:
		host_token = NULL;
		free(host_token);
	kys:
		printf("im killing myself\r\n");
		freeaddrinfo(result);
		res = NULL;
		result = NULL;
		close(host_sock);
		close(client_sock);
		pthread_exit(NULL);
		printf("this should not print vro\r\n");
}

void *client_tx_thread(void *args) {
	struct socks_t *sock = args;
	char *hostname = sock->hostname;
	printf("(%s, host) in my connecting era\r\n", hostname);

	int host_sock = *(sock->host_sock_ptr), client_sock = *(sock->client_sock_ptr);

	long unsigned int len = 0, sent = 0;
	char *buf = malloc(BUF_SIZE);
	bzero(buf, sizeof(buf));
	strcpy(buf, "HTTP/1.1 200 OK\r\n\r\n");
	sent = send(client_sock, buf, strlen(buf), 0);
	printf("(%s, host) in my connecting era 2\r\n", hostname);
	while (1) {
		len = read(host_sock, buf, BUF_SIZE);
		if (len < 1) {
			printf("(%s, host) recv: %d (%s)\r\n", hostname, errno, strerror(errno));
			goto tx_done;
		}
		if (len > 4) {
			TLSRecordHeader *header = (TLSRecordHeader*) buf;
			printf("(%s, host) %s %s; length: %d\r\n", hostname, get_tls_version(header->version), get_tls_content_type(header->content_type), htons(header->length));
			header = NULL;
		}
		sent = send(client_sock, buf, len, 0);
		if (sent != len) {
			goto tx_done;
		}
	}
	tx_done:
		free(buf);
		buf = NULL;
		printf("(%s, host) should be killed.\r\n", hostname);
		pthread_cond_signal(sock->death);
		pthread_exit(NULL);
		printf("(%s, host) should not print.\r\n", hostname);

}

void *client_rx_thread(void *args) {
	struct socks_t *sock = args;
	char *hostname = sock->hostname;
	char sni_found = 0;
	printf("(%s, client) in my connecting era\r\n", hostname);

	int host_sock = *(sock->host_sock_ptr), client_sock = *(sock->client_sock_ptr);

	long unsigned int len = 0, sent = 0;
	char *buf = malloc(BUF_SIZE);
	printf("(%s, client) in my connecting era 2\r\n", hostname);
	while (1) {
		len = read(client_sock, buf, BUF_SIZE);
		if (len < 1) {
			printf("(%s, client) recv: %d (%s)\r\n", hostname, errno, strerror(errno));
			goto rx_done;
		}
		if (len > 4) {
			TLSRecordHeader *header = (TLSRecordHeader*) buf;
			printf("(%s, client) %s %s; length: %d\r\n", hostname, get_tls_version(header->version), get_tls_content_type(header->content_type), htons(header->length));
			header = NULL;
		}
		sent = send(host_sock, buf, len, 0);
		if (sent != len) {
			goto rx_done;
		}
	}
	rx_done:
		free(buf);
		buf = NULL;
		printf("(%s, client) should be killed.\r\n", hostname);
		pthread_cond_signal(sock->death);
		pthread_exit(NULL);
		printf("(%s, client) should not print.\r\n", hostname);
}
