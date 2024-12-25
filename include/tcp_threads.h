#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define bzero(buf, len) memset(buf, 0, len)
	#define ioctl(s, cmd, argp) ioctlsocket(s, cmd, argp)
	#define close(s) closesocket(s)
#else
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <sys/ioctl.h>
#endif

#ifndef TCP_THREADS_H
#define TCP_THREADS_H

void *client_thread(void *vargp);
void *client_tx_thread(void *vargp);
void *client_rx_thread(void *vargp);

struct socks_t {
	int *client_sock_ptr;
	int *host_sock_ptr;
	char *hostname;
	pthread_cond_t *death;
};

#endif