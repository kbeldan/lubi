/*
 * Copyright (c) 2017 Sagemcom
 * Author: karl.beldan@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __LIBLUBI_H__
#define __LIBLUBI_H__

enum {
	FLASH_STATUS_BAD,
	FLASH_STATUS_GOOD
};

struct lubi_args {
	void *priv;
	int (*flash_read)(void *priv, void *dst, int pnum, int offset, int len);
	int peb_sz;
	int peb_min;
	int peb_nb;
};

int lubi_read_svol(void *priv, void *buf, int vol_id, unsigned int max_lnum);
int lubi_list_vols(const void *priv);
int lubi_get_vol_id(const void *priv, const char *name, int *upd_marker);
int lubi_attach(void *priv);
int lubi_mem_sz(void);
int lubi_init(void *priv, const struct lubi_args *args);

#endif /* !__LIBLUBI_H__ */
