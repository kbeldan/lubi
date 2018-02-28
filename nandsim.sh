#!/usr/bin/env bash

#qemu="qemu-mips -L /usr/mips-linux-gnu"
LUBI_PRG="$qemu $(dirname $(realpath $0))/lubi"

modprobe nandsim parts="32" bbt=1
modprobe gluebi

rand_data=rand.dat
vol="vol_0"
mtdline=($(grep "NAND simulator partition 0" /proc/mtd))
mtd=${mtdline[0]%:}
peb_sz=$((0x${mtdline[2]}))
cdev=$(ubiattach -p /dev/$mtd -b 1 | sed -ne 's/.*number \([0-9]*\).*/\1/p')

ubimkvol /dev/ubi$cdev -S 4 -N $vol -t static
dd bs=23581 count=1 if=/dev/urandom of=$rand_data 2>/dev/null
# Retry for EBUSY
i=0; while [ $((i++)) -lt 3 ]; do ubiupdatevol /dev/ubi0_0 $rand_data && break; done

nanddump --bb=dumpbad -n /dev/$mtd -f ${mtd}.dat
$LUBI_PRG --ifile ${mtd}.dat --peb_sz $peb_sz --vol $vol --ofile=${vol}.dat

vol_mtd=/dev/$(sed -ne 's/^\([^:]*\):.*'$vol'"$/\1/p' /proc/mtd)

files="$rand_data $vol_mtd ${vol}.dat"
i=0 md5s=(); for f in $files; do md5=$(md5sum $f); md5s[$i]=${md5%% *}; ((i++)); done
[ ${md5s[0]}${md5s[0]} != ${md5s[1]}${md5s[2]} ] && {
	echo XXX Bad md5s ${md5s[@]}
	exit 1
}

rm -f $rand_data ${vol}.dat ${mtd}.dat
ubidetach -d $cdev
for m in gluebi nandsim; do modprobe -r $m; done
