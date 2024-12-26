#include <tls.h>

bool validate_tls_header(TLSRecordHeader header) {
	uint16_t ver = htons(header.version);
	return ver > 0x29f && ver < 0x304 && header.content_type > 0x13 && header.content_type < 0x18;
}

char *get_tls_content_type(uint8_t code) {
	switch (code) {
		case 0x14: return "Change Cipher Spec";
		case 0x15: return "Alert";
		case 0x16: return "Handshake";
		case 0x17: return "Application Data";
		default: return "Invalid";
	}
}

char *get_tls_version(uint16_t code) {
	switch (htons(code)) {
		case 0x300: return "SSL 3.0";
		case 0x301: return "TLS 1.0";
		case 0x302: return "TLS 1.1";
		case 0x303: return "TLS 1.2";
		default: return "Invalid";
	}
}