#!/bin/sh
# Copyright (c) 2005 Poul-Henning Kamp.
#
# See /usr/share/examples/etc/bsd-style-copyright for license terms.
#
# $FreeBSD$
#

set -e

# Name of this NanoBSD build.  (Used to construct workdir names)
NANO_NAME=full

# Source tree directory
NANO_SRC=/usr/src

# Object tree directory
# default is subdir of /usr/obj
# XXX: MAKEOBJDIRPREFIX handling... ?
#NANO_OBJ=""

# Parallel Make
NANO_PMAKE="make -j 3"

# Options to put in make.conf during buildworld only
CONF_BUILD=' '

# Options to put in make.conf during installworld only
CONF_INSTALL=' '

# Options to put in make.conf during both build- & installworld.
CONF_WORLD=' '

# Kernel config file to use
NANO_KERNEL=GENERIC

# Newfs paramters to use
NANO_NEWFS="-b 4096 -f 512 -i 8192 -O1 -U"

# Target media size in 512 bytes sectors
NANO_MEDIA=1048576

# Number of code images on media (1 or 2)
NANO_IMAGES=2

# Size of configuration file system in 512 bytes sectors
NANO_CONFSIZE=2048

# Size of data file system in 512 bytes sectors
NANO_DATASIZE=1023

# Media geometry, only relevant if bios doesn't understand LBA.
NANO_SECTS=32
NANO_HEADS=16

#######################################################################
# Not a variable at this time

NANO_ARCH=i386

#######################################################################
# Functions which can be overridden in configs.

clean_target ( ) (
	echo "## Clean and create object directory (${MAKEOBJDIRPREFIX})"

	if rm -rf ${MAKEOBJDIRPREFIX} > /dev/null 2>&1 ; then
		true
	else
		chflags -R noschg ${MAKEOBJDIRPREFIX}
		rm -rf ${MAKEOBJDIRPREFIX}
	fi
	mkdir -p ${MAKEOBJDIRPREFIX}
)

make_conf_build ( ) (
	echo "## Construct build make.conf ($NANO_MAKE_CONF)"

	echo "${CONF_WORLD}" > ${NANO_MAKE_CONF}
	echo "${CONF_BUILD}" >> ${NANO_MAKE_CONF}
)

build_world ( ) (
	echo "## run buildworld"
	echo "### log: ${MAKEOBJDIRPREFIX}/_.bw"

	cd ${NANO_SRC}
	${NANO_PMAKE} __MAKE_CONF=${NANO_MAKE_CONF} buildworld \
		> ${MAKEOBJDIRPREFIX}/_.bw 2>&1
)

build_kernel ( ) (
	echo "## build kernel ($NANO_KERNEL"
	echo "### log: ${MAKEOBJDIRPREFIX}/_.bk"

	if [ -f ${NANO_KERNEL} ] ; then
		cp ${NANO_KERNEL} ${NANO_SRC}/sys/${NANO_ARCH}/conf
	fi

	cd ${NANO_SRC}
	${NANO_PMAKE} buildkernel \
		__MAKE_CONF=${NANO_MAKE_CONF} KERNCONF=${NANO_KERNEL} \
		> ${MAKEOBJDIRPREFIX}/_.bk 2>&1
)

clean_world ( ) (
	echo "## Clean and create world directory (${NANO_WORLDDIR})"
	if rm -rf ${NANO_WORLDDIR}/ > /dev/null 2>&1 ; then
		true
	else
		chflags -R noschg ${NANO_WORLDDIR}/
		rm -rf ${NANO_WORLDDIR}/
	fi
	mkdir -p ${NANO_WORLDDIR}/
)

make_conf_install ( ) (
	echo "## Construct install make.conf ($NANO_MAKE_CONF)"

	echo "${CONF_WORLD}" > ${NANO_MAKE_CONF}
	echo "${CONF_INSTALL}" >> ${NANO_MAKE_CONF}
)

install_world ( ) (
	echo "## installworld"
	echo "### log: ${MAKEOBJDIRPREFIX}/_.iw"

	cd ${NANO_SRC}
	${NANO_PMAKE} __MAKE_CONF=${NANO_MAKE_CONF} installworld \
		DESTDIR=${NANO_WORLDDIR} \
		> ${MAKEOBJDIRPREFIX}/_.iw 2>&1
	chflags -R noschg ${NANO_WORLDDIR}
)

install_etc ( ) (

	echo "## install /etc"
	echo "### log: ${MAKEOBJDIRPREFIX}/_.etc"

	cd ${NANO_SRC}/etc
	${NANO_PMAKE} __MAKE_CONF=${NANO_MAKE_CONF} distribution \
		DESTDIR=${NANO_WORLDDIR} \
		> ${MAKEOBJDIRPREFIX}/_.etc 2>&1
)

install_kernel ( ) (
	echo "## install kernel"
	echo "### log: ${MAKEOBJDIRPREFIX}/_.ik"

	cd ${NANO_SRC}
	${NANO_PMAKE} installkernel \
		DESTDIR=${NANO_WORLDDIR} \
		__MAKE_CONF=${NANO_MAKE_CONF} KERNCONF=${NANO_KERNEL} \
		> ${MAKEOBJDIRPREFIX}/_.ik 2>&1
)

setup_diskless ( ) (
	echo "## configure diskless setup"

	cd ${NANO_WORLDDIR}

	# create diskless marker file
	touch etc/diskless

	# link /var under /conf
	# we use hard links so we have them both places.
	# the files in /var will be hidden by the mount.
	# XXX: configure /var ramdisk size
	mkdir -p conf/base/var
	find var -print | cpio -dumpl ../conf/base/var

	# move /etc under /conf
	# we use hard links so we have them both places.
	# the files in /etc will be hidden by the mount.
	# XXX: configure /etc ramdisk size
	mkdir -p conf/base/etc conf/default/etc
	find etc -print | cpio -dumpl ../conf/base/etc

	# pick up config files from the special partition
	echo "mount -o ro /dev/ad0s3" > conf/default/etc/remount

	# Put /tmp on the /var ramdisk
	rmdir tmp
	ln -s var/tmp tmp
)

prune_usr() (

	# Remove all empty directories in /usr 
	find ${NANO_WORLDDIR}/usr -type d -depth -print |
		while read d
		do
			rmdir $d > /dev/null 2>&1 || true 
		done
)

create_i386_diskimage ( ) (

	echo $NANO_MEDIA $NANO_IMAGES \
		$NANO_CONFSIZE $NANO_DATASIZE \
		$NANO_SECTS $NANO_HEADS |
	awk '
		{
		# size of cylinder in sectors
		cs = $5 * $6

		# number of full cylinders on media
		cyl = int ($1 / cs)

		# output fdisk geometry spec
		if (cyl <= 1023)
			print "g c" cyl " h" $6 " s" $5
		else
			print "g c" 1023 " h" $6 " s" $5

		# size of data partition in full cylinders
		dsl = int (($4 + cs - 1) / cs)

		# size of config partition in full cylinders
		csl = int (($3 + cs - 1) / cs)

		# size of image partition(s) in full cylinders
		isl = int ((cyl - dsl - csl) / $2)

		# First image partition start at second track
		print "p 1 165 " $5, isl * cs - $5
		c = isl * cs;

		# Second image partition (if any) also starts offset one 
		# track to keep them identical.
		if ($2 > 1) {
			print "p 2 165 " $5 + c, isl * cs - $5
			c += isl * cs;
		}

		# Config partition starts at cylinder boundary.
		print "p 3 165 " c, csl * cs
		c += csl * cs

		# Data partition (if any) starts at cylinder boundary.
		if ($4 > 0)
			print "p 4 165 " c, dsl * cs
		}
	' > ${MAKEOBJDIRPREFIX}/_.fdisk

	IMG=${MAKEOBJDIRPREFIX}/_.disk.full
	MNT=${MAKEOBJDIRPREFIX}/_.mnt
	mkdir -p ${MNT}

	dd if=/dev/zero of=${IMG} bs=${NANO_SECTS}b \
	    count=`expr ${NANO_MEDIA} / ${NANO_SECTS}`

	MD=`mdconfig -a -t vnode -f ${IMG} -x ${NANO_SECTS} -y ${NANO_HEADS}`

	trap "df -i ${MNT} ; umount ${MNT} || true ; mdconfig -d -u $MD" 1 2 15 EXIT

	fdisk -i -f ${MAKEOBJDIRPREFIX}/_.fdisk ${MD}
	# XXX: params
	# XXX: pick up cached boot* files, they may not be in image anymore.
	boot0cfg -B -b ${NANO_WORLDDIR}/boot/boot0sio -o packet -s 1 -m 3 ${MD}
	bsdlabel -w -B ${MD}s1

	# Create first image
	newfs ${NANO_NEWFS} /dev/${MD}s1a
	mount /dev/${MD}s1a ${MNT}
	df -i ${MNT}
	( cd ${NANO_WORLDDIR} && find . -print | cpio -dump ${MNT} )
	# XXX: make_fstab
	df -i ${MNT}
	( cd ${MNT} && mtree -c ) > ${MAKEOBJDIRPREFIX}/_.mtree
	( cd ${MNT} && du -k ) > ${MAKEOBJDIRPREFIX}/_.du
	umount ${MNT}

	if [ $NANO_IMAGES -gt 1 ] ; then
		# Duplicate to second image (if present)
		dd if=/dev/${MD}s1 of=/dev/${MD}s2 bs=64k
	fi
	
	# Create Config slice
	newfs ${NANO_NEWFS} /dev/${MD}s3
	# XXX: fill from where ?

	# Create Data slice, if any.
	if [ $NANO_DATASIZE -gt 0 ] ; then
		newfs ${NANO_NEWFS} /dev/${MD}s4
		# XXX: fill from where ?
	fi

	dd if=/dev/${MD}s1 of=${MAKEOBJDIRPREFIX}/_.disk.image bs=64k
	mdconfig -d -u $MD
)


#######################################################################
#
# Heavy wizardry ahead, no user serviceable parts below.
#
#######################################################################

#######################################################################
# Parse arguments

args=`getopt c: $*`
if [ $? -ne 0 ] ; then
	echo "Usage: $0 [-c config file]" 1>&2
	exit 2
fi

set -- $args
for i
do
	case "$i" 
	in
	-c)
		. "$2"
		shift;
		shift;
		;;
	--)
		shift;
		break;
	esac
done


#######################################################################
# Internal variables
if [ "x${NANO_OBJ}" = "x" ] ; then
	MAKEOBJDIRPREFIX=/usr/obj/nanobsd.${NANO_NAME}/
else
	MAKEOBJDIRPREFIX=${NANO_OBJ}
fi

NANO_WORLDDIR=${MAKEOBJDIRPREFIX}/_.w
NANO_MAKE_CONF=${MAKEOBJDIRPREFIX}/make.conf

#######################################################################
#
export MAKEOBJDIRPREFIX

export NANO_ARCH
export NANO_CONFSIZE
export NANO_DATASIZE
export NANO_HEADS
export NANO_IMAGES
export NANO_MAKE_CONF
export NANO_MEDIA
export NANO_NAME
export NANO_NEWFS
export NANO_OBJ
export NANO_PMAKE
export NANO_SECTS
export NANO_SRC
export NANO_WORLDDIR

#######################################################################
# Set up object directory

clean_target
make_conf_build
build_world
build_kernel
clean_world
make_conf_install
install_world
install_etc
install_kernel
setup_diskless
prune_usr
create_${NANO_ARCH}_diskimage
