#include <tcp_threads.h>
#include <tls.h>

void *client_data_thread(void *vargp);

void* client_thread(void *vargp) {
	int client_sock = *((int *) vargp);

	char method[BUF_SIZE], host[BUF_SIZE], buf[BUF_SIZE];

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

	int addrinfo = getaddrinfo(hostname, NULL, &hints, &result);

	if (addrinfo < 0) {
		fprintf(stderr, "(%s) error in getaddrinfo(): %d: (%s)\r\n", hostname, addrinfo, strerror(addrinfo));
		goto post_hostname_kys;
	}

	res = result;

	pthread_t rx_thread, tx_thread;
	struct sockaddr_in host_addr;
	int host_sock;


	while (res) {
		bzero(&host_addr, sizeof(host_addr));

		host_addr.sin_family = AF_INET;
		host_addr.sin_addr.s_addr = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;
		host_addr.sin_port = htons(host_port);

		host_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (host_sock < 0) {
			fprintf(stderr, "(%s) Error initializing server socket: %d (%s)\r\n", hostname, errno, strerror(errno));
			break;
		}

		if (connect(host_sock, (struct sockaddr*) &host_addr, sizeof(host_addr)) < 0) {
			fprintf(stderr, "(%s) error connecting: %d: %s\r\n", hostname, errno, strerror(errno));
			res = res->ai_next;
			continue;
		}
		printf("(%s) connected\r\n", hostname);

		pthread_cond_t death = PTHREAD_COND_INITIALIZER;
		pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

		struct socks_t *rx_sock = malloc(sizeof(struct socks_t)), *tx_sock = malloc(sizeof(struct socks_t));
		rx_sock->client_sock_ptr = (int *) vargp;
		rx_sock->host_sock_ptr = &host_sock;
		rx_sock->hostname = hostname;
		rx_sock->death = &death;
		memcpy(tx_sock, rx_sock, sizeof(struct socks_t));
		rx_sock->is_host = 0;
		tx_sock->is_host = 1;

		pthread_create(&tx_thread, NULL, client_data_thread, (void*) tx_sock);
		pthread_create(&rx_thread, NULL, client_data_thread, (void*) rx_sock);

		pthread_mutex_lock(&lock);
		pthread_cond_wait(&death, &lock);
		pthread_mutex_unlock(&lock);

		free(rx_sock);
		rx_sock = NULL;
		free(tx_sock);
		tx_sock = NULL;

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
		freeaddrinfo(result);
		res = NULL;
		result = NULL;
		close(host_sock);
		close(client_sock);
		pthread_exit(NULL);
}

void *client_data_thread(void *args) {
	struct socks_t *sock = args;
	char *hostname = sock->hostname;
	int rx_sock, tx_sock;

	if (sock->is_host) {
		rx_sock = *(sock->host_sock_ptr), tx_sock = *(sock->client_sock_ptr);
	} else {
		tx_sock = *(sock->host_sock_ptr), rx_sock = *(sock->client_sock_ptr);
	}

	long unsigned int len = 0, sent = 0;
	char *buf = malloc(BUF_SIZE);

	if (sock->is_host) {
		bzero(buf, sizeof(buf));
		strcpy(buf, "HTTP/1.1 200 OK\r\n\r\n");
		sent = send(tx_sock, buf, strlen(buf), 0);
	}

	char *thread_name = sock->is_host ? "host" : "client";

	while (1) {
		len = read(rx_sock, buf, BUF_SIZE);
		if (len < 1) {
			printf("(%s, %s) error in recv(): %d (%s)\r\n", hostname, thread_name, errno, strerror(errno));
			goto done;
		}
		if (len > 4) {
			TLSRecordHeader *header = (TLSRecordHeader*) buf;
			if (validate_tls_header(*header)) {
				printf("(%s, %s) %s %s; length: %d\r\n", hostname, thread_name, get_tls_version(header->version), get_tls_content_type(header->content_type), htons(header->length));
			}
			header = NULL;
		}
		sent = send(tx_sock, buf, len, 0);
		if (sent != len) {
			goto done;
		}
	}
	done:
		free(buf);
		buf = NULL;
		printf("(%s, %s) should be killed.\r\n", hostname, thread_name);
		pthread_cond_signal(sock->death);
		pthread_exit(NULL);

}
