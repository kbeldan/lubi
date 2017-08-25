/*
 * Copyright (c) 2017 Sagemcom
 * Author: karl.beldan@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __LUBI_H__
#define __LUBI_H__

#ifndef CFG_LUBI_PEB_NB_MAX
#define CFG_LUBI_PEB_NB_MAX	128
#endif
#ifndef CFG_LUBI_PEB_SZ_MAX
#define CFG_LUBI_PEB_SZ_MAX	(128 << 10)
#endif

#ifdef CFG_LUBI_DBG
#include <stdio.h>
#define SGR_BRED		"\033[1;31m"
#define SGR_BGRN		"\033[1;32m"
#define SGR_BBLU		"\033[1;34m"
#define SGR_BRST 		"\033[1m"
#define SGR_LRED		"\033[31m"
#define SGR_LGRN		"\033[32m"
#define SGR_LBLU		"\033[34m"
#define SGR_RST 		"\033[0m"
#define DBG(str, ...)		do {				\
		fprintf(stderr, str SGR_RST, ## __VA_ARGS__);	\
	} while (0)
#else
#define DBG(...)		do {} while (0)
#endif

#ifdef CFG_LUBI_INT_CRC32
#define CRCPOLY_LE 0xEDB88320
extern int crc32_le_init(uint32_t poly);
extern uint32_t crc32_le(uint32_t crc, const uint8_t *p, size_t len, uint32_t poly);

#define crc32_init()		crc32_le_init(CRCPOLY_LE)
#define crc32(buf, len)		crc32_le(~0, (const uint8_t *)(buf), len, CRCPOLY_LE)
#endif

#endif /* !__LUBI_H__ */
