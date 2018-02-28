# LUBI - Lightweight UBI library

This is a simple library that can read UBI (Unsorted Block Images) static volumes.  
It can be used in early stages of a bootloader as well as tested as-is on a workstation with a  
flash dump input (see example below). For integrity, it uses solely the CRCs computed by UBI  
and does not try to rely on the values returned by flash_read callback.

It doesn't use malloc or trees but:

* memset / memmove / strcmp
* htobe{16,32}
* crc32

## Building

### Example
```
CC=mips-linux-gnu-gcc ./configure --enable-debug && make
```

### Compile Flags

Flags (c.f. liblubi\_cfg.h):

```
CFG_LUBI_PEB_NB_MAX* - Maximum number of PEBs the lib can handle
CFG_LUBI_PEB_SZ_MAX* - Maximum size of a PEB the lib can handle
CFG_LUBI_DBG         - Enable stdio debugging
CFG_LUBI_INT_CRC32   - Use the internal crc32 func
```

(\*) These flags allow for some code simplification but said hard limits could be handled otherwise.

## Usage example
### Example program
An example program is provided:
```
$ ./lubi --help
Usage: lubi     --ifile in_file
                [--ofile out_file]
                [--peb_min peb_min]
                [--peb_nb peb_nb]
                --peb_sz peb_sz
                [--vol volume_name]

$ nanddump --bb=dumpbad /dev/mtd1 -f mtd1.dat
$ ./lubi --ifile mtd1.dat --peb_sz $((128 << 10)) --vol vol_0 --ofile vol_0.dat

See also nandsim.sh.
```
### Code snippet

Parametering for a flash with 128KB blocks and a UBI partition starting at block 1 and ending  
at block 64 incl., i.e. for mtd addresses running from start=0x20000 to end=0x820000.  

```
struct ubi_args ubi_args;
void *ubi_priv = malloc(ubi_mem_sz());

ubi_args.priv = flash_read_cookie; // arg to call flash_read with
ubi_args.flash_read = flash_read;
ubi_args.peb_sz = 128 << 10;
ubi_args.peb_min = 1;
ubi_args.peb_nb = 64;


ubi_init(ubi_priv, &ubi_args));

ubi_attach(ubi_priv);
vol_id = ubi_get_vol_id(ubi_priv, "vol_0", &upd_marker));
ubi_read_svol(ubi_priv, buf, vol_id, -1));
```
