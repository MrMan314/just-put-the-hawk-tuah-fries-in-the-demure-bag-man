#include <tcp_threads.h>
#include <tls.h>

void *client_data_thread(void *vargp);
void *proxy_connection(void *vargp);

void* client_thread(void *vargp) {
	int client_sock = *((int *) vargp);

	char *method = malloc(BUF_SIZE), *host = malloc(BUF_SIZE), *buf = malloc(BUF_SIZE), *saveptr = NULL;

	bzero(buf, BUF_SIZE);

	if (recv(client_sock, buf, BUF_SIZE, 0) < 0) {
		fprintf(stderr, "error in recv(): %d (%s)\r\n", errno, strerror(errno));
		goto kys;
	}

	sscanf(buf, "%10s %259s", method, host);
	if (strcasecmp(method, "CONNECT")) {
		bzero(buf, BUF_SIZE);
		strcpy(buf, "HTTP/1.1 405 METHOD NOT ALLOWED\r\n\r\n<h1>Error 405: Method not allowed.</h1>\r\n<p>This proxy only supports CONNECT requests.</p>\r\n");
		int sent = send(client_sock, buf, strlen(buf), 0);
		goto kys;
	}

	char *host_token = strtok_r(host, ":", &saveptr);
	if (host_token == NULL) {
		fprintf(stderr, "error reading host\r\n");
		goto kys;
	}
	char *hostname = host_token, *end_ptr;
	host_token = strtok_r(NULL, ":", &saveptr);
	if (host_token == NULL) {
		fprintf(stderr, "(%s) error reading port\r\n", hostname);
		goto kys;
	}
	int host_port = strtol(host_token, &end_ptr, 10);
	if (end_ptr == host_token || *end_ptr || host_port < 0 || host_port > 65535) {
		fprintf(stderr, "(%s) error parsing port\r\n", hostname);
		goto kys;
	}

	struct addrinfo hints, *res, *result;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int addrinfo = getaddrinfo(hostname, NULL, &hints, &result);

	if (addrinfo < 0) {
		fprintf(stderr, "(%s) error in getaddrinfo(): %d: (%s)\r\n", hostname, addrinfo, strerror(addrinfo));
		goto kys;
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
		rx_sock->header = buf;
		memcpy(tx_sock, rx_sock, sizeof(struct socks_t));
		rx_sock->is_host = 0;
		tx_sock->is_host = 1;

		pthread_create(&tx_thread, NULL, client_data_thread, (void*) tx_sock);
		pthread_create(&rx_thread, NULL, client_data_thread, (void*) rx_sock);

		pthread_mutex_lock(&lock);
		pthread_cond_wait(&death, &lock);
		pthread_mutex_unlock(&lock);

		pthread_cancel(tx_thread);
		pthread_cancel(rx_thread);

		free(rx_sock);
		rx_sock = NULL;
		free(tx_sock);
		tx_sock = NULL;

		break;
	}

	freeaddrinfo(result);
	res = NULL;
	result = NULL;

	printf("(%s) should be killed.\r\n", hostname);
	kys:
		free(buf);
		free(method);
		free(host);
		hostname = NULL;
		host_token = NULL;
		buf = NULL;
		method = NULL;
		host = NULL;
		saveptr = NULL;
		close(host_sock);
		close(client_sock);
		pthread_exit(NULL);
}

void *client_data_thread(void *args) {
	struct socks_t *sock = args;
	char *hostname = sock->hostname;
	struct proxy_socks_t* proxy_socks;
	int rx_sock, tx_sock, proxy_sock;
	pthread_t proxy_thread;
	bool proxy_active = 0;

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

	char *thread_name = sock->is_host ? "host" : "client", *name;

	while (1) {
		len = read(rx_sock, buf, BUF_SIZE);
		if (proxy_active) {
			printf("(%s, %s) killing proxy_thread\r\n", hostname, thread_name);
			pthread_cancel(proxy_thread);
			close(proxy_sock);
			free(proxy_socks);
			proxy_active = 0;
		}
		if (len < 1) {
			goto done;
		}
		if (!sock->is_host && len > 4) {
			TLSRecordHeader *header = (TLSRecordHeader*) buf;
			if (header->content_type == TLS_HANDSHAKE && validate_tls_header(*header)) {
				printf("(%s, %s) %s %s; length: %d\r\n", hostname, thread_name, get_tls_version(header->version), get_tls_content_type(header->content_type), htons(header->length));
				TLSHandshakeRecordHeader *hello_header = (TLSHandshakeRecordHeader*) (buf + sizeof(TLSRecordHeader));
				if (hello_header->handshake_type == TLS_HANDSHAKE_CLIENT_HELLO) {
					TLSHandshakeExtensionRecordHeader *extension_header;
					printf("\thandshake len: %d, ver: %s\r\n", (hello_header->length[0] << 16) + (hello_header->length[1] << 8) + hello_header->length[2], get_tls_version(hello_header->version));
					uint16_t cipher_suites_len = htons(*(uint16_t *)(buf + sizeof(TLSRecordHeader) + sizeof(TLSHandshakeRecordHeader) + hello_header->session_id_length));
					printf("\tcipher suites len: %d\r\n", cipher_suites_len);
					uint8_t compression_methods_len = *(uint16_t *)(buf + sizeof(TLSRecordHeader) + sizeof(TLSHandshakeRecordHeader) + hello_header->session_id_length + cipher_suites_len + 2);
					printf("\tcompression methods len: %x\r\n", compression_methods_len);
					uint16_t extensions_len = htons(*(uint16_t *)(buf + sizeof(TLSRecordHeader) + sizeof(TLSHandshakeRecordHeader) + hello_header->session_id_length + cipher_suites_len + compression_methods_len + 3));
					printf("\textensions len: %d\r\n", extensions_len);
					size_t offset = sizeof(TLSRecordHeader) + sizeof(TLSHandshakeRecordHeader) + hello_header->session_id_length + cipher_suites_len + compression_methods_len + 5;
					while (offset < len && extensions_len) {
						extension_header = (TLSHandshakeExtensionRecordHeader *) (buf + offset);
						printf("\t\textension: type %x, len: %d\r\n", htons(extension_header->type), htons(extension_header->length));
						if (!extension_header->type) {
							name = (char *) (buf + offset + sizeof(TLSHandshakeExtensionRecordHeader) + 5);
							printf("\t\t\tname: %.*s\r\n", htons(extension_header->length) - 5, name);
							break;
						}
						offset += sizeof(TLSHandshakeExtensionRecordHeader) + htons(extension_header->length);
					}

					hello_header = NULL;
					header = NULL;
					name = NULL;
					extension_header = NULL;

					proxy_sock = socket(AF_INET, SOCK_STREAM, 0);
					struct sockaddr_in proxy_addr;

					bzero(&proxy_addr, sizeof(proxy_addr));

					proxy_addr.sin_family = AF_INET;
					proxy_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
					proxy_addr.sin_port = htons(18080);

					if (proxy_sock < 0) {
						goto proxy_failure;
					}

					if (connect(proxy_sock, (struct sockaddr*) &proxy_addr, sizeof(proxy_addr)) < 0) {
						goto proxy_failure;
					}

					printf("(%s, %s) proxy connected\r\n", hostname, thread_name);

					if (send(proxy_sock, sock->header, strlen(sock->header), 0) != strlen(sock->header)) {
						goto proxy_failure;
					}

					char *recv_header = malloc(BUF_SIZE);

					read(proxy_sock, recv_header, BUF_SIZE);
					free(recv_header);
					recv_header = NULL;

					if (send(proxy_sock, buf, len, 0) != len) {
						goto proxy_failure;
					}

					proxy_socks = malloc(sizeof(struct proxy_socks_t));

					proxy_socks->proxy_sock_ptr = &proxy_sock;
					proxy_socks->rx_sock_ptr = &rx_sock;

					pthread_create(&proxy_thread, NULL, proxy_connection, (void *) proxy_socks);
					proxy_active = 1;

//					test_value->type = 0xFFFF;
//					test_value = NULL;

					continue;
//					goto cont;

					proxy_failure:
					printf("(%s, %s) proxy failure\r\n", hostname, thread_name);
				}
			}
		}
		cont:
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

void *proxy_connection(void *args) {
	struct proxy_socks_t *sock = args;
	char *buf = malloc(BUF_SIZE);
	int len, proxy_sock = *(sock->proxy_sock_ptr), rx_sock = *(sock->rx_sock_ptr);

	while (1) {
		len = read(proxy_sock, buf, BUF_SIZE);

		if (len < 0) {
			goto done;
		}

		if (send(rx_sock, buf, len, 0) != len) {
			goto done;
		}
	}

	done:
		free(buf);
		buf = NULL;
		pthread_exit(NULL);

}
