#ifndef __LOGGER_H
#define __LOGGER_H

extern char *progname;
extern int facility;

void logger(int, char *,...);
int log_open(const char *);
int log_close(void);
int logfd(void);
void setlogfile(const char *);

#ifndef WITH_SYSLOG
#define LOG_LOCAL2 -1

#define LOG_ALERT 0
#define LOG_CRIT 1
#define LOG_DEBUG 2
#define LOG_EMERG 3
#define LOG_ERR 4
#define LOG_INFO 5
#define LOG_NOTICE 6
#define LOG_WARNING 7

#else

#include <syslog.h>

#endif /* WITH_SYSLOG */

#endif /* __LOGGER_H */
