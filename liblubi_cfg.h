/*
 * Copyright (c) 2017 Sagemcom
 * Author: karl.beldan@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __LUBI_H__
#define __LUBI_H__

#ifdef __UBOOT__
#include <common.h>

#define CFG_LUBI_PEB_NB_MAX	CONFIG_SPL_LUBI_PEB_NB_MAX
#define CFG_LUBI_PEB_SZ_MAX	CONFIG_SPL_LUBI_PEB_SZ_MAX
#define CFG_LUBI_INT_CRC32
#define CFG_LUBI_USE_LVL	CONFIG_SPL_LUBI_USE_LVL
#ifdef CONFIG_SPL_LUBI_DBG
#define CFG_LUBI_DBG
#endif

#ifdef CFG_LUBI_DBG
#include <asm/global_data.h>
DECLARE_GLOBAL_DATA_PTR;

#define fprintf(s, ...)		do { if (!(gd->flags & GD_FLG_SILENT)) printf(__VA_ARGS__); } while (0)
#endif

#else

#ifndef CFG_LUBI_PEB_NB_MAX
#define CFG_LUBI_PEB_NB_MAX	128
#endif
#ifndef CFG_LUBI_PEB_SZ_MAX
#define CFG_LUBI_PEB_SZ_MAX	(128 << 10)
#endif
#ifndef CFG_LUBI_USE_LVL
#define CFG_LUBI_USE_LVL	1
#endif
#endif // __UBOOT__

#ifdef CFG_LUBI_DBG
#ifndef __UBOOT__
#include <stdio.h>
#endif
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
#ifndef __UBOOT__
#define CRCPOLY_LE		0xEDB88320
#define crc32(buf, len)		crc32_le(UBI_CRC32_INIT, (const uint8_t *)(buf), len, CRCPOLY_LE)
extern uint32_t crc32_le(uint32_t crc, const uint8_t *p, size_t len, uint32_t poly);
#else
#include <u-boot/crc.h>
#define crc32(buf, len)		crc32_no_comp(~0, (unsigned char const *)(buf), len)
#endif
#endif

#endif /* !__LUBI_H__ */
