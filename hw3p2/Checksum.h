// Checksum.h
// CSCE 463-500
// Luke Grammer
// 11/12/19

#pragma once

class Checksum 
{
	DWORD crc_table[256] = { 0 };

public:
	Checksum();

	DWORD CRC32(UCHAR* buf, size_t len);
};