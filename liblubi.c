/*
 * Copyright (c) 2017 Sagemcom
 * Author: karl.beldan@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <asm/byteorder.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liblubi_cfg.h"
#include "ubi-media.h"
#include "liblubi.h"

#define DBG_FUNC_ENTRY() DBG(SGR_LGRN ">>> %s\n", __func__)

struct peb_rec {
	struct ubi_ec_hdr ehdr;
	struct ubi_vid_hdr vhdr;
	uint8_t vhdr_crc_ok;
	uint8_t unused[3];
};

struct leb2peb {
	uint8_t dcrc_ok;
	uint8_t unused;
	uint16_t peb;
};

struct lubi_priv {
	// user args
	void *ext_priv;
	flash_read_fn_t ext_flash_read;
	int peb_sz;
	int peb_nb;
	int peb_min;

	// Zeroed by scan {
	// scan dyn params
	char scan_mem_start[0];
	uint32_t leb_sz;
	uint32_t vhdr_offs;
	uint32_t data_offs;
	int vtbl_slots;

	uint8_t vtbls_buf[2 * CFG_LUBI_PEB_SZ_MAX];
	struct ubi_vtbl_record *vtbl_recs;

	struct peb_rec pebs[CFG_LUBI_PEB_NB_MAX];
	char scan_mem_end[0];
	// }

	// scratch mem
	struct leb2peb scratch_leb2pebs[CFG_LUBI_PEB_NB_MAX];
	uint8_t scratch_leb[CFG_LUBI_PEB_SZ_MAX];
};

/**
 *
 */
static int flash_read(struct lubi_priv *lubi, void *dst, int pnum, int offset,
		      int len)
{
	return lubi->ext_flash_read(lubi->ext_priv, dst, pnum, offset, len);
}

/**
 * Gets the dynamics offsets from the valid EC headers
 * 	from the 1st one if vhdr_offs == 0
 * 	else from the 1st one which vid_hdr_offset == vhdr_offs
 */
static int lubi_scan_ecs(struct lubi_priv *lubi, uint32_t vhdr_offs)
{
	DBG_FUNC_ENTRY();

	for (int i = 0; i < lubi->peb_nb; i++) {
		struct peb_rec *peb = &lubi->pebs[i];
		struct ubi_ec_hdr *ehdr = &peb->ehdr;

		flash_read(lubi, ehdr, lubi->peb_min + i, 0, sizeof(struct ubi_ec_hdr));

		if (ehdr->magic == __be32_to_cpu(UBI_EC_HDR_MAGIC) &&
		    crc32(ehdr, UBI_EC_HDR_SIZE_CRC) == __be32_to_cpu(ehdr->hdr_crc)) {
			uint32_t voffs = __be32_to_cpu(ehdr->vid_hdr_offset);

			if (vhdr_offs && vhdr_offs != voffs)
				continue;

			lubi->vhdr_offs = voffs;
			lubi->data_offs = __be32_to_cpu(ehdr->data_offset);

			return 0;
		}
	}
	return -1;
}

/**
 *
 */
static int lubi_scan_vids(struct lubi_priv *lubi)
{
	DBG_FUNC_ENTRY();

	for (int i = 0; i < lubi->peb_nb; i++) {
		struct peb_rec *peb = &lubi->pebs[i];
		struct ubi_vid_hdr *vhdr = &peb->vhdr;

		flash_read(lubi, vhdr, lubi->peb_min + i, lubi->vhdr_offs,
			   sizeof(struct ubi_vid_hdr));

		if (vhdr->magic != __be32_to_cpu(UBI_VID_HDR_MAGIC) ||
		    crc32(vhdr, UBI_VID_HDR_SIZE_CRC) != __be32_to_cpu(vhdr->hdr_crc))
			continue;

		peb->vhdr_crc_ok = 1;

		DBG("%s:%3d: PEB %3d @ %08x: vol_id %8X lnum %5d sqnum %5lld\n",
		    __func__, __LINE__, lubi->peb_min + i,
		    (lubi->peb_min + i) * lubi->peb_sz,
		    __be32_to_cpu(vhdr->vol_id), __be32_to_cpu(vhdr->lnum),
		    (long long)__be64_to_cpu(vhdr->sqnum));
	}
	return 0;
}

/**
 *
 */
static int check_vtbl(const struct lubi_priv *lubi,
		      const struct ubi_vtbl_record *recs)
{
	for (int i = 0; i < lubi->vtbl_slots; i++) {
		if (crc32(&recs[i], UBI_VTBL_RECORD_SIZE_CRC) !=
		    __be32_to_cpu(recs[i].crc))
			return -1;
	}
	return 0;
}

/**
 *
 */
int lubi_read_svol(void *priv, void *buf, int vol_id, unsigned int max_lnum)
{
	struct lubi_priv *lubi = priv;
	struct leb2peb *leb2pebs = lubi->scratch_leb2pebs;
	int usable_leb_sz;
	int ret_len = 0, lebs_ok = 0, used_ebs = -1;
	int is_lvl, dcrc_ok;

	DBG_FUNC_ENTRY();

	is_lvl = vol_id == UBI_LAYOUT_VOLUME_ID;

	if (is_lvl)
		usable_leb_sz = lubi->leb_sz;
	else if (!lubi->vtbl_recs ||
		  lubi->vtbl_recs[vol_id].vol_type != UBI_VID_STATIC)
			return -1;
	else
		usable_leb_sz = lubi->leb_sz -
				__be32_to_cpu(lubi->vtbl_recs[vol_id].data_pad);

	if (max_lnum > CFG_LUBI_PEB_NB_MAX - 1)
		max_lnum = CFG_LUBI_PEB_NB_MAX - 1;

	memset(leb2pebs, 0, (max_lnum + 1) * sizeof(leb2pebs[0]));

	for (int i = 0; i < lubi->peb_nb; i++) {
		struct peb_rec *peb = &lubi->pebs[i];
		struct ubi_vid_hdr *prev_vhdr = NULL, *vhdr = &peb->vhdr;
		uint32_t lnum, len, prev_len;
		struct leb2peb *l2p;
		void *buf_dst, *rd_dst;

		if (!peb->vhdr_crc_ok ||
		    vhdr->vol_id != __cpu_to_be32(vol_id))
			continue;

		lnum = __be32_to_cpu(vhdr->lnum);
		if (lnum > max_lnum)
			continue;

		l2p = &leb2pebs[lnum];
		buf_dst = (uint8_t *)buf + lnum * usable_leb_sz;
		// The layout volume is special-cased because of the way :(
		// Linux-UBI handles its data_{crc,size} when restoring it
		//
		// Linux-UBI uses the LEB size to compute vtbl_slots
		//
		// To improve any diagnostic and ease ret_len computation we
		// could check that in case of data and unless we are parsing
		// the latest LEB, data_size is usable_leb_sz
		len = is_lvl ? lubi->vtbl_slots * UBI_VTBL_RECORD_SIZE :
			       __be32_to_cpu(vhdr->data_size);
		if (!l2p->dcrc_ok) {
			rd_dst = buf_dst;
		} else {
			prev_vhdr = &lubi->pebs[l2p->peb].vhdr;

			if (__be64_to_cpu(vhdr->sqnum) < __be64_to_cpu(prev_vhdr->sqnum))
				continue;

			rd_dst = lubi->scratch_leb;
			// clobber the buffer
			memset(rd_dst + len - len / 8, 0x5A, len / 8);
		}

		flash_read(lubi, rd_dst, lubi->peb_min + i, lubi->data_offs,
			   len);

		if (is_lvl)
			dcrc_ok = !check_vtbl(lubi, rd_dst);
		else
			dcrc_ok = crc32(rd_dst, len) == __be32_to_cpu(vhdr->data_crc);

		if (dcrc_ok) {
			l2p->peb = i;
			if (!l2p->dcrc_ok) {
				l2p->dcrc_ok = 1;
				lebs_ok++;
			} else {
				prev_len = is_lvl ?
					   len : __be32_to_cpu(prev_vhdr->data_size);

				memmove(buf_dst, rd_dst, len);
				ret_len -= prev_len;
			}
			ret_len += len;
			// Pick used_ebs from the last PEB with data crc ok
			used_ebs = __be32_to_cpu(vhdr->used_ebs);
		}
	}

	DBG(SGR_BRST "%s: Volume \"%s\"\n\tEBs used / ok: %d / %d\n\tread %d bytes\n",
	    __func__, is_lvl ? NULL : lubi->vtbl_recs[vol_id].name, used_ebs,
	    lebs_ok, ret_len);

	if (ret_len && lebs_ok == used_ebs) {
		int expected;
		int last_peb = leb2pebs[used_ebs - 1].peb;

		if (lebs_ok)
			expected = usable_leb_sz * (used_ebs - 1) +
				   __be32_to_cpu(lubi->pebs[last_peb].vhdr.data_size);
		else
			expected = 0;
		if (expected != ret_len) {
			DBG(SGR_BRED "%s: Expected %d bytes - read %d\n",
			    __func__, expected, ret_len);
			return -1;
		}
	} else {
		// Do not return an error in case we could get 1 LEB from the LVL
		if (!is_lvl || lebs_ok < 1) {
			DBG(SGR_BRED "%s: Volume read failure (read %d bytes)\n",
			    __func__, ret_len);
			return -1;
		}
	}

	return ret_len;
}

/**
 *
 */
int lubi_list_vols(const void *priv)
{
	const struct lubi_priv *lubi = priv;
	struct ubi_vtbl_record *recs = lubi->vtbl_recs;

	DBG_FUNC_ENTRY();

	if (!recs)
		return -1;

	for (int i = 0; i < lubi->vtbl_slots; i++)
		if (recs[i].name_len)
			DBG(SGR_BRST "\tfound %40s: upd_marker:%d type:%s\n",
			    recs[i].name, recs[i].upd_marker,
			    recs[i].vol_type == UBI_VID_STATIC ?
			    "static" : "dynamic");

	return 0;
}

/**
 *
 */
int lubi_get_vol_id(const void *priv, const char *name, int *upd_marker)
{
	const struct lubi_priv *lubi = priv;
	struct ubi_vtbl_record *recs = lubi->vtbl_recs;
	size_t len;

	DBG_FUNC_ENTRY();

	if (!recs)
		return -1;

	len = strlen(name); // strnlen(name, UBI_VOL_NAME_MAX + 1) if available

	if (!len || len > UBI_VOL_NAME_MAX)
		return -1;

	len = __cpu_to_be16(len);
	for (int i = 0; i < lubi->vtbl_slots; i++) {
#if 0 // ATM liblubi doesn't accept damaged LVLs so the following can't happen
		if (crc32(&recs[i], UBI_VTBL_RECORD_SIZE_CRC) !=
		    __be32_to_cpu(recs[i].crc)) {
			DBG(SGR_BRED "%s: Bad VTBL rec\n", __func__);
			continue;
		}
#endif
		if (len == recs[i].name_len &&
		    !strcmp((const char *)recs[i].name, name)) {
			*upd_marker = recs[i].upd_marker;
			return i;
		}
	}

	return -1;
}

/**
 *
 */
int lubi_attach(void *priv, uint32_t vhdr_offs, uint32_t data_offs)
{
	struct lubi_priv *lubi = priv;
	struct leb2peb *leb2pebs = lubi->scratch_leb2pebs;

	DBG_FUNC_ENTRY();

	memset(lubi->scan_mem_start, 0,
	       __builtin_offsetof(struct lubi_priv, scan_mem_end) -
	       __builtin_offsetof(struct lubi_priv , scan_mem_start));

	if (!vhdr_offs || !data_offs) {
		// if vhdr_offs == 0, data_offs is not used
		if (lubi_scan_ecs(lubi, vhdr_offs) < 0)
			return -1;
	} else {
		lubi->vhdr_offs = vhdr_offs;
		lubi->data_offs = data_offs;
	}
	lubi->leb_sz = lubi->peb_sz - lubi->data_offs;
	lubi->vtbl_slots = lubi->leb_sz / UBI_VTBL_RECORD_SIZE;
	if (lubi->vtbl_slots > UBI_MAX_VOLUMES)
		lubi->vtbl_slots = UBI_MAX_VOLUMES;

	if (lubi_scan_vids(lubi))
		return -1;

	if (lubi_read_svol(lubi, lubi->vtbls_buf, UBI_LAYOUT_VOLUME_ID, 1) < 0)
		return -1;

	for (int i = 0; i < 2; i++)
		DBG("LVL: LEB[%1d] -> PEB[%3d] - data crc: %s\n",
		    i, lubi->peb_min + leb2pebs[i].peb,
		    leb2pebs[i].dcrc_ok ? "good" : "bad");

	if (leb2pebs[0].dcrc_ok)
		lubi->vtbl_recs = (void *)&lubi->vtbls_buf[0];
	else if (leb2pebs[1].dcrc_ok)
		lubi->vtbl_recs = (void *)&lubi->vtbls_buf[lubi->leb_sz];
	else
		return -1;

	return 0;
}

/**
 *
 */
int lubi_mem_sz(void)
{
	return sizeof(struct lubi_priv);
}

/**
 *
 */
int lubi_init(void *priv, void *ext_priv, flash_read_fn_t flash_read,
	      int peb_sz, int peb_min, int peb_nb)
{
	struct lubi_priv *lubi = priv;

	DBG_FUNC_ENTRY();

	lubi->ext_priv = ext_priv;
	lubi->ext_flash_read = flash_read;
	lubi->peb_sz = peb_sz;
	lubi->peb_min = peb_min;
	lubi->peb_nb = peb_nb;

	if (lubi->peb_nb > CFG_LUBI_PEB_NB_MAX) {
		DBG("peb_nb arg = %d > %d\n", lubi->peb_nb, CFG_LUBI_PEB_NB_MAX);
		return -1;
	}
	if (lubi->peb_sz > CFG_LUBI_PEB_SZ_MAX) {
		DBG("peb_nb arg = %d > %d\n", lubi->peb_sz, CFG_LUBI_PEB_SZ_MAX);
		return -1;
	}

	return 0;
}
