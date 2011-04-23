/*

sudosh - sudo shell that supports input and output logging to syslog

Copyright 2004 and $Date: 2008/02/25 20:29:12 $
		Douglas Richard Hanks Jr.

Licensed under the Open Software License version 2.0

This program is free software; you can redistribute it
and/or modify it under the terms of the Open Software
License, version 2.0 by Lauwrence E. Rosen.

This program is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the Open Software License for details.

*/

#include "super.h"
#include "struct.h"
#include "getopt.h"
#include "stdio.h"
#include "stdlib.h"

#ifndef SIGCHLD
#define SIGCHLD	SIGCLD
#endif

#define WRITE(a, b, c) do_write(a, b, c, __FILE__, __LINE__)

#define DEF_LOGFILE "sudosh.log"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef MAX_SYSLOG_MSG
#define MAX_SYSLOG_MSG 512
#endif

#define BYTES_THROTTLE 512
#define SECONDS_THROTTLE 1

#define mysyslog(pri,format,...) logger(pri,format,##__VA_ARGS__)
//void mysyslog (int, const char *, ...);

char logfile[PATH_MAX]=DEF_LOGFILE;

static struct termios termorig;
static struct winsize winorig;

struct pst
{
	char *master;
	char *slave;
	int mfd;
	int sfd;
} pspair;

struct s_file
{
	int fd;
	int bytes;
	struct stat stat;
	struct stat cstat;
	struct stat tstat;
	char name[BUFSIZ];
	char str[BUFSIZ];
};

struct s_file script;
struct s_file timing;

struct s_env
{
	char str[BUFSIZ];
	char *ptr;
};

struct s_user
{
	char to[BUFSIZ];
	char from[BUFSIZ];
	char *vshell;
	struct s_env home;
	struct s_env to_home;
	struct s_env term;
	struct s_env path;
	struct s_env mail;
	struct s_env shell;
	struct s_env logname;
	struct passwd *pw;
};

struct s_user user;
struct s_option sudosh_option;
int facility = LOG_LOCAL2;

time_t now = 0;
time_t sysloglast = 0;
char syslogbuf[MAX_SYSLOG_MSG];
int syslogcount = 0;
time_t timinglast = 0;
char timingbuf[MAX_SYSLOG_MSG];
int timingcount = 0;
int buffertimeout = 30;
int logtosyslog = 1;
void cleansyslog ();

char *progname;
char start_msg[BUFSIZ];

int loginshell = 0;

static void alarm_handler(int);
static void bye (int);
static void newwinsize (int);
static void prepchild (struct pst *);
static void rawmode (int);
static int findms (struct pst *);
char *rand2str (size_t len);
int do_write (int, void *, size_t, char *, unsigned int);

static int bytes_written=0;
void setlogfile(const char *);

extern void parse (option *, const char *);

extern int unlockpt (int);
extern int grantpt (int);
extern char *ptsname (int);
extern void mklogdir (char * logdir);

extern char *optarg;
extern int optind;

int main (int argc, char *argv[], char *environ[])
{

	int n = 1;
	int valid = -1;
	//int found = FALSE;
	char iobuf[BUFSIZ];
	char sysconfdir[BUFSIZ];
	char c_str[BUFSIZ];
	char c_command[BUFSIZ];
	char *p = NULL;
	char *c_args = NULL;
	char *rand = rand2str (16);
	now = time ((time_t *) NULL);
	sysloglast = now;
	timinglast = now;
	struct stat s;
	struct sigaction saterm;
	struct sigaction sawinch;
	struct sigaction sachild;
	struct timeval tv;
	double oldtime, newtime, difftime;
	int skip_write=0;
	int wrote=0;
	int try_read_again=0;
	int try_select_again=0;
	struct stat ttybuf;
	char errorstr[BUFSIZ];
	int error;
	int c;
	user.vshell = NULL;
	user.shell.ptr = NULL;
	user.home.ptr = NULL;
	user.term.ptr = NULL;

	progname = argv[0];

	if ((p = (char *) strrchr (progname, '/')) != NULL)
		progname = p + 1;

	if (*progname == '-')
		loginshell = 1;

	/* Who are you? */
	user.pw = getpwuid ((uid_t) geteuid ());

	if (user.pw == NULL)
		{
			fprintf (stderr, "I do not know who you are.  Stopping.\n");
			perror ("getpwuid");
			exit (EXIT_FAILURE);
		}

	strncpy (user.to, user.pw->pw_name, BUFSIZ - 1);

	user.term.ptr = getenv ("TERM");

	if (user.term.ptr == NULL)
		user.term.ptr = "dumb";

	if (strlen (user.term.ptr) < 1)
		user.term.ptr = "dumb";

	snprintf(sysconfdir, BUFSIZ - 1, "%s/sudosh.conf", SYSCONFDIR);

	sudosh_option.bytespersecond=BYTES_THROTTLE;

	parse (&sudosh_option, sysconfdir);
	facility = sudosh_option.facility;

	if(sudosh_option.bytespersecond<1)
		sudosh_option.bytespersecond=BYTES_THROTTLE;

	logger(LOG_DEBUG,"Using throttle bytes of %d\n",sudosh_option.bytespersecond);
	logger(LOG_DEBUG,"BUFSIZ = %d\n",BUFSIZ);

	while ((c = getopt(argc, argv, "l:c:t:hivVn")) != EOF)
	{
		switch (c)
		{
		case 'l':
			setlogfile(optarg);
			break;
		case 'c':
//			fprintf(stderr,"optarg is [%s]\n",optarg);
			strncpy (user.from, user.pw->pw_name, BUFSIZ - 1);
			strncpy (c_str, optarg, BUFSIZ - 1);
			strncpy (c_command, optarg, BUFSIZ -1);
			c_args = (char *) strchr (optarg, ' ');
			p=strchr(c_str, ' ');
			if (p)
			{
				p[0]=0;
//				fprintf(stderr,"args=%s\n",c_args);
			}
	
			if (c_str)
			{
//				fprintf(stderr,"Testing c\n");
				// Make sure that c_str is in argallow
				char argtest[BUFSIZ];
			
				sprintf(argtest,"$%.100s$",c_str);
//				fprintf(stderr,"Testing for %s\n",argtest);
		
				if (strstr(sudosh_option.argallow,argtest)!=NULL)
				{
          if (!logtosyslog)
          {
            struct stat logdir_stat;

            if (((stat(sudosh_option.logdir, &logdir_stat)) == -1) || !(logdir_stat.st_mode & S_IFDIR))
              mklogdir (sudosh_option.logdir);

            if ((stat(sudosh_option.logdir, &logdir_stat)) == -1)
            { 
              fprintf(stderr, "Fatal error - logdir %s is invalid: %s\n", sudosh_option.logdir, strerror(errno));
              exit(EXIT_FAILURE);
            }

            if (!(logdir_stat.st_mode & S_IFDIR))
            { 
              fprintf(stderr, "logdir '%s' is not a directory.\n", sudosh_option.logdir);
              exit(EXIT_FAILURE);
            }

					  FILE *f;
					  snprintf (script.name, (size_t) BUFSIZ - 1,
						  "%s/%s%c%s%cinteractive%c%i%c%s",
						  sudosh_option.logdir, user.from,
						  sudosh_option.fdl, user.to, sudosh_option.fdl,
						  sudosh_option.fdl, (int) now, sudosh_option.fdl,
						  rand);

					  f = fopen (script.name, "w");

					  if (f == (FILE *) 0)
					  {
						  fprintf (stderr, "%.100s: %.100s (%i)\n", script.name,
							  strerror (errno), errno);
						  exit (EXIT_FAILURE);
					  }

					  fprintf (f, "%.256s\n", c_command);
					  fclose (f);
          }
          else 
          {
            mysyslog (sudosh_option.priority, "[%i]: interactive: %s:%s: %s", now, user.from, user.to, c_command);
          }
					execl ("/bin/sh", "sh", "-c", c_command,  (char *) 0);
					exit (EXIT_SUCCESS);
					break;
				}
				else
				{
					fprintf (stderr, "\"%s\" isn't allowed to be executed.\n",
						c_str);
					exit (EXIT_FAILURE);
					break;
				}
			}
			break; //end case 'c'
        case 't':
            if (atoi(optarg) > 0) {
                buffertimeout = atoi(optarg);
            }
            break; //end case 't'
        case 'n':
          logtosyslog = 0;
          break; // end case 'n'
		case 'h':
		case '?':
			fprintf (stdout,
				 "Usage: sudosh\n"
				 "sudo shell that supports input and output logging to syslog\n"
				 "\n"
				 "-h, --help	   display this help and exit\n"
				 "-i, --init	   initialize logdir (mkdir and chmod) (ignored for compatibility)\n"
				 "-v, --version	 output version information and exit\n"
         "-t, --timeout  set the syslog buffer timeout(seconds). Defaults to 30\n"
         "-n, --nosyslog record sessions in script and timing files rather than\n"
         "               sending the messages to syslog\n"
				 "\n" "Report bugs to <%s>\n", PACKAGE_BUGREPORT);
			exit (EXIT_SUCCESS);
			break;
		case 'i':
			fprintf(stdout,"Ignoring initialize option, this is done automatically\n");
			exit (EXIT_SUCCESS);
			break;
		case 'v':
		case 'V':
			fprintf (stdout, "%s version %s\n", PACKAGE_NAME, VERSION);
			exit (EXIT_SUCCESS);
			break;
		default:
			fputs ("Try `sudosh -h' for more information.\n", stderr);
			exit (EXIT_FAILURE);
			break;
		}
	}
	if(log_open(logfile)<0)
	{
		fprintf(stderr,"Couldn't open logfile, terminating");
		exit(1);
	}
	logger(LOG_DEBUG,"Test logging.");


	if (ttyname (0) != NULL)
	{
		if (stat (ttyname (0), &ttybuf) == 0)
		{
			if ((getpwuid (ttybuf.st_uid)->pw_name) == NULL)
			{
				fprintf (stderr, "I have no idea who you are.\n");
				exit (EXIT_FAILURE);
			}
			strncpy (user.from, getpwuid (ttybuf.st_uid)->pw_name, BUFSIZ - 1);
		}
		else
		{
			fprintf (stderr, "Couldn't stat %s\n", ttyname (0));
			exit (EXIT_FAILURE);
		}
	}else
	{
		fprintf(stderr, "%s: couldn't get your controlling terminal.\n", progname);
		exit(EXIT_FAILURE);
	}
	user.pw = getpwuid ((uid_t) geteuid ());

	snprintf (user.home.str, BUFSIZ - 1, "HOME=%s", user.pw->pw_dir);
	strncpy (user.to_home.str, user.pw->pw_dir, BUFSIZ - 1);
	snprintf (user.term.str, BUFSIZ - 1, "TERM=%s", user.term.ptr);


#ifdef HAVE_GETUSERSHELL
	if ((user.shell.ptr = getenv ("SHELL")) == NULL)
		user.shell.ptr = user.pw->pw_shell;

	/* check against /etc/shells to make sure it's a real shell */
	setusershell ();
	while ((user.vshell = (char *) getusershell ()) != (char *) 0)
		{
			if (strcmp (user.shell.ptr, user.vshell) == 0)
	valid = 1;
		}
	endusershell ();

	if (valid != 1)
	{
		if (user.shell.ptr == NULL)
		{
			fprintf (stderr, "Could not determine a valid shell.\n");
			if (sudosh_option.priority!=-1)
				mysyslog (sudosh_option.priority,
				"Could not determine a valid shell");
			exit (EXIT_FAILURE);
		}else
		{
			fprintf (stderr, "%s is not in /etc/shells\n", user.shell.ptr);
			mysyslog (sudosh_option.priority,
				"%s,%s: %s is not in /etc/shells", user.from,
				ttyname (0), user.shell.ptr);
			exit (EXIT_FAILURE);
	}
		}

	if (stat ((const char *) user.shell.ptr, &s) == -1)
	{
		fprintf (stderr, "Shell %s doesn't exist.\n", user.shell.ptr);
		if (sudosh_option.priority!=-1)
			mysyslog (sudosh_option.priority, "%s,%s: shell %s doesn't exist.",
				user.from, ttyname (0), user.shell.ptr);
		exit (EXIT_FAILURE);
	}
#else
	user.shell.ptr = user.pw->pw_shell;
#endif /* HAVE_GETUSERSHELL */

	if (loginshell)
		user.shell.ptr = sudosh_option.defshell;


	script.bytes = 0;
	timing.bytes = 0;
	difftime = 0;

	snprintf (script.name, (size_t) BUFSIZ - 1, "%s/%s%c%s%cscript%c%i%c%s",
		sudosh_option.logdir, user.from, sudosh_option.fdl, user.to,
		sudosh_option.fdl, sudosh_option.fdl, (int) now,
		sudosh_option.fdl, rand);
	snprintf (timing.name, (size_t) BUFSIZ - 1, "%s/%s%c%s%ctime%c%i%c%s",
		sudosh_option.logdir, user.from, sudosh_option.fdl, user.to,
		sudosh_option.fdl, sudosh_option.fdl, (int) now,
		sudosh_option.fdl, rand);

	snprintf (start_msg, BUFSIZ - 1,
		"starting session for %s as %s, session id %i, tty %s, shell %s", user.from, user.to,
		(int) now, ttyname (0), user.shell.ptr);

  if(!logtosyslog) {
    struct stat logdir_stat;

    if (((stat(sudosh_option.logdir, &logdir_stat)) == -1) || !(logdir_stat.st_mode & S_IFDIR))
      mklogdir (sudosh_option.logdir);

    if ((stat(sudosh_option.logdir, &logdir_stat)) == -1)
    {
      fprintf(stderr, "Fatal error - logdir %s is invalid: %s\n", sudosh_option.logdir, strerror(errno));
      exit(EXIT_FAILURE);
    }

    if (!(logdir_stat.st_mode & S_IFDIR))
    {
      fprintf(stderr, "logdir '%s' is not a directory.\n", sudosh_option.logdir);
      exit(EXIT_FAILURE);
    }
	  
    if ((script.fd =
		  open (script.name, O_RDWR | O_CREAT | O_EXCL,
		    S_IRUSR | S_IWUSR)) == -1)
	  {
		  perror (script.name);
		  bye (EXIT_FAILURE);
	  }

	  if (fstat (script.fd, &script.stat) == -1)
	  {
		  perror ("fstat script.fd");
		  exit (EXIT_FAILURE);
	  }

	  if ((timing.fd =
		  open (timing.name, O_RDWR | O_CREAT | O_EXCL,
			  S_IRUSR | S_IWUSR)) == -1)
	  {
		  perror (timing.name);
		  bye (EXIT_FAILURE);
	  }

	  if (fstat (timing.fd, &timing.stat) == -1)
	  {
		  perror ("fstat timing.fd");
		  exit (EXIT_FAILURE);
	  }
  }

	if (sudosh_option.priority!=-1)
		  mysyslog (sudosh_option.priority, start_msg);
	rawmode (0);

	if (findms (&pspair) < 0)
	{
		perror ("open pty failed");
		bye (EXIT_FAILURE);
	}

	switch (fork ())
	{
	case 0:
		close (pspair.mfd);
		prepchild (&pspair);
	case -1:
		perror ("fork failed");
		bye (EXIT_FAILURE);
	default:
		close (pspair.sfd);
	}

	setuid (getuid ());

	memset (&sawinch, 0, sizeof sawinch);
	sawinch.sa_handler = newwinsize;
	sawinch.sa_flags = SA_RESTART;
	sigaction (SIGWINCH, &sawinch, (struct sigaction *) 0);

	memset (&saterm, 0, sizeof saterm);
	saterm.sa_handler = bye;
	sigaction (SIGTERM, &sawinch, (struct sigaction *) 0);

	memset (&sachild, 0, sizeof sachild);
	sachild.sa_handler = bye;
	sigaction (SIGCHLD, &sachild, (struct sigaction *) 0);
	signal(SIGALRM,alarm_handler);

	oldtime = time (NULL);
	
	alarm(SECONDS_THROTTLE);
	while (n > 0)
	{
		fd_set readfds;

		if(bytes_written>=sudosh_option.bytespersecond)
		{
			logger(LOG_DEBUG,"Throttling");
			skip_write=1;
		}else {
			if(skip_write && !logtosyslog) {
				 int prompt_size = 4;
				 char prompt[4] = "\n->\0";

				script.bytes += WRITE (script.fd, prompt, prompt_size);

        snprintf (timing.str, BUFSIZ - 1, "%f %i\n", difftime, prompt_size);
        timing.bytes += WRITE (timing.fd, &timing.str, strlen (timing.str));
			}
			skip_write=0;
		}

		FD_ZERO (&readfds);
		FD_SET (pspair.mfd, &readfds);
		FD_SET (0, &readfds);

		gettimeofday ((struct timeval *) &tv, NULL);
		logger(LOG_DEBUG,"About to do/while select 1\n");
		do
		{
			try_select_again=0;
			if (select (pspair.mfd + 1, &readfds, NULL,
				NULL, NULL) < 0)
			{
				error=errno;
				switch(error)
				{
				case EINTR:
					logger(LOG_DEBUG,"select interrupted.\n");
					try_select_again=1;
					break;
				default:
					strerror_r(error,errorstr,BUFSIZ);
		 			logger(LOG_ERR,"select: %s\n",errorstr);
					bye (EXIT_FAILURE);
				}
			}
		}while(try_select_again);
		if (FD_ISSET (pspair.mfd, &readfds))
		{
			logger(LOG_DEBUG,"About to do/while read 1\n");

			do
			{
				try_read_again=0;
				
				if (sizeof (iobuf) < sudosh_option.bytespersecond) {
					n = read (pspair.mfd, iobuf, sizeof (iobuf));
				} else {
					n = read (pspair.mfd, iobuf, sudosh_option.bytespersecond);
				}
				if(n<0)
				{
					error=errno;
					logger(LOG_DEBUG,"Read %d bytes\n",n);
					switch(error)
					{

					case EINTR:
						try_read_again=1;
						break;
					default:
						strerror_r(error,errorstr,BUFSIZ);
		 				logger(LOG_ERR,"read: %s\n",errorstr);
					}
				}
					
			}while(try_read_again);
			logger(LOG_DEBUG,"Read 1 read %d bytes\n",n);

			if (n > 0)
			{
				WRITE (1, iobuf, n);
        if(!logtosyslog)
        {
				  wrote=(skip_write ? 0 : WRITE (script.fd, iobuf, n));
          script.bytes += wrote;
        }

				if (!skip_write && logtosyslog) {

          wrote = n;

					if(syslogcount + n < MAX_SYSLOG_MSG - 1) {
						snprintf(&(syslogbuf[syslogcount]), n+1, "%s", iobuf);
						syslogcount += n;
					}
					else {
						cleansyslog();
            mysyslog (sudosh_option.priority, "[%i]: msg: %s:%s: %s", now, user.from, user.to, syslogbuf);
						memset(syslogbuf, MAX_SYSLOG_MSG, '\0');
						sysloglast = time((time_t *) NULL);

						if(n > MAX_SYSLOG_MSG-1) {
							snprintf(syslogbuf, MAX_SYSLOG_MSG, "%s", iobuf);
							syslogcount = MAX_SYSLOG_MSG;
						}
						else {
							snprintf(syslogbuf, n+1, "%s", iobuf);
							syslogcount = n;
						}
					}
				}

				bytes_written += wrote;
			}


			newtime = tv.tv_sec + (double) tv.tv_usec / 1000000;
			difftime = newtime - oldtime;
			snprintf (timing.str, BUFSIZ - 1, "%f %i\n", difftime, n);
      if(!logtosyslog)
			  timing.bytes +=
				  (skip_write ? 0 : WRITE (timing.fd, &timing.str, strlen (timing.str)));

			// write timing to syslog
			if (!skip_write && logtosyslog) {
				if (timingcount + strlen(timing.str) < MAX_SYSLOG_MSG - 2) {
					snprintf(&(timingbuf[timingcount]), strlen(timing.str)+1, timing.str);
					timingcount += strlen(timing.str);
					timingbuf[timingcount] = ':';
					timingcount++;
				}
				else {
					mysyslog ( sudosh_option.priority, "[%i]: time: %s:%s: %s", now, user.from, user.to, timingbuf );
					memset ( timingbuf, MAX_SYSLOG_MSG, '\0' );
					timinglast = time((time_t *) NULL);

					snprintf(timingbuf, strlen(timing.str)+1, timing.str);
					timingcount = strlen(timing.str);
				}
			}

			oldtime = newtime;
		}

		if (FD_ISSET (0, &readfds))
		{	
			logger(LOG_DEBUG,"About to do/while read 2\n");

			do
			{
				try_read_again=0;
				n = read (0, iobuf, BUFSIZ);
				error=errno;
				if(n<0)
				{
					switch(error)
					{
						case EINTR:
							try_read_again=1;
							break;
						default:
							strerror_r(error,errorstr,BUFSIZ);
							logger(LOG_ERR,"read: %s\n",errorstr);
					}
				}
			}while(try_read_again);
			logger(LOG_DEBUG,"Read 2 read %d bytes\n",n);
			if (n  > 0)
			{
				WRITE (pspair.mfd, iobuf, n);

				// If the user starts typing, reset the counter
				if (skip_write) {
					bytes_written = 0;
				}
			} //end if
		}//end if
	}//end while
	logger(LOG_DEBUG,"fell out of while loop\n");
	bye (EXIT_SUCCESS);
	return (0);
}

void cleansyslog ()
{
	int i;
	char replace = 183;

	for (i = 0; i < syslogcount; i++) {
        // replace any non-ascii characters with char 183
		if ( isascii(syslogbuf[i]) == 0 ) {
			syslogbuf[i] = replace;
		}
	}
}

static int findms (struct pst *p)
{
	char *sname;

	if ((p->mfd = open ("/dev/ptmx", O_RDWR)) == -1)
	{
		if ((p->mfd = open ("/dev/ptc", O_RDWR)) == -1)
		{
			perror ("Cannot open cloning master pty");
			return -1;
		}
	}

	(void) unlockpt (p->mfd);
	(void) grantpt (p->mfd);

	sname = (char *) ptsname (p->mfd);

	if ((p->sfd = open (sname, O_RDWR)) == -1)
	{
		perror ("open slave pty");
		close (p->mfd);
		return -1;
	}

	p->master = (char *) 0;

	p->slave = malloc (strlen (sname) + 1);
	strcpy (p->slave, sname);

#ifdef I_LIST
	if (ioctl (p->sfd, I_LIST, NULL) > 0)
	{
#ifdef I_FIND
		if (ioctl (p->sfd, I_FIND, "ldterm") != 1 &&
			ioctl (p->sfd, I_FIND, "ldtty") != 1)
		{
#ifdef I_PUSH
			(void) ioctl (p->sfd, I_PUSH, "ptem");
			(void) ioctl (p->sfd, I_PUSH, "ldterm");
#endif
		}
#endif
	}
#endif
	return p->mfd;
}

static void prepchild (struct pst *pst)
{
	int i;
	char *b = NULL;
	char newargv[BUFSIZ];
	char *env_list[] =
		{ user.term.str, user.home.str, user.shell.str, user.logname.str,
		user.path.str, NULL
	};

	close (0);
	close (1);
	close (2);

	setsid ();
#ifdef TIOCSCTTY
	(void) ioctl (pst->sfd, TIOCSCTTY, 0);
#endif

	if ((pst->sfd = open (pst->slave, O_RDWR)) < 0)
		exit (EXIT_FAILURE);

	dup (0);
	dup (0);

	for (i = 3; i < 100; ++i)
		close (i);

#ifdef TCSETS
	(void) ioctl (0, TCSETS, &termorig);
#endif
	(void) ioctl (0, TIOCSWINSZ, &winorig);

	setuid (getuid ());

	strncpy (newargv, user.shell.ptr, BUFSIZ - 1);

	if ((b = strrchr (newargv, '/')) == NULL)
		b = newargv;
	*b = '-';


	snprintf (user.shell.str, BUFSIZ - 1, "SHELL=%s", user.shell.ptr);
	snprintf (user.logname.str, BUFSIZ - 1, "LOGNAME=%s", user.to);
	if (!strcmp (user.to, "root"))
		snprintf (user.path.str, BUFSIZ - 1,
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin:");
	else
		snprintf (user.path.str, BUFSIZ - 1,
			"PATH=/usr/bin:/bin:/usr/local/bin:");

#ifdef HAVE_SETPENV
	/* I love AIX - setpenv takes care of everything, including chdir() and the env */
	setpenv (user.to, PENV_INIT, (char **) 0, (char *) 0);
#endif
	if (chdir (user.to_home.str) == -1)
	{
		fprintf (stderr, "Unable to chdir to %s, using /tmp instead.\n",
			user.pw->pw_dir);
		if (chdir ("/tmp") == -1)
		fprintf (stderr, "Unable to chdir to /tmp\n");
	}

	if (sudosh_option.clearenvironment==0)
	execl (user.shell.ptr, b, (char *) 0);
	else
	execle (user.shell.ptr, b, (char *) 0, env_list);

	abort ();
}

static void rawmode (int ttyfd)
{
	static struct termios termnew;

#ifdef TCGETS
	if (ioctl (ttyfd, TCGETS, &termorig) == -1)
	{
		perror ("ioctl TCGETS failed");
		exit (EXIT_FAILURE);
	}
#endif

	if (ioctl (ttyfd, TIOCGWINSZ, &winorig) == -1)
	{
		perror ("ioctl TIOCGWINSZ failed");
		exit (EXIT_FAILURE);
	}

	termnew.c_cc[VEOF] = 1;
	termnew.c_iflag = BRKINT | ISTRIP | IXON | IXANY;
	termnew.c_oflag = 0;
	termnew.c_cflag = termorig.c_cflag;
	termnew.c_lflag &= ~ECHO;

#ifdef TCSETS
	(void) ioctl (ttyfd, TCSETS, &termnew);
#endif
}

static void bye (int signum)
{
#ifdef TCSETS
	(void) ioctl (0, TCSETS, &termorig);
#endif

  if (!logtosyslog) {
  	close (timing.fd);
	  close (script.fd);
  }
  else {
	  if (sudosh_option.priority!=-1) {
		  cleansyslog();
		  mysyslog (sudosh_option.priority, "[%i]: msg: %s:%s: %s", now, user.from, user.to, syslogbuf);
		  mysyslog (sudosh_option.priority, "[%i]: time: %s:%s: %s", now, user.from, user.to, timingbuf);
		  mysyslog (sudosh_option.priority, "stopping session for %s as %s, tty %s, shell %s",
		    user.from, user.to, ttyname(0), user.shell.ptr);
	  }
  }
	exit (signum);
}

static void newwinsize (int signum)
{
	int fd;

	if (ioctl (0, TIOCGWINSZ, &winorig) != -1)
	{
		if ((fd = open (pspair.slave, O_RDWR)) >= 0)
		{
			(void) ioctl (fd, TIOCSWINSZ, &winorig);
			close (fd);
		}
	}
}
/*
void
mysyslog (int pri, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZ + 1];

	va_start (ap, fmt);
	vsnprintf (buf, sizeof (buf), fmt, ap);

	openlog (progname, 0, sudosh_option.facility);
	syslog (pri, "%s", buf);
	closelog ();
}
*/

int do_write (int fd, void *buf, size_t size, char *file, unsigned int line)
{
	char str[BUFSIZ];
	int s;
	int error;
	int try_again=0;

	if (fd < 0)
		return -1;
	do
	{
		try_again=0;
		if ((s = write (fd, buf, size)) < 0)
		{
			error=errno;
			switch(error)
			{
			case EINTR:
				try_again=1;
				break;
			default:
				snprintf (str, BUFSIZ - 1, "%s [%s, line %i]: %s\n",
					progname, file, line, strerror (errno));
				fprintf(stderr,"%s",str);
				exit (error);
			}
		}
	}while(try_again);

	return s;
}

void setlogfile(const char *newlogfile)
{
	strncpy(logfile,newlogfile,PATH_MAX-1);
	logfile[PATH_MAX-1]='\0';
	return;
}

void alarm_handler(int sig)
{
	bytes_written=0;

  if(logtosyslog) {
	  if ( time((time_t *) NULL) - sysloglast > buffertimeout) {
		  if (syslogcount > 0) {
			  cleansyslog();
			  mysyslog (sudosh_option.priority, "[%i]: msg: %s:%s: %s", now, user.from, user.to, syslogbuf);
			  syslogcount=0;
			  memset(syslogbuf, '\0', MAX_SYSLOG_MSG);
		  }
		  sysloglast = time((time_t *) NULL);
	  }

	  if ( time((time_t *) NULL) - timinglast > buffertimeout) {
		  if (timingcount > 0) {
			  mysyslog (sudosh_option.priority, "[%i]: time: %s:%s: %s", now, user.from, user.to, timingbuf);
			  timingcount = 0;
			  memset(timingbuf, '\0', MAX_SYSLOG_MSG);
		  }
		  timinglast = time((time_t *) NULL);
	  }
  }

	logger(LOG_DEBUG,"In alarm handler");
	alarm(SECONDS_THROTTLE);
	return;
}

