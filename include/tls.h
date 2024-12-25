#include <stdint.h>
#include <arpa/inet.h>

#ifndef TLS_H
#define TLS_H

typedef struct __attribute__((packed)) {
	uint8_t content_type;
	uint16_t version;
	uint16_t length;
} TLSRecordHeader;

char *get_tls_version(uint16_t code);
char *get_tls_content_type(uint8_t code);

#endif