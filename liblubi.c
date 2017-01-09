/*
 * Copyright (c) 2017 Sagemcom
 * Author: karl.beldan@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#define  _DEFAULT_SOURCE
#include <endian.h>
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
	int vhdr_crc_ok;
};

struct leb2peb {
	uint8_t valid;
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

	// scan dyn params
	uint32_t leb_sz;
	uint32_t vhdr_offs;
	uint32_t data_offs;

	struct peb_rec pebs[CFG_LUBI_PEB_NB_MAX];
	struct leb2peb scratch_leb2pebs[CFG_LUBI_PEB_NB_MAX];
	uint8_t scratch_leb[CFG_LUBI_PEB_SZ_MAX];

	uint8_t vtbls_buf[2 * CFG_LUBI_PEB_SZ_MAX];
	struct ubi_vtbl_record *vtbl_recs;
	int vtbl_slots;
};

enum {
	PEB_HAS_EHDR_MAGIC,
	PEB_HAS_EHDR_CRC_OK,
	PEB_HAS_VHDR_MAGIC,
	PEB_HAS_VHDR_CRC_OK,
	PEB_HAS_CNT
};

static const char __attribute__((unused)) *peb_has_strs[PEB_HAS_CNT] = {
	[PEB_HAS_EHDR_MAGIC]	= "EC  magic ",
	[PEB_HAS_EHDR_CRC_OK]	= "EC  crc ok",
	[PEB_HAS_VHDR_MAGIC]	= "VID magic ",
	[PEB_HAS_VHDR_CRC_OK]	= "VID crc ok",
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
 *
 */
static int lubi_scan(struct lubi_priv *lubi)
{
	// These will be opt. out if DBG is not enabled
	int peb_stats[PEB_HAS_CNT] = { 0, };
	uint64_t min_ec = __UINT64_MAX__, mean_ec = 0, max_ec = 0;
	int dynparams_set = 0;

	DBG_FUNC_ENTRY();

	for (int i = lubi->peb_min; i < lubi->peb_min + lubi->peb_nb; i++) {
		struct peb_rec *peb = &lubi->pebs[i];
		struct ubi_ec_hdr *ehdr = &peb->ehdr;
		struct ubi_vid_hdr *vhdr = &peb->vhdr;
		uint64_t ec;

		flash_read(lubi, ehdr, i, 0, sizeof(struct ubi_ec_hdr));

		if (ehdr->magic != be32toh(UBI_EC_HDR_MAGIC)) {
			DBG(SGR_LRED "%s: Bad block ? PEB %d @ %08x\n",
			    __func__, i, i * lubi->peb_sz);
			continue;
		}

		peb_stats[PEB_HAS_EHDR_MAGIC]++;
		if (crc32(ehdr, UBI_EC_HDR_SIZE_CRC) != be32toh(ehdr->hdr_crc))
			continue;
		peb_stats[PEB_HAS_EHDR_CRC_OK]++;

		// robustness paranoia mode off:
		if (!dynparams_set) {
			lubi->vhdr_offs = be32toh(ehdr->vid_hdr_offset);
			lubi->data_offs = be32toh(ehdr->data_offset);

			lubi->leb_sz = lubi->peb_sz - lubi->data_offs;
			lubi->vtbl_slots = lubi->leb_sz / UBI_VTBL_RECORD_SIZE;
			if (lubi->vtbl_slots > UBI_MAX_VOLUMES)
				lubi->vtbl_slots = UBI_MAX_VOLUMES;
		}

		ec = be64toh(ehdr->ec);
		mean_ec += ec;
		if (ec < min_ec)
			min_ec = ec;
		if (ec > max_ec)
			max_ec = ec;

		flash_read(lubi, vhdr, i, lubi->vhdr_offs,
			   sizeof(struct ubi_vid_hdr));

		if (vhdr->magic != be32toh(UBI_VID_HDR_MAGIC))
			continue;

		peb_stats[PEB_HAS_VHDR_MAGIC]++;
		if (crc32(vhdr, UBI_VID_HDR_SIZE_CRC) != be32toh(vhdr->hdr_crc))
			continue;
		peb_stats[PEB_HAS_VHDR_CRC_OK]++;
		peb->vhdr_crc_ok = 1;

		DBG("%s:%3d: PEB %3d @ %08x: vol_id %8X lnum %5d sqnum %5lld ec %lld\n",
		    __func__, __LINE__, i, i * lubi->peb_sz,
		    be32toh(vhdr->vol_id), be32toh(vhdr->lnum),
		    (long long)be64toh(vhdr->sqnum), (long long)ec);
	}

	DBG(SGR_BRST "PEBs counts (Total = %d):\n", lubi->peb_nb);
	for (int i = 0; i < PEB_HAS_CNT; i++)
		DBG("\t%s - %3d\n", peb_has_strs[i], peb_stats[i]);
	DBG(SGR_BRST "Erase counters (min-mean-max):\n\tEC  %lld-%lld-%lld\n",
	    (long long)min_ec,
	    (long long)mean_ec / (peb_stats[PEB_HAS_EHDR_CRC_OK] ?: 1),
	    (long long)max_ec);

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
		    be32toh(recs[i].crc))
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
	int ret_len = 0, lebs_ok = 0, used_ebs = 0;
	int is_lvl, data_ok;

	DBG_FUNC_ENTRY();

	is_lvl = vol_id == UBI_LAYOUT_VOLUME_ID;

	if (is_lvl)
		usable_leb_sz = lubi->leb_sz;
	else if (!lubi->vtbl_recs ||
		  lubi->vtbl_recs[vol_id].vol_type != UBI_VID_STATIC)
			return -1;
	else
		usable_leb_sz = lubi->leb_sz -
				be32toh(lubi->vtbl_recs[vol_id].data_pad);

	if (max_lnum > CFG_LUBI_PEB_NB_MAX)
		max_lnum = CFG_LUBI_PEB_NB_MAX;

	memset(leb2pebs, 0, max_lnum * sizeof(leb2pebs[0]));

	for (int i = lubi->peb_min; i < lubi->peb_min + lubi->peb_nb; i++) {
		struct peb_rec *peb = &lubi->pebs[i];
		struct ubi_vid_hdr *prev_vhdr, *vhdr = &peb->vhdr;
		uint32_t lnum, len, prev_len;
		struct leb2peb *l2p;
		void *buf_dst, *rd_dst;

		if (!peb->vhdr_crc_ok ||
		    vhdr->vol_id != htobe32(vol_id))
			continue;

		lnum = be32toh(vhdr->lnum);
		if (lnum > max_lnum)
			continue;

		l2p = &leb2pebs[lnum];
		buf_dst = (uint8_t *)buf + lnum * usable_leb_sz;
		// - The layout volume is special-cased because of the way :(
		// Linux-UBI handles its data_{crc,size} when restoring it
		//
		// - Linux-UBI uses the LEB size to compute vtbl_slots
		len = is_lvl ? lubi->vtbl_slots * UBI_VTBL_RECORD_SIZE :
			       be32toh(vhdr->data_size);
		if (!l2p->valid) {
			rd_dst = buf_dst;
		} else {
			prev_vhdr = &lubi->pebs[l2p->peb].vhdr;

			if (be64toh(vhdr->sqnum) < be64toh(prev_vhdr->sqnum))
				continue;

			rd_dst = lubi->scratch_leb;
			// clobber the buffer
			memset(rd_dst + len - len / 8, 0x5A, len / 8);
		}

		flash_read(lubi, rd_dst, i, lubi->data_offs, len);

		if (is_lvl)
			data_ok = !check_vtbl(lubi, rd_dst);
		else
			data_ok = crc32(rd_dst, len) == be32toh(vhdr->data_crc);

		if (data_ok) {
			l2p->peb = i;
			if (!l2p->valid) {
				l2p->valid = 1;
				if (!lebs_ok++)
					used_ebs = be32toh(vhdr->used_ebs);
			} else {
				prev_len = is_lvl ?
					   len : be32toh(prev_vhdr->data_size);

				memmove(buf_dst, rd_dst, len);
				ret_len -= prev_len;
			}
			ret_len += len;
		}
	}

	DBG(SGR_BRST "%s: Volume \"%s\"\n\tEBs used / ok: %d / %d\n\tread %d bytes\n",
	    __func__, is_lvl ? NULL : lubi->vtbl_recs[vol_id].name, used_ebs,
	    lebs_ok, ret_len);

	if ((used_ebs != lebs_ok) && (!is_lvl || lebs_ok < 1)) {
		DBG(SGR_BRED "%s: Volume read failure (read %d bytes)\n",
		    __func__, ret_len);
		return -1;
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

	len = htobe16(len);
	for (int i = 0; i < lubi->vtbl_slots; i++) {
		/* Should not happen */
		if (crc32(&recs[i], UBI_VTBL_RECORD_SIZE_CRC) !=
		    be32toh(recs[i].crc)) {
			DBG(SGR_BRED "%s: Bad VTBL rec\n", __func__);
			continue;
		}
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
int lubi_attach(void *priv)
{
	struct lubi_priv *lubi = priv;
	struct leb2peb *leb2pebs = lubi->scratch_leb2pebs;

	DBG_FUNC_ENTRY();

	if (lubi_scan(lubi))
		return -1;

	if (lubi_read_svol(lubi, lubi->vtbls_buf, UBI_LAYOUT_VOLUME_ID, 1) < 0)
		return -1;

	for (int i = 0; i < 2; i++)
		DBG("LVL: LEB[%1d] -> PEB[%3d] - valid: %s\n",
		    i, leb2pebs[i].peb, leb2pebs[i].valid ? "yes" : "no");

	if (leb2pebs[0].valid)
		lubi->vtbl_recs = (void *)&lubi->vtbls_buf[0];
	else if (leb2pebs[1].valid)
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

	memset(lubi, 0, sizeof(struct lubi_priv));

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

	if (crc32_init()) {
		DBG("crc32_init failed\n");
		return -1;
	}

	return 0;
}
