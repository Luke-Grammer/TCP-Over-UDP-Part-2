// Checksum.cpp
// CSCE 463-500
// Luke Grammer
// 11/12/19

#include "pch.h"

Checksum::Checksum()
{
	// set up a lookup table for later use
	for (DWORD i = 0; i < 256; i++)
	{
		DWORD c = i;
		for (int j = 0; j < 8; j++) {
			c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
		}

		crc_table[i] = c;
	}
}

DWORD Checksum::CRC32(UCHAR* buf, size_t len)
{
	DWORD c = 0xFFFFFFFF;
	for (size_t i = 0; i < len; i++)
		c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);

	return c ^ 0xFFFFFFFF;
}