/*-
 * Copyright (c) 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1993 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)cat.c	7.1 (Berkeley) %G%";
#endif /* not lint */

main()
{
	register int c, fd;
	char c;

	fd = getfile("File", 0);
	while (read(fd, &c, 1) == 1)
		putchar(c);
	exit(0);
}
