#!/bin/bash

LUBI_PRG=./main

modprobe nandsim parts="32" bbt=1
modprobe gluebi

rand_data=rand.dat
vol="vol_0"
mtd=$(sed -ne 's/^\([^:]*\):.*NAND sim.*0"$/\1/p' /proc/mtd)
cdev=$(ubiattach -p /dev/$mtd -b 1 | sed -ne 's/.*number \([0-9]*\).*/\1/p')

ubimkvol /dev/ubi$cdev -S 4 -N $vol -t static
dd bs=23581 count=1 if=/dev/urandom of=$rand_data 2>/dev/null
ubiupdatevol /dev/ubi0_0 $rand_data

nanddump --bb=dumpbad -n /dev/$mtd -f ${mtd}.dat

$LUBI_PRG --ifile ${mtd}.dat --peb_sz $((16 << 10)) --vol $vol --ofile=${vol}.dat

vol_mtd=/dev/$(sed -ne 's/^\([^:]*\):.*'$vol'"$/\1/p' /proc/mtd)

for f in $rand_data $vol_mtd ${vol}.dat; do md5sum $f; done

rm -f $rand_data ${vol}.dat ${mtd}.dat
ubidetach -d $cdev
for m in gluebi nandsim; do modprobe -r $m; done
