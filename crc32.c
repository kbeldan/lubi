/*
 * crc_le(crc, buf, len, poly) -> crc:32,bitrev(poly),crc,true,true,0
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <stdlib.h>
#include <stdint.h>

#ifdef CFG_LUBI_INT_CRC32_TBL
static uint32_t crc32_le_tbl[256];

uint32_t crc32_le_init(uint32_t poly)
{
	for (int i = 0; i < 256; i++) {
		uint32_t crc = i;
		for (int j = 0; j < 8; j++)
			crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
		crc32_le_tbl[i] = crc;
	}

	return 0;
}
#else
uint32_t crc32_le_init(__attribute__((unused)) uint32_t poly)
{
	return 0;
}
#endif

uint32_t crc32_le(uint32_t crc, const uint8_t *p, size_t len,
		  __attribute__((unused)) uint32_t poly)
{
	for (unsigned int i = 0; i < len; i++) {
#ifdef CFG_LUBI_INT_CRC32_TBL
		crc = crc32_le_tbl[(crc & 0xff) ^ *p++] ^ (crc >> 8);
#else
		crc ^= *p++;
		for (int j = 0; j < 8; j++)
			crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
#endif
	}

	return crc;
}
