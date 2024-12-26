#include <stdint.h>
#include <stdbool.h>
#ifdef WIN32
	#include <winsock.h>
#else
	#include <arpa/inet.h>
#endif

#ifndef TLS_H
#define TLS_H

typedef struct __attribute__((packed)) {
	uint8_t content_type;
	uint16_t version;
	uint16_t length;
} TLSRecordHeader;

bool validate_tls_header(TLSRecordHeader header);
char *get_tls_version(uint16_t code);
char *get_tls_content_type(uint8_t code);

#endif