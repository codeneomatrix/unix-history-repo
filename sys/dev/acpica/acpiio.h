/*-
 * Copyright (c) 1999 Takanori Watanabe <takawata@shidahara1.planet.sci.kobe-u.ac.jp>
 * Copyright (c) 1999 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * Core ACPI subsystem ioctls
 */
#define ACPIIO_ENABLE		_IO('P', 1)
#define ACPIIO_DISABLE		_IO('P', 2)
#define ACPIIO_SETSLPSTATE	_IOW('P', 3, int)

#define ACPI_CMBAT_MAXSTRLEN 32
struct acpi_bif {
	u_int32_t unit;				/* 0 for mWh, 1 for mAh */
	u_int32_t dcap;				/* Design Capacity */
	u_int32_t btech;			/* Battery Technorogy */
	u_int32_t lfcap;			/* Last Full capacity */
	u_int32_t dvol;				/* Design voltage (mV) */
	u_int32_t wcap;				/* WARN capacity */
	u_int32_t lcap;				/* Low capacity */
	u_int32_t gra1;				/* Granulity 1(Warn to Low) */
	u_int32_t gra2;				/* Granulity 2(Full to Warn) */
	char model[ACPI_CMBAT_MAXSTRLEN];	/* model identifier */
	char serial[ACPI_CMBAT_MAXSTRLEN];	/* Serial number */
	char type[ACPI_CMBAT_MAXSTRLEN];	/* Type */
	char oeminfo[ACPI_CMBAT_MAXSTRLEN];	/* OEM infomation */
};

struct acpi_bst {
	u_int32_t state;			/* Battery State */
	u_int32_t rate;				/* Present Rate */
	u_int32_t cap;				/* Remaining Capacity */
	u_int32_t volt;				/* Present Voltage */
};

#ifdef _KERNEL
extern int	acpi_register_ioctl(u_long cmd, int (* fn)(u_long cmd, caddr_t addr, void *arg), void *arg);
extern void	acpi_deregister_ioctl(u_long cmd, int (* fn)(u_long cmd, caddr_t addr, void *arg));
#endif
