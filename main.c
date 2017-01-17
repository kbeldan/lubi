/*
 * Copyright (c) 2017 Sagemcom
 * Author: karl.beldan@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/mman.h>

#include <unistd.h>
#include <string.h>

#include <getopt.h>
#include <err.h>

#include <libgen.h>

#include "liblubi.h"
#include "config.h"

#define handle_error(str) \
	do { err(-1, "%d: %s", __LINE__, str); return -1; } while (0)

struct data {
	char *addr;
	int peb_sz;
};

static int flash_read(void *priv, void *dst, int pnum, int offset, int len)
{
	struct data *data = (struct data *)priv;
        memcpy(dst, data->addr + data->peb_sz * pnum + offset, len);
        return len;
}

static void usage(char *prg)
{
	fprintf(stderr, "Usage: %s\n"
		"\t\t--ifile in_file\n"
		"\t\t[--ofile out_file]\n"
		"\t\t[--peb_min peb_min]\n"
		"\t\t[--peb_nb peb_nb]\n"
		"\t\t--peb_sz peb_sz\n"
		"\t\t[--vol volume_name]\n",
		prg);
}

static void version(char *prg)
{
	printf("%s - " PACKAGE_VERSION "\n", prg);
}

int main(int argc, char *argv[])
{
	struct data data;
	void *lubi_priv;
	int i_fd, o_fd = 0, len;
	struct stat stat;

	unsigned char *buf;
	int vol_id, upd_marker;

	const char *arg_ipath = NULL, *arg_opath = NULL, *arg_volname = NULL;
	int arg_peb_sz = 0, arg_peb_min = 0, arg_peb_nb = 0;
	char *prg = basename(argv[0]);

	for (;;) {
		static const struct option l_opts[] = {
			{"help",       no_argument,       0, 0},
			{"ifile",      required_argument, 0, 1},
			{"ofile",      required_argument, 0, 2},
			{"peb_min",    required_argument, 0, 3},
			{"peb_nb",     required_argument, 0, 4},
			{"peb_sz",     required_argument, 0, 5},
			{"vol",        required_argument, 0, 6},
			{"version",    no_argument,       0, 7},
			{0, 0, 0, 0},
		};
		int opt_idx = 0;
		int c = getopt_long_only(argc, argv, "", l_opts, &opt_idx);
		if (c == EOF)
			break;

		switch (c) {
		case  0:
			usage(prg);
			exit(0);
		case  1:
			arg_ipath = optarg;
			break;
		case  2:
			arg_opath = optarg;
			break;
		case  3:
			arg_peb_min = atoi(optarg);
			break;
		case  4:
			arg_peb_nb = atoi(optarg);
			break;
		case  5:
			arg_peb_sz = atoi(optarg);
			break;
		case  6:
			arg_volname = optarg;
			break;
		case  7:
			version(prg);
			exit(0);
		}
	}

	if (!arg_ipath || !arg_peb_sz) {
		usage(prg);
		exit(-1);
	}

	if ((i_fd = open(arg_ipath, O_RDONLY)) == -1)
		handle_error(arg_ipath);

	if (fstat(i_fd, &stat) == -1)
		handle_error(arg_ipath);

	data.addr = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, i_fd, 0);
	if (data.addr == MAP_FAILED)
		handle_error("mmap");

	if (arg_opath &&
	    (o_fd = open(arg_opath, O_WRONLY | O_TRUNC | O_CREAT, 0644)) == -1)
		handle_error(arg_opath);

	if (!(lubi_priv = malloc(lubi_mem_sz())))
		handle_error("malloc");

	data.peb_sz = arg_peb_sz;

	if (lubi_init(lubi_priv, &data, flash_read, data.peb_sz, arg_peb_min,
		      arg_peb_nb ?: stat.st_size / data.peb_sz)) {
		fprintf(stderr, "%s:%d: lubi_init failed\n", __func__, __LINE__);
		exit(-1);
	}
	if (lubi_attach(lubi_priv, 0, 0)) {
		fprintf(stderr, "%s:%d: lubi_attach failed\n", __func__, __LINE__);
		exit(-1);
	}
	if (lubi_list_vols(lubi_priv)) {
		fprintf(stderr, "%s:%d: lubi_list_vols failed\n", __func__, __LINE__);
		exit(-1);
	}

	if (!arg_volname)
		return 0;

	if ((vol_id = lubi_get_vol_id(lubi_priv, arg_volname, &upd_marker)) < 0) {
		fprintf(stderr, "%s:%d: Could not find volume \"%s\"\n",
		       __func__, __LINE__, arg_volname);
		exit(-1);
	}
	if (!(buf = malloc(data.peb_sz * 32)))
		handle_error("malloc");
	if ((len = lubi_read_svol(lubi_priv, buf, vol_id, 32 - 1)) < 0) {
		fprintf(stderr, "%s:%d: lubi_read_svol failed\n", __func__, __LINE__);
		exit(-1);
	}

	fprintf(stderr, "Dumping volume \"%s\" (%d bytes) ..\n", arg_volname, len);
	if (o_fd) {
		while (len) {
			ssize_t w = write(o_fd, buf, len);
			if (w < 0)
				handle_error("write");
			buf += w;
			len -= w;
		}
	} else {
		for (int i = 0; i < len; i++) {
			if (!(i % 4) && i)
				putchar(i % 16 ? ' ' : '\n');
			printf("%02x ", buf[i]);
		}
		putchar('\n');
	}

	return 0;
}
