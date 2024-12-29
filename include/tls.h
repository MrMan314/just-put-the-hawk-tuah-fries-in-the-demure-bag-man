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

typedef struct __attribute__((packed)) {
	uint8_t handshake_type;
	uint8_t length[3];
	uint16_t version;
	uint8_t client_random[32];
	uint8_t session_id_length;
} TLSHandshakeRecordHeader;

typedef struct __attribute__((packed)) {
	uint16_t type;
	uint16_t length;
} TLSHandshakeExtensionRecordHeader;

bool validate_tls_header(TLSRecordHeader header);
char *get_tls_version(uint16_t code);
char *get_tls_content_type(uint8_t code);

#define TLS_CHANGE_CIPHER_SPEC 0x14
#define TLS_ALERT 0x15
#define TLS_HANDSHAKE 0x16
#define TLS_APPLICATION_DATA 0x17

#define TLS_HANDSHAKE_CLIENT_HELLO 0x01

#endif
