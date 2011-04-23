#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <string.h>

#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>


#include "logger.h"

#ifdef DEBUG
#undef DEBUG
#define DEBUG 1
#else
#define DEBUG 0
#endif

#define MAX_STRING 4096
#define MAX_MSG MAX_STRING-16


static int logging_on=0;

static int logfile_fd=-1;

const char *prioritynames[]=
{
    "alert", 
    "crit",
    "debug",
    "emerg",
    "err",
    "info",
    "notice",
    "warning",
    NULL
};


#define MAX_PRIORITY (sizeof(prioritynames)/sizeof(char *))

int logfd(void)
{
	return logfile_fd;
}

void logger(int pri,char *fmt,...)
{
	if((pri==LOG_DEBUG) && !DEBUG)
		return;

	static int progcleanedup=0;
	char *p=NULL;
	if(!progcleanedup)
	{
		if ((p = (char *) strrchr (progname, '/')) != NULL)
			progname = p + 1;
		progcleanedup=1;

	}

    va_list args;
    char buffer[MAX_STRING];
    char msg[MAX_MSG];

	
    pri=(pri<MAX_PRIORITY) ? pri : MAX_PRIORITY;
//	printf("got priority %d\n",pri);
	
    memset(buffer,0,MAX_STRING);
    memset(msg,0,MAX_MSG);
    
    va_start(args,fmt);
    vsnprintf(msg,sizeof(msg)-1,fmt,args);
    va_end(args);
    
#ifdef WITH_SYSLOG

	openlog(progname,0,facility);
	syslog(pri,"%s",msg);
	closelog();
	return;


#else

    snprintf(buffer,sizeof(buffer)-1,"%s: %s\n",prioritynames[pri],msg);
	if(logging_on)
	{
		if((write(logfile_fd,buffer,strnlen(buffer,MAX_STRING)))<0)
		{
			perror("write");
			printf("logfile_fd is %d\n",logfile_fd);
	    	printf("%s", buffer);
		}
	}else
	    printf("%s", buffer);

	return;		
#endif /* WITH_SYSLOG */
}

int log_open(const char *logfile)
{
	
#ifdef WITH_SYSLOG
	logfile_fd=1;
#else
	if((logfile_fd=open(logfile,O_CREAT|O_WRONLY|O_APPEND|O_SYNC,0644))<0)
	{
		perror("open");
		fprintf(stderr,"couldn't open logfile %s\n",logfile);
	}else
		logging_on=1;
#endif /* WITH_SYSLOG */	
	return logfile_fd;
	
}

int log_close(void)
{
	int ret=0;
	if(logging_on) 
		if((ret=close(logfile_fd))>0)
			logging_on=0;
	
	return ret;
}
