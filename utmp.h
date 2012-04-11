#ifndef UTMP_H
#define UTMP_H

#define UT_LINESIZE	16
#define UT_NAMESIZE	32
#define UT_HOSTSIZE	128

#define _PATH_UTMP	"/var/run/utx.active"
#define _PATH_WTMP	"/var/log/utx.log"
#define _PATH_LASTLOG	"/var/log/utx.lastlogin"

#endif
