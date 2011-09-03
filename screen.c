/* Copyright (c) 1993
 *      Juergen Weigert (jnweiger@immd4.informatik.uni-erlangen.de)
 *      Michael Schroeder (mlschroe@immd4.informatik.uni-erlangen.de)
 * Copyright (c) 1987 Oliver Laumann
#ifdef HAVE_BRAILLE
 * Modified by:
 *      Authors:  Hadi Bargi Rangin  bargi@dots.physics.orst.edu
 *                Bill Barry         barryb@dots.physics.orst.edu
 *                Randy Lundquist    randyl@dots.physics.orst.edu
 *
 * Modifications Copyright (c) 1995 by
 * Science Access Project, Oregon State University.
#endif
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 ****************************************************************
 */

#include "rcs.h"
RCS_ID("$Id: screen.c,v 1.24 1994/09/06 17:00:20 mlschroe Exp $ FAU")


#include <sys/types.h>
#include <ctype.h>

#include <fcntl.h>

#ifdef sgi
# include <sys/sysmacros.h>
#endif

#include <sys/stat.h>
#ifndef sun
# include <sys/ioctl.h>
#endif

#ifndef SIGINT
# include <signal.h>
#endif

#include "config.h"

#ifdef SVR4
# include <sys/stropts.h>
#endif

#if defined(SYSV) && !defined(ISC)
# include <sys/utsname.h>
#endif

#if defined(sequent) || defined(SVR4)
# include <sys/resource.h>
#endif /* sequent || SVR4 */

#ifdef ISC
# include <sys/tty.h>
# include <sys/sioctl.h>
# include <sys/pty.h>
#endif /* ISC */

#if (defined(AUX) || defined(_AUX_SOURCE)) && defined(POSIX)
# include <compat.h>
#endif

#include "screen.h"
#ifdef HAVE_BRAILLE
# include "braille.h"
#endif

#include "patchlevel.h"

/*
 *  At the moment we only need the real password if the
 *  builtin lock is used. Therefore disable SHADOWPW if
 *  we do not really need it (kind of security thing).
 */
#ifndef LOCK
# undef SHADOWPW
#endif

#include <pwd.h>
#ifdef SHADOWPW
# include <shadow.h>
#endif /* SHADOWPW */

#include "logfile.h"	/* islogfile, logfflush */

#ifdef DEBUG
FILE *dfp;
#endif


extern char *blank, *null, Term[], screenterm[], **environ, Termcap[];
int force_vt = 1, assume_LP = 0;
int VBellWait, MsgWait, MsgMinWait, SilenceWait;

extern struct plop plop_tab[];
extern struct user *users;
extern struct display *displays, *display; 

/* tty.c */
extern int intrc;


extern int visual_bell;
#ifdef COPY_PASTE
extern unsigned char mark_key_tab[];
#endif
extern char version[];
extern char DefaultShell[];


char *ShellProg;
char *ShellArgs[2];

extern struct NewWindow nwin_undef, nwin_default, nwin_options;

static struct passwd *getpwbyname __P((char *, struct passwd *));
static void  SigChldHandler __P((void));
static sigret_t SigChld __P(SIGPROTOARG);
static sigret_t SigInt __P(SIGPROTOARG);
static sigret_t CoreDump __P(SIGPROTOARG);
static sigret_t FinitHandler __P(SIGPROTOARG);
static void  DoWait __P((void));
static void  serv_read_fn __P((struct event *, char *));
static void  serv_select_fn __P((struct event *, char *));
static void  logflush_fn __P((struct event *, char *));
static int   IsSymbol __P((char *, char *));
#ifdef DEBUG
static void  fds __P((void));
#endif

int nversion;	/* numerical version, used for secondary DA */

/* the attacher */
struct passwd *ppp;
char *attach_tty;
char *attach_term;
char *LoginName;
struct mode attach_Mode;

char SockPath[MAXPATHLEN + 2 * MAXSTR];
char *SockName;			/* SockName is pointer in SockPath */
char *SockMatch = NULL;		/* session id command line argument */
int ServerSocket = -1;
struct event serv_read;
struct event serv_select;
struct event logflushev;

char **NewEnv = NULL;

char *RcFileName = NULL;
char *home;

char *screenlogfile;			/* filename layout */
int log_flush = 10;           		/* flush interval in seconds */
int logtstamp_on = 0;			/* tstamp disabled */
char *logtstamp_string;			/* stamp layout */
int logtstamp_after = 120;		/* first tstamp after 120s */
char *hardcopydir = NULL;
char *BellString;
char *VisualBellString;
char *ActivityString;
#ifdef COPY_PASTE
char *BufferFile;
#endif
#ifdef POW_DETACH
char *PowDetachString;
#endif
char *hstatusstring;
char *captionstring;
int auto_detach = 1;
int iflag, rflag, dflag, lsflag, quietflag, wipeflag, xflag;
int adaptflag;

#ifdef MULTIUSER
char *multi;
char *multi_home;
int multi_uid;
int own_uid;
int multiattach;
int tty_mode;
int tty_oldmode = -1;
#endif

char HostName[MAXSTR];
int MasterPid;
int real_uid, real_gid, eff_uid, eff_gid;
int default_startup;
int ZombieKey_destroy, ZombieKey_resurrect;
char *preselect = NULL;		/* only used in Attach() */

#ifdef NETHACK
int nethackflag = 0;
#endif
#ifdef MAPKEYS
int maptimeout = 300000;
#endif


struct layer *flayer;
struct win *fore;
struct win *windows;
struct win *console_window;



/*
 * Do this last
 */
#include "extern.h"

char strnomem[] = "Out of memory.";


static int InterruptPlease;
static int GotSigChld;

static int 
lf_secreopen(name, wantfd, l)
char *name;
int wantfd;
struct logfile *l;
{
  int got_fd;

  close(wantfd);
  if (((got_fd = secopen(name, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) ||
      lf_move_fd(got_fd, wantfd) < 0)
    {
      logfclose(l);
      debug1("lf_secreopen: failed for %s\n", name);
      return -1;
    }
  l->st->st_ino = l->st->st_dev = 0;
  debug2("lf_secreopen: %d = %s\n", wantfd, name);
  return 0;
}

/********************************************************************/
/********************************************************************/
/********************************************************************/


struct passwd *
getpwbyname(name, ppp)
char *name;
struct passwd *ppp;
{
  int n;
#ifdef SHADOWPW
  struct spwd *sss = NULL;
  static char *spw = NULL;
#endif
 
  if (!(ppp = getpwnam(name)))
    return NULL;

  /* Do password sanity check..., allow ##user for SUN_C2 security */
#ifdef SHADOWPW
pw_try_again:
#endif
  n = 0;
  if (ppp->pw_passwd[0] == '#' && ppp->pw_passwd[1] == '#' &&
      strcmp(ppp->pw_passwd + 2, ppp->pw_name) == 0)
    n = 13;
  for (; n < 13; n++)
    {
      char c = ppp->pw_passwd[n];
      if (!(c == '.' || c == '/' ||
	    (c >= '0' && c <= '9') || 
	    (c >= 'a' && c <= 'z') || 
	    (c >= 'A' && c <= 'Z'))) 
	break;
    }

#ifdef SHADOWPW
  /* try to determine real password */
  if (n < 13 && sss == 0)
    {
      sss = getspnam(ppp->pw_name);
      if (sss)
	{
	  if (spw)
	    free(spw);
	  ppp->pw_passwd = spw = SaveStr(sss->sp_pwdp);
	  endspent();	/* this should delete all buffers ... */
	  goto pw_try_again;
	}
      endspent();	/* this should delete all buffers ... */
    }
#endif
  if (n < 13)
    ppp->pw_passwd = 0;
#ifdef linux
  if (ppp->pw_passwd && strlen(ppp->pw_passwd) == 13 + 11)
    ppp->pw_passwd[13] = 0;	/* beware of linux's long passwords */
#endif

  return ppp;
}


int
main(ac, av)
int ac;
char **av;
{
  register int n;
  char *ap;
  char *av0;
  char socknamebuf[2 * MAXSTR];
  int mflag = 0;
  char *myname = (ac == 0) ? "screen" : av[0];
  char *SockDir;
  struct stat st;
#ifdef _MODE_T			/* (jw) */
  mode_t oumask;
#else
  int oumask;
#endif
#if defined(SYSV) && !defined(ISC)
  struct utsname utsnam;
#endif
  struct NewWindow nwin;
  int detached = 0;		/* start up detached */
#ifdef MULTIUSER
  char *sockp;
#endif

#if (defined(AUX) || defined(_AUX_SOURCE)) && defined(POSIX)
  setcompat(COMPAT_POSIX|COMPAT_BSDPROT); /* turn on seteuid support */
#endif
#if defined(sun) && defined(SVR4)
  {
    /* Solaris' login blocks SIGHUP! This is _very bad_ */
    sigset_t sset;
    sigemptyset(&sset);
    sigprocmask(SIG_SETMASK, &sset, 0);
  }
#endif

  /*
   *  First, close all unused descriptors
   *  (otherwise, we might have problems with the select() call)
   */
  closeallfiles(0);
#ifdef DEBUG
  opendebug(1, 0);
#endif
  sprintf(version, "%d.%.2d.%.2d%s (%s) %s", REV, VERS,
	  PATCHLEVEL, STATE, ORIGIN, DATE);
  nversion = REV * 10000 + VERS * 100 + PATCHLEVEL;
  debug2("-- screen debug started %s (%s)\n", *av, version);
#ifdef POSIX
  debug("POSIX\n");
#endif
#ifdef TERMIO
  debug("TERMIO\n");
#endif
#ifdef SYSV
  debug("SYSV\n");
#endif
#ifdef SYSVSIGS
  debug("SYSVSIGS\n");
#endif
#ifdef NAMEDPIPE
  debug("NAMEDPIPE\n");
#endif
#if defined(SIGWINCH) && defined(TIOCGWINSZ)
  debug("Window changing enabled\n");
#endif
#ifdef HAVE_SETREUID
  debug("SETREUID\n");
#endif
#ifdef HAVE_SETEUID
  debug("SETEUID\n");
#endif
#ifdef hpux
  debug("hpux\n");
#endif
#ifdef USEBCOPY
  debug("USEBCOPY\n");
#endif
#ifdef UTMPOK
  debug("UTMPOK\n");
#endif
#ifdef LOADAV
  debug("LOADAV\n");
#endif
#ifdef NETHACK
  debug("NETHACK\n");
#endif
#ifdef TERMINFO
  debug("TERMINFO\n");
#endif
#ifdef SHADOWPW
  debug("SHADOWPW\n");
#endif
#ifdef NAME_MAX
  debug1("NAME_MAX = %d\n", NAME_MAX);
#endif

  BellString = SaveStr("Bell in window %n");
  VisualBellString = SaveStr("   Wuff,  Wuff!!  ");
  ActivityString = SaveStr("Activity in window %n");
  screenlogfile = SaveStr("screenlog.%n");
  logtstamp_string = SaveStr("-- %n:%t -- time-stamp -- %M/%d/%y %c:%s --\n");
  hstatusstring = SaveStr("%h");
  captionstring = SaveStr("%3n %t");
#ifdef COPY_PASTE
  BufferFile = SaveStr(DEFAULT_BUFFERFILE);
#endif
  ShellProg = NULL;
#ifdef POW_DETACH
  PowDetachString = 0;
#endif
  default_startup = (ac > 1) ? 0 : 1;
  adaptflag = 0;
  VBellWait = VBELLWAIT;
  MsgWait = MSGWAIT;
  MsgMinWait = MSGMINWAIT;
  SilenceWait = SILENCEWAIT;
#ifdef HAVE_BRAILLE
  InitBraille();
#endif

#ifdef COPY_PASTE
  CompileKeys((char *)NULL, mark_key_tab);
#endif
  nwin = nwin_undef;
  nwin_options = nwin_undef;
  strcpy(screenterm, "screen");

  logreopen_register(lf_secreopen);

  av0 = *av;
  /* if this is a login screen, assume -RR */
  if (*av0 == '-')
    {
      rflag = 4;
#ifdef MULTI
      xflag = 1;
#else
      dflag = 1;
#endif
      ShellProg = SaveStr(DefaultShell); /* to prevent nasty circles */
    }
  while (ac > 0)
    {
      ap = *++av;
      if (--ac > 0 && *ap == '-')
	{
	  if (ap[1] == '-' && ap[2] == 0)
	    {
	      av++;
	      ac--;
	      break;
	    }
	  while (ap && *ap && *++ap)
	    {
	      switch (*ap)
		{
		case 'a':
		  nwin_options.aflag = 1;
		  break;
		case 'A':
		  adaptflag = 1;
		  break;
		case 'p': /* preselect */
		  if (*++ap)
		    preselect = ap;
		  else
		    {
		      if (!--ac)
			exit_with_usage(myname, "Specify a window to preselect with -p", NULL);
		      preselect = *++av;
		    }
		  ap = NULL;
		  break;
#ifdef HAVE_BRAILLE
		case 'B':
		  bd.bd_start_braille = 1;
		  break;
#endif
		case 'c':
		  if (*++ap)
		    RcFileName = ap;
		  else
		    {
		      if (--ac == 0)
			exit_with_usage(myname, "Specify an alternate rc-filename with -c", NULL);
		      RcFileName = *++av;
		    }
		  ap = NULL;
		  break;
		case 'e':
		  if (!*++ap)
		    {
		      if (--ac == 0)
			exit_with_usage(myname, "Specify command characters with -e", NULL);
		      ap = *++av;
		    }
		  if (ParseEscape(NULL, ap))
		    Panic(0, "Two characters are required with -e option, not '%s'.", ap);
		  ap += 3; /* estimated size of notation */
		  break;
		case 'f':
		  ap++;
		  switch (*ap++)
		    {
		    case 'n':
		    case '0':
		      nwin_options.flowflag = FLOW_NOW * 0;
		      break;
		    case '\0':
		      ap--;
		      /* FALLTHROUGH */
		    case 'y':
		    case '1':
		      nwin_options.flowflag = FLOW_NOW * 1;
		      break;
		    case 'a':
		      nwin_options.flowflag = FLOW_AUTOFLAG;
		      break;
		    default:
		      exit_with_usage(myname, "Unknown flow option -%s", --ap);
		    }
		  break;
		case 'h':
		  if (--ac == 0)
		    exit_with_usage(myname, NULL, NULL);
		  nwin_options.histheight = atoi(*++av);
		  if (nwin_options.histheight < 0)
		    exit_with_usage(myname, "-h: %s: negative scrollback size?", *av);
		  break;
		case 'i':
		  iflag = 1;
		  break;
		case 't': /* title, the former AkA == -k */
		  if (--ac == 0)
		    exit_with_usage(myname, "Specify a new window-name with -t", NULL);
		  nwin_options.aka = *++av;
		  break;
		case 'l':
		  ap++;
		  switch (*ap++)
		    {
		    case 'n':
		    case '0':
		      nwin_options.lflag = 0;
		      break;
		    case '\0':
		      ap--;
		      /* FALLTHROUGH */
		    case 'y':
		    case '1':
		      nwin_options.lflag = 1;
		      break;
		    case 's':	/* -ls */
		    case 'i':	/* -list */
		      lsflag = 1;
		      if (ac > 1 && !SockMatch)
			{
			  SockMatch = *++av;
			  ac--;
			}
		      ap = NULL;
		      break;
		    default:
		      exit_with_usage(myname, "%s: Unknown suboption to -l", --ap);
		    }
		  break;
		case 'w':
		  lsflag = 1;
		  wipeflag = 1;
		  if (ac > 1 && !SockMatch)
		    {
		      SockMatch = *++av;
		      ac--;
		    }
		  break;
		case 'L':
		  nwin_options.Lflag = 1;
		  break;
		case 'm':
		  mflag = 1;
		  break;
		case 'O':		/* to be (or not to be?) deleted. jw. */
		  force_vt = 0;
		  break;
		case 'T':
		  if (--ac == 0)
		    exit_with_usage(myname, "Specify terminal-type with -T", NULL);
		  if (strlen(*++av) < 20)
		    strcpy(screenterm, *av);
		  else
		    Panic(0, "-T: terminal name too long. (max. 20 char)");
		  nwin_options.term = screenterm;
		  break;
		case 'q':
		  quietflag = 1;
		  break;
		case 'r':
		case 'R':
#ifdef MULTI
		case 'x':
#endif
		  if (ac > 1 && *av[1] != '-' && !SockMatch)
		    {
		      SockMatch = *++av;
		      ac--;
		      debug2("rflag=%d, SockMatch=%s\n", dflag, SockMatch);
		    }
#ifdef MULTI
		  if (*ap == 'x')
		    xflag = 1;
#endif
		  if (rflag)
		    rflag = 2;
		  rflag += (*ap == 'R') ? 2 : 1;
		  break;
#ifdef REMOTE_DETACH
		case 'd':
		  dflag = 1;
		  /* FALLTHROUGH */
		case 'D':
		  if (!dflag)
		    dflag = 2;
		  if (ac == 2)
		    {
		      if (*av[1] != '-' && !SockMatch)
			{
			  SockMatch = *++av;
			  ac--;
			  debug2("dflag=%d, SockMatch=%s\n", dflag, SockMatch);
			}
		    }
		  break;
#endif
		case 's':
		  if (--ac == 0)
		    exit_with_usage(myname, "Specify shell with -s", NULL);
		  if (ShellProg)
		    free(ShellProg);
		  ShellProg = SaveStr(*++av);
		  debug1("ShellProg: '%s'\n", ShellProg);
		  break;
		case 'S':
		  if (!SockMatch)
		    {
		      if (--ac == 0)
			exit_with_usage(myname, "Specify session-name with -S", NULL);
		      SockMatch = *++av;
		    }
		  if (!*SockMatch)
		    exit_with_usage(myname, "Empty session-name?", NULL);
		  break;
		case 'v':
		  Panic(0, "Screen version %s", version);
		  /* NOTREACHED */
		default:
		  exit_with_usage(myname, "Unknown option %s", --ap);
		}
	    }
	}
      else
	break;
    }
  if (SockMatch && strlen(SockMatch) >= MAXSTR)
    Panic(0, "Ridiculously long socketname - try again.");
  if (dflag && mflag && !(rflag || xflag))
    detached = 1;
  nwin = nwin_options;
  if (ac)
    nwin.args = av;
  real_uid = getuid();
  real_gid = getgid();
  eff_uid = geteuid();
  eff_gid = getegid();
  if (eff_uid != real_uid)
    {		
      /* if running with s-bit, we must install a special signal
       * handler routine that resets the s-bit, so that we get a
       * core file anyway.
       */
#ifdef SIGBUS /* OOPS, linux has no bus errors! */
      signal(SIGBUS, CoreDump);
#endif /* SIGBUS */
      signal(SIGSEGV, CoreDump);
    }

  /* make the write() calls return -1 on all errors */
#ifdef SIGXFSZ
  /*
   * Ronald F. Guilmette, Oct 29 '94, bug-gnu-utils@prep.ai.mit.edu:
   * It appears that in System V Release 4, UNIX, if you are writing
   * an output file and you exceed the currently set file size limit,
   * you _don't_ just get the call to `write' returning with a
   * failure code.  Rather, you get a signal called `SIGXFSZ' which,
   * if neither handled nor ignored, will cause your program to crash
   * with a core dump.
   */
  signal(SIGXFSZ, SIG_IGN);
#endif /* SIGXFSZ */

#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

  if (!ShellProg)
    {
      register char *sh;

      sh = getenv("SHELL");
      ShellProg = SaveStr(sh ? sh : DefaultShell);
    }
  ShellArgs[0] = ShellProg;
  home = getenv("HOME");

#ifdef NETHACK
  if (!(nethackflag = (getenv("NETHACKOPTIONS") != NULL)))
    {
      char nethackrc[MAXPATHLEN];

      if (home && (strlen(home) < (MAXPATHLEN - 20)))
        {
	  sprintf(nethackrc,"%s/.nethackrc", home);
	  nethackflag = !access(nethackrc, F_OK);
	}
    }
#endif

#ifdef MULTIUSER
  own_uid = multi_uid = real_uid;
  if (SockMatch && (sockp = index(SockMatch, '/')))
    {
      if (eff_uid)
        Panic(0, "Must run suid root for multiuser support.");
      *sockp = 0;
      multi = SockMatch;
      SockMatch = sockp + 1;
      if (*multi)
	{
	  struct passwd *mppp;
	  if ((mppp = getpwnam(multi)) == (struct passwd *)0)
	    Panic(0, "Cannot identify account '%s'.", multi);
	  multi_uid = mppp->pw_uid;
	  multi_home = SaveStr(mppp->pw_dir);
          if (strlen(multi_home) > MAXPATHLEN - 10)
	    Panic(0, "home directory path too long");
# ifdef MULTI
          /* always fake multi attach mode */
	  if (rflag || lsflag)
	    xflag = 1;
# endif /* MULTI */
	  detached = 0;
	  multiattach = 1;
	}
    }
  if (SockMatch && *SockMatch == 0)
    SockMatch = 0;
#endif /* MULTIUSER */

  if ((LoginName = getlogin()) && LoginName[0] != '\0')
    {
      if ((ppp = getpwnam(LoginName)) != (struct passwd *) 0)
	if (ppp->pw_uid != real_uid)
	  ppp = (struct passwd *) 0;
    }
  if (ppp == 0)
    {
      if ((ppp = getpwuid(real_uid)) == 0)
        {
	  Panic(0, "getpwuid() can't identify your account!");
	  exit(1);
        }
      LoginName = ppp->pw_name;
    }
  LoginName = SaveStr(LoginName);

  ppp = getpwbyname(LoginName, ppp);

#if !defined(SOCKDIR) && defined(MULTIUSER)
  if (multi && !multiattach)
    {
      if (home && strcmp(home, ppp->pw_dir))
        Panic(0, "$HOME must match passwd entry for multiuser screens.");
    }
#endif

  if (home == 0 || *home == '\0')
    home = ppp->pw_dir;
  if (strlen(LoginName) > 20)
    Panic(0, "LoginName too long - sorry.");
#ifdef MULTIUSER
  if (multi && strlen(multi) > 20)
    Panic(0, "Screen owner name too long - sorry.");
#endif
  if (strlen(home) > MAXPATHLEN - 25)
    Panic(0, "$HOME too long - sorry.");

  attach_tty = "";
  if (!detached && !lsflag && !(dflag && !mflag && !rflag && !xflag))
    {
      /* ttyname implies isatty */
      if (!(attach_tty = ttyname(0)))
        Panic(0, "Must be connected to a terminal.");
      if (strlen(attach_tty) >= MAXPATHLEN)
	Panic(0, "TtyName too long - sorry.");
      if (stat(attach_tty, &st))
	Panic(errno, "Cannot access '%s'", attach_tty);
#ifdef MULTIUSER
      tty_mode = (int)st.st_mode & 0777;
#endif
      if ((n = secopen(attach_tty, O_RDWR, 0)) < 0)
	Panic(0, "Cannot open your terminal '%s' - please check.", attach_tty);
      close(n);
      debug1("attach_tty is %s\n", attach_tty);
      if ((attach_term = getenv("TERM")) == 0 || *attach_term == 0)
	Panic(0, "Please set a terminal type.");
      if (strlen(attach_term) > sizeof(D_termname) - 1)
	Panic(0, "$TERM too long - sorry.");
      GetTTY(0, &attach_Mode);
#ifdef DEBUGGGGGGGGGGGGGGG
      DebugTTY(&attach_Mode);
#endif /* DEBUG */
    }
  
#ifdef _MODE_T
  oumask = umask(0);		/* well, unsigned never fails? jw. */
#else
  if ((oumask = umask(0)) == -1)
    Panic(errno, "Cannot change umask to zero");
#endif
  SockDir = getenv("SCREENDIR");
  if (SockDir)
    {
      if (strlen(SockDir) >= MAXPATHLEN - 1)
	Panic(0, "Ridiculously long $SCREENDIR - try again.");
#ifdef MULTIUSER
      if (multi)
	Panic(0, "No $SCREENDIR with multi screens, please.");
#endif
    }
#ifdef MULTIUSER
  if (multiattach)
    {
# ifndef SOCKDIR
      sprintf(SockPath, "%s/.screen", multi_home);
      SockDir = SockPath;
# else
      SockDir = SOCKDIR;
      sprintf(SockPath, "%s/S-%s", SockDir, multi);
# endif
    }
  else
#endif
    {
#ifndef SOCKDIR
      if (SockDir == 0)
	{
	  sprintf(SockPath, "%s/.screen", home);
	  SockDir = SockPath;
	}
#endif
      if (SockDir)
	{
	  if (access(SockDir, F_OK))
	    {
	      debug1("SockDir '%s' missing ...\n", SockDir);
	      if (UserContext() > 0)
		{
		  if (mkdir(SockDir, 0700))
		    UserReturn(0);
		  UserReturn(1);
		}
	      if (UserStatus() <= 0)
		Panic(0, "Cannot make directory '%s'.", SockDir);
	    }
	  if (SockDir != SockPath)
	    strcpy(SockPath, SockDir);
	}
#ifdef SOCKDIR
      else
	{
	  SockDir = SOCKDIR;
	  if (lstat(SockDir, &st))
	    {
	      n = (eff_uid == 0) ? 0755 :
	          (eff_gid != real_gid) ? 0775 :
#ifdef S_ISVTX
		  0777|S_ISVTX;
#else
		  0777;
#endif
	      if (mkdir(SockDir, eff_uid ? 0777 : 0755) == -1)
		Panic(errno, "Cannot make directory '%s'", SockDir);
	    }
	  else
	    {
	      if (!S_ISDIR(st.st_mode))
		Panic(0, "'%s' must be a directory.", SockDir);
              if (eff_uid == 0 && real_uid && st.st_uid != eff_uid)
		Panic(0, "Directory '%s' must be owned by root.", SockDir);
	      n = (eff_uid == 0 && (real_uid || (st.st_mode & 0777) != 0777)) ? 0755 :
	          (eff_gid == st.st_gid && eff_gid != real_gid) ? 0775 :
		  0777;
	      if ((st.st_mode & 0777) != n)
		Panic(0, "Directory '%s' must have mode %03o.", SockDir, n);
	    }
	  sprintf(SockPath, "%s/S-%s", SockDir, LoginName);
	  if (access(SockPath, F_OK))
	    {
	      if (mkdir(SockPath, 0700) == -1)
		Panic(errno, "Cannot make directory '%s'", SockPath);
	      (void) chown(SockPath, real_uid, real_gid);
	    }
	}
#endif
    }

  if (stat(SockPath, &st) == -1)
    Panic(errno, "Cannot access %s", SockPath);
  else
  if (!S_ISDIR(st.st_mode))
    Panic(0, "%s is not a directory.", SockPath);
#ifdef MULTIUSER
  if (multi)
    {
      if (st.st_uid != multi_uid)
	Panic(0, "%s is not the owner of %s.", multi, SockPath);
    }
  else
#endif
    {
      if (st.st_uid != real_uid)
	Panic(0, "You are not the owner of %s.", SockPath);
    }
  if ((st.st_mode & 0777) != 0700)
    Panic(0, "Directory %s must have mode 700.", SockPath);
  SockName = SockPath + strlen(SockPath) + 1;
  *SockName = 0;
  (void) umask(oumask);
  debug2("SockPath: %s  SockMatch: %s\n", SockPath, SockMatch ? SockMatch : "NULL");

#if defined(SYSV) && !defined(ISC)
  if (uname(&utsnam) == -1)
    Panic(errno, "uname");
  strncpy(HostName, utsnam.nodename, sizeof(utsnam.nodename) < MAXSTR ? sizeof(utsnam.nodename) : MAXSTR - 1);
  HostName[sizeof(utsnam.nodename) < MAXSTR ? sizeof(utsnam.nodename) : MAXSTR - 1] = '\0';
#else
  (void) gethostname(HostName, MAXSTR);
  HostName[MAXSTR - 1] = '\0';
#endif
  if ((ap = index(HostName, '.')) != NULL)
    *ap = '\0';

  if (lsflag)
    {
      int i, fo, oth;

#ifdef MULTIUSER
      if (multi)
	real_uid = multi_uid;
#endif
      setuid(real_uid);
      setgid(real_gid);
      eff_uid = real_uid;
      eff_gid = real_gid;
      i = FindSocket((int *)NULL, &fo, &oth, SockMatch);
      if (quietflag)
        exit(8 + (fo ? ((oth || i) ? 2 : 1) : 0) + i);
      if (fo == 0)
        Panic(0, "No Sockets found in %s.\n", SockPath);
      Panic(0, "%d Socket%s in %s.\n", fo, fo > 1 ? "s" : "", SockPath);
      /* NOTREACHED */
    }
  signal(SIG_BYE, AttacherFinit);	/* prevent races */
  if (rflag || xflag)
    {
      debug("screen -r: - is there anybody out there?\n");
      if (Attach(MSG_ATTACH))
	{
	  Attacher();
	  /* NOTREACHED */
	}
      debug("screen -r: backend not responding -- still crying\n");
    }
  else if (dflag && !mflag)
    {
      (void) Attach(MSG_DETACH);
      Msg(0, "[%s %sdetached.]\n", SockName, (dflag > 1 ? "power " : ""));
      eexit(0);
      /* NOTREACHED */
    }
  if (!SockMatch && !mflag)
    {
      register char *sty;

      if ((sty = getenv("STY")) != 0 && *sty != '\0')
	{
	  setuid(real_uid);
	  setgid(real_gid);
	  eff_uid = real_uid;
	  eff_gid = real_gid;
	  nwin_options.args = av;
	  SendCreateMsg(sty, &nwin);
	  exit(0);
	  /* NOTREACHED */
	}
    }
  nwin_compose(&nwin_default, &nwin_options, &nwin_default);

  if (DefaultEsc == -1)
    DefaultEsc = Ctrl('a');
  if (DefaultMetaEsc == -1)
    DefaultMetaEsc = 'a';

  if (!detached || dflag != 2)
    MasterPid = fork();
  else
    MasterPid = 0;

  switch (MasterPid)
    {
    case -1:
      Panic(errno, "fork");
      /* NOTREACHED */
#ifdef FORKDEBUG
    default:
      break;
    case 0:
      MasterPid = getppid();
#else
    case 0:
      break;
    default:
#endif
      if (detached)
        exit(0);
      if (SockMatch)
	sprintf(socknamebuf, "%d.%s", MasterPid, SockMatch);
      else
	sprintf(socknamebuf, "%d.%s.%s", MasterPid, stripdev(attach_tty), HostName);
      for (ap = socknamebuf; *ap; ap++)
	if (*ap == '/')
	  *ap = '-';
#ifdef NAME_MAX
      if (strlen(socknamebuf) > NAME_MAX)
        socknamebuf[NAME_MAX] = 0;
#endif
      sprintf(SockPath + strlen(SockPath), "/%s", socknamebuf);
      setuid(real_uid);
      setgid(real_gid);
      eff_uid = real_uid;
      eff_gid = real_gid;
      Attacher();
      /* NOTREACHED */
    }

  ap = av0 + strlen(av0) - 1;
  while (ap >= av0)
    {
      if (!strncmp("screen", ap, 6))
	{
	  strncpy(ap, "SCREEN", 6); /* name this process "SCREEN-BACKEND" */
	  break;
	}
      ap--;
    }
  if (ap < av0)
    *av0 = 'S';

#ifdef DEBUG
  {
    char buf[256];

    if (dfp && dfp != stderr)
      fclose(dfp);
    sprintf(buf, "%s/SCREEN.%d", DEBUGDIR, (int)getpid());
    if ((dfp = fopen(buf, "w")) == NULL)
      dfp = stderr;
    else
      (void) chmod(buf, 0666);
  }
#endif
  if (!detached)
    {
      /* reopen tty. must do this, because fd 0 may be RDONLY */
      if ((n = secopen(attach_tty, O_RDWR, 0)) < 0)
	Panic(0, "Cannot reopen '%s' - please check.", attach_tty);
    }
  else
    n = -1;
  freopen("/dev/null", "r", stdin);
  freopen("/dev/null", "w", stdout);

#ifdef DEBUG
  if (dfp != stderr)
#endif
  freopen("/dev/null", "w", stderr);
  debug("-- screen.back debug started\n");

  /* 
   * This guarantees that the session owner is listed, even when we
   * start detached. From now on we should not refer to 'LoginName'
   * any more, use users->u_name instead.
   */
  if (UserAdd(LoginName, (char *)0, (struct user **)0) < 0)
    Panic(0, "Could not create user info");
  if (!detached)
    {
#ifdef FORKDEBUG
      if (MakeDisplay(LoginName, attach_tty, attach_term, n, MasterPid, &attach_Mode) == 0)
#else
      if (MakeDisplay(LoginName, attach_tty, attach_term, n, getppid(), &attach_Mode) == 0)
#endif
	Panic(0, "Could not alloc display");
    }

  if (SockMatch)
    {
      /* user started us with -S option */
      sprintf(socknamebuf, "%d.%s", (int)getpid(), SockMatch);
    }
  else
    {
      sprintf(socknamebuf, "%d.%s.%s", (int)getpid(), stripdev(attach_tty),
	      HostName);
    }
  for (ap = socknamebuf; *ap; ap++)
    if (*ap == '/')
      *ap = '-';
#ifdef NAME_MAX
  if (strlen(socknamebuf) > NAME_MAX)
    {
      debug2("Socketname %s truncated to %d chars\n", socknamebuf, NAME_MAX);
      socknamebuf[NAME_MAX] = 0;
    }
#endif
  sprintf(SockPath + strlen(SockPath), "/%s", socknamebuf);
  
  ServerSocket = MakeServerSocket();
  InitKeytab();
#ifdef ETCSCREENRC
# ifdef ALLOW_SYSSCREENRC
  if ((ap = getenv("SYSSCREENRC")))
    StartRc(ap);
  else
# endif
    StartRc(ETCSCREENRC);
#endif
  StartRc(RcFileName);
# ifdef UTMPOK
#  ifndef UTNOKEEP
  InitUtmp(); 
#  endif /* UTNOKEEP */
# endif /* UTMPOK */
  if (display)
    {
      if (InitTermcap(0, 0))
	{
	  debug("Could not init termcap - exiting\n");
	  fcntl(D_userfd, F_SETFL, 0);	/* Flush sets FNBLOCK */
	  freetty();
	  if (D_userpid)
	    Kill(D_userpid, SIG_BYE);
	  eexit(1);
	}
      MakeDefaultCanvas();
      InitTerm(0);
#ifdef UTMPOK
      RemoveLoginSlot();
#endif
    }
  else
    MakeTermcap(1);
#ifdef LOADAV
  InitLoadav();
#endif /* LOADAV */
  MakeNewEnv();
  signal(SIGHUP, SigHup);
  signal(SIGINT, FinitHandler);
  signal(SIGQUIT, FinitHandler);
  signal(SIGTERM, FinitHandler);
#ifdef BSDJOBS
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
#endif

  if (display)
    {
      brktty(D_userfd);
      SetMode(&D_OldMode, &D_NewMode, display->d_flow, iflag);
      /* Note: SetMode must be called _before_ FinishRc. */
      SetTTY(D_userfd, &D_NewMode);
      if (fcntl(D_userfd, F_SETFL, FNBLOCK))
	Msg(errno, "Warning: NBLOCK fcntl failed");
    }
  else
    brktty(-1);		/* just try */
#ifdef ETCSCREENRC
# ifdef ALLOW_SYSSCREENRC
  if ((ap = getenv("SYSSCREENRC")))
    FinishRc(ap);
  else
# endif
    FinishRc(ETCSCREENRC);
#endif
  FinishRc(RcFileName);

  debug2("UID %d  EUID %d\n", (int)getuid(), (int)geteuid());
  if (windows == NULL)
    {
      debug("We open one default window, as screenrc did not specify one.\n");
      if (MakeWindow(&nwin) == -1)
	{
	  Msg(0, "Sorry, could not find a PTY.");
	  sleep(5);
	  Finit(0);
	  /* NOTREACHED */
	}
    }

#ifdef HAVE_BRAILLE
  StartBraille();
#endif
  
  if (display && default_startup)
    display_copyright();
  signal(SIGCHLD, SigChld);
  signal(SIGINT, SigInt);
  if (rflag && (rflag & 1) == 0)
    {
      Msg(0, "New screen...");
      rflag = 0;
    }

  serv_read.type = EV_READ;
  serv_read.fd = ServerSocket;
  serv_read.handler = serv_read_fn;
  evenq(&serv_read);

  serv_select.pri = -10;
  serv_select.type = EV_ALWAYS;
  serv_select.handler = serv_select_fn;
  evenq(&serv_select);

  logflushev.type = EV_TIMEOUT;
  logflushev.handler = logflush_fn;

  sched();
  /* NOTREACHED */
  return 0;
}

void
WindowDied(p)
struct win *p;
{
  if (ZombieKey_destroy)
    {
      char buf[100], *s;
      time_t now;

      (void) time(&now);
      s = ctime(&now);
      if (s && *s)
        s[strlen(s) - 1] = '\0';
      debug3("window %d (%s) going into zombie state fd %d",
	     p->w_number, p->w_title, p->w_ptyfd);
#ifdef UTMPOK
      if (p->w_slot != (slot_t)0 && p->w_slot != (slot_t)-1)
	{
	  RemoveUtmp(p);
	  p->w_slot = 0;	/* "detached" */
	}
#endif
      CloseDevice(p);

      p->w_pid = 0;
      ResetWindow(p);
      /* p->w_y = p->w_bot; */
      p->w_y = MFindUsedLine(p, p->w_bot, 1);
      sprintf(buf, "\n\r=== Window terminated (%s) ===", s ? s : "?");
      WriteString(p, buf, strlen(buf));
    }
  else
    KillWindow(p);
#ifdef UTMPOK
  CarefulUtmp();
#endif
}

static void
SigChldHandler()
{
  struct stat st;
#ifdef DEBUG
  fds();
#endif
  while (GotSigChld)
    {
      GotSigChld = 0;
      DoWait();
#ifdef SYSVSIGS
      signal(SIGCHLD, SigChld);
#endif
    }
  if (stat(SockPath, &st) == -1)
    {
      debug1("SigChldHandler: Yuck! cannot stat '%s'\n", SockPath);
      if (!RecoverSocket())
	{
	  debug("SCREEN cannot recover from corrupt Socket, bye\n");
	  Finit(1);
	}
      else
	debug1("'%s' reconstructed\n", SockPath);
    }
  else
    debug2("SigChldHandler: stat '%s' o.k. (%03o)\n", SockPath, (int)st.st_mode);
}

static sigret_t
SigChld SIGDEFARG
{
  debug("SigChld()\n");
  GotSigChld = 1;
  SIGRETURN;
}

sigret_t
SigHup SIGDEFARG
{
  if (display == 0)
    return;
  debug("SigHup()\n");
  if (D_userfd >= 0)
    {
      close(D_userfd);
      D_userfd = -1;
    }
  if (auto_detach || displays->d_next)
    Detach(D_DETACH);
  else
    Finit(0);
  SIGRETURN;
}

/* 
 * the backend's Interrupt handler
 * we cannot insert the intrc directly, as we never know
 * if fore is valid.
 */
static sigret_t
SigInt SIGDEFARG
{
#if HAZARDOUS
  char buf[1];

  debug("SigInt()\n");
  *buf = (char) intrc;
  if (fore)
    fore->w_inlen = 0;
  if (fore)
    write(fore->w_ptyfd, buf, 1);
#else
  signal(SIGINT, SigInt);
  debug("SigInt() careful\n");
  InterruptPlease = 1;
#endif
  SIGRETURN;
}

static sigret_t
CoreDump SIGDEFARG
{
  struct display *disp;
  char buf[80];

#if defined(SYSVSIGS) && defined(SIGHASARG)
  signal(sigsig, SIG_IGN);
#endif
  setgid(getgid());
  setuid(getuid());
  unlink("core");
#ifdef SIGHASARG
  sprintf(buf, "\r\n[screen caught signal %d.%s]\r\n", sigsig,
#else
  sprintf(buf, "\r\n[screen caught a fatal signal.%s]\r\n",
#endif
#if defined(SHADOWPW) && !defined(DEBUG) && !defined(DUMPSHADOW)
              ""
#else /* SHADOWPW  && !DEBUG */
              " (core dumped)"
#endif /* SHADOWPW  && !DEBUG */
              );
  for (disp = displays; disp; disp = disp->d_next)
    {
      fcntl(disp->d_userfd, F_SETFL, 0);
      SetTTY(disp->d_userfd, &D_OldMode);
      write(disp->d_userfd, buf, strlen(buf));
      Kill(disp->d_userpid, SIG_BYE);
    }
#if defined(SHADOWPW) && !defined(DEBUG) && !defined(DUMPSHADOW)
  Kill(getpid(), SIGKILL);
  eexit(11);
#else /* SHADOWPW && !DEBUG */
  abort();
#endif /* SHADOWPW  && !DEBUG */
  SIGRETURN;
}

static void
DoWait()
{
  register int pid;
  struct win *p, *next;
#ifdef BSDWAIT
  union wait wstat;
#else
  int wstat;
#endif

#ifdef BSDJOBS
# ifndef BSDWAIT
  while ((pid = waitpid(-1, &wstat, WNOHANG | WUNTRACED)) > 0)
# else
# ifdef USE_WAIT2
  /* 
   * From: rouilj@sni-usa.com (John Rouillard) 
   * note that WUNTRACED is not documented to work, but it is defined in
   * /usr/include/sys/wait.h, so it may work 
   */
  while ((pid = wait2(&wstat, WNOHANG | WUNTRACED )) > 0)
#  else /* USE_WAIT2 */
  while ((pid = wait3(&wstat, WNOHANG | WUNTRACED, (struct rusage *) 0)) > 0)
#  endif /* USE_WAIT2 */
# endif
#else	/* BSDJOBS */
  while ((pid = wait(&wstat)) < 0)
    if (errno != EINTR)
      break;
  if (pid > 0)
#endif	/* BSDJOBS */
    {
      for (p = windows; p; p = next)
	{
	  next = p->w_next;
	  if (pid == p->w_pid)
	    {
#ifdef BSDJOBS
	      if (WIFSTOPPED(wstat))
		{
		  debug3("Window %d pid %d: WIFSTOPPED (sig %d)\n", p->w_number, p->w_pid, WSTOPSIG(wstat));
#ifdef SIGTTIN
		  if (WSTOPSIG(wstat) == SIGTTIN)
		    {
		      Msg(0, "Suspended (tty input)");
		      continue;
		    }
#endif
#ifdef SIGTTOU
		  if (WSTOPSIG(wstat) == SIGTTOU)
		    {
		      Msg(0, "Suspended (tty output)");
		      continue;
		    }
#endif
		  /* Try to restart process */
		  Msg(0, "Child has been stopped, restarting.");
		  if (killpg(p->w_pid, SIGCONT))
		    kill(p->w_pid, SIGCONT);
		}
	      else
#endif
		{
		  WindowDied(p);
		}
	      break;
	    }
#ifdef PSEUDOS
	  if (p->w_pwin && pid == p->w_pwin->p_pid)
	    {
	      debug2("pseudo of win Nr %d died. pid == %d\n", p->w_number, p->w_pwin->p_pid);
	      FreePseudowin(p);
	      break;
	    }
#endif
	}
      if (p == 0)
	{
	  debug1("pid %d not found - hope that's ok\n", pid);
	}
    }
}


static sigret_t
FinitHandler SIGDEFARG
{
#ifdef SIGHASARG
  debug1("FinitHandler called, sig %d.\n", sigsig);
#else
  debug("FinitHandler called.\n");
#endif
  Finit(1);
  SIGRETURN;
}

void
Finit(i)
int i;
{
  struct win *p, *next;

  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  debug1("Finit(%d);\n", i);
  for (p = windows; p; p = next)
    {
      next = p->w_next;
      FreeWindow(p);
    }
  if (ServerSocket != -1)
    {
      debug1("we unlink(%s)\n", SockPath);
#ifdef USE_SETEUID
      xseteuid(real_uid);
      xsetegid(real_gid);
#endif
      (void) unlink(SockPath);
#ifdef USE_SETEUID
      xseteuid(eff_uid);
      xsetegid(eff_gid);
#endif
    }
  for (display = displays; display; display = display->d_next)
    {
      if (D_status)
	RemoveStatus();
      FinitTerm();
#ifdef UTMPOK
      RestoreLoginSlot();
#endif
      AddStr("[screen is terminating]\r\n");
      Flush();
      SetTTY(D_userfd, &D_OldMode);
      fcntl(D_userfd, F_SETFL, 0);
      freetty();
      Kill(D_userpid, SIG_BYE);
    }
  /*
   * we _cannot_ call eexit(i) here, 
   * instead of playing with the Socket above. Sigh.
   */
  exit(i);
}

void
eexit(e)
int e;
{
  debug("eexit\n");
  if (ServerSocket != -1)
    {
      debug1("we unlink(%s)\n", SockPath);
      setuid(real_uid);
      setgid(real_gid);
      (void) unlink(SockPath);
    }
  exit(e);
}

/*
 * Detach now has the following modes:
 *D_DETACH	 SIG_BYE	detach backend and exit attacher
 *D_STOP	 SIG_STOP	stop attacher (and detach backend)
 *D_REMOTE	 SIG_BYE	remote detach -- reattach to new attacher
 *D_POWER 	 SIG_POWER_BYE 	power detach -- attacher kills his parent
 *D_REMOTE_POWER SIG_POWER_BYE	remote power detach -- both
 *D_LOCK	 SIG_LOCK	lock the attacher
 * (jw)
 * we always remove our utmp slots. (even when "lock" or "stop")
 * Note: Take extra care here, we may be called by interrupt!
 */
void
Detach(mode)
int mode;
{
  int sign = 0, pid;
  struct canvas *cv;
  struct win *p;

  if (display == 0)
    return;

  signal(SIGHUP, SIG_IGN);
  debug1("Detach(%d)\n", mode);
  if (D_status)
    RemoveStatus();
  FinitTerm();
  switch (mode)
    {
    case D_DETACH:
      AddStr("[detached]\r\n");
      sign = SIG_BYE;
      break;
#ifdef BSDJOBS
    case D_STOP:
      sign = SIG_STOP;
      break;
#endif
#ifdef REMOTE_DETACH
    case D_REMOTE:
      AddStr("[remote detached]\r\n");
      sign = SIG_BYE;
      break;
#endif
#ifdef POW_DETACH
    case D_POWER:
      AddStr("[power detached]\r\n");
      if (PowDetachString) 
	{
	  AddStr(expand_vars(PowDetachString, display));
	  AddStr("\r\n");
	}
      sign = SIG_POWER_BYE;
      break;
#ifdef REMOTE_DETACH
    case D_REMOTE_POWER:
      AddStr("[remote power detached]\r\n");
      if (PowDetachString) 
	{
	  AddStr(expand_vars(PowDetachString, display));
	  AddStr("\r\n");
	}
      sign = SIG_POWER_BYE;
      break;
#endif
#endif
    case D_LOCK:
      ClearDisplay();
      sign = SIG_LOCK;
      /* tell attacher to lock terminal with a lockprg. */
      break;
    }
#ifdef UTMPOK
  if (displays->d_next == 0)
    {
      for (p = windows; p; p = p->w_next)
        {
	  if (p->w_slot != (slot_t) -1)
	    {
	      RemoveUtmp(p);
	      /*
	       * Set the slot to 0 to get the window
	       * logged in again.
	       */
	      p->w_slot = (slot_t) 0;
	    }
	}
    }
  RestoreLoginSlot();
#endif
  if (displays->d_next == 0 && console_window)
    {
      if (TtyGrabConsole(console_window->w_ptyfd, 0, "detach"))
	{
	  debug("could not release console - killing window\n");
	  KillWindow(console_window);
	  display = displays;		/* restore display */
	}
    }
  if (D_fore)
    {
#ifdef MULTIUSER
      ReleaseAutoWritelock(display, D_fore);
#endif
      D_user->u_detachwin = D_fore->w_number;
      D_user->u_detachotherwin = D_other ? D_other->w_number : -1;
    }
  for (cv = D_cvlist; cv; cv = cv->c_next)
    {
      p = Layer2Window(cv->c_layer);
      SetCanvasWindow(cv, 0);
      if (p)
        WindowChanged(p, 'u');
    }

  pid = D_userpid;
  debug2("display: %#x displays: %#x\n", (unsigned int)display, (unsigned int)displays);
  FreeDisplay();
  if (displays == 0)
    /* Flag detached-ness */
    (void) chsock();
  /*
   * tell father what to do. We do that after we
   * freed the tty, thus getty feels more comfortable on hpux
   * if it was a power detach.
   */
  Kill(pid, sign);
  debug2("Detach: Signal %d to Attacher(%d)!\n", sign, pid);
  debug("Detach returns, we are successfully detached.\n");
  signal(SIGHUP, SigHup);
}

static int
IsSymbol(e, s)
char *e, *s;
{
  register int l;

  l = strlen(s);
  return strncmp(e, s, l) == 0 && e[l] == '=';
}

void
MakeNewEnv()
{
  register char **op, **np;
  static char stybuf[MAXSTR];

  for (op = environ; *op; ++op)
    ;
  if (NewEnv)
    free((char *)NewEnv);
  NewEnv = np = (char **) malloc((unsigned) (op - environ + 7 + 1) * sizeof(char **));
  if (!NewEnv)
    Panic(0, strnomem);
  sprintf(stybuf, "STY=%s", strlen(SockName) <= MAXSTR - 5 ? SockName : "?");
  *np++ = stybuf;	                /* NewEnv[0] */
  *np++ = Term;	                /* NewEnv[1] */
  np++;		/* room for SHELL */
#ifdef TIOCSWINSZ
  np += 2;	/* room for TERMCAP and WINDOW */
#else
  np += 4;	/* room for TERMCAP WINDOW LINES COLUMNS */
#endif

  for (op = environ; *op; ++op)
    {
      if (!IsSymbol(*op, "TERM") && !IsSymbol(*op, "TERMCAP")
	  && !IsSymbol(*op, "STY") && !IsSymbol(*op, "WINDOW")
	  && !IsSymbol(*op, "SCREENCAP") && !IsSymbol(*op, "SHELL")
	  && !IsSymbol(*op, "LINES") && !IsSymbol(*op, "COLUMNS")
	 )
	*np++ = *op;
    }
  *np = 0;
}

void
/*VARARGS2*/
#if defined(USEVARARGS) && defined(__STDC__)
Msg(int err, char *fmt, VA_DOTS)
#else
Msg(err, fmt, VA_DOTS)
int err;
char *fmt;
VA_DECL
#endif
{
  VA_LIST(ap)
  char buf[MAXPATHLEN*2];
  char *p = buf;

  VA_START(ap, fmt);
  fmt = DoNLS(fmt);
  (void)vsnprintf(p, sizeof(buf) - 100, fmt, VA_ARGS(ap));
  VA_END(ap);
  if (err)
    {
      p += strlen(p);
      *p++ = ':';
      *p++ = ' ';
      strncpy(p, strerror(err), buf + sizeof(buf) - p - 1);
      buf[sizeof(buf) - 1] = 0;
    }
  debug2("Msg('%s') (%#x);\n", buf, (unsigned int)display);

  if (display && displays)
    MakeStatus(buf);
  else if (displays)
    {
      for (display = displays; display; display = display->d_next)
	MakeStatus(buf);
    }
  else if (display)
    {
      /* no displays but a display - must have forked.
       * send message to backend!
       */
      char *tty = D_usertty;
      struct display *olddisplay = display;
      display = 0;	/* only send once */
      SendErrorMsg(tty, buf);
      display = olddisplay;
    }
  else
    printf("%s\r\n", buf);
}

/*
 * Call FinitTerm for all displays, write a message to each and call eexit();
 */
void
/*VARARGS2*/
#if defined(USEVARARGS) && defined(__STDC__)
Panic(int err, char *fmt, VA_DOTS)
#else
Panic(err, fmt, VA_DOTS)
int err;
char *fmt;
VA_DECL
#endif
{
  VA_LIST(ap)
  char buf[MAXPATHLEN*2];
  char *p = buf;

  VA_START(ap, fmt);
  fmt = DoNLS(fmt);
  (void)vsnprintf(p, sizeof(buf) - 100, fmt, VA_ARGS(ap));
  VA_END(ap);
  if (err)
    {
      p += strlen(p);
      *p++ = ':';
      *p++ = ' ';
      strncpy(p, strerror(err), buf + sizeof(buf) - p - 1);
      buf[sizeof(buf) - 1] = 0;
    }
  debug3("Panic('%s'); display=%x displays=%x\n", buf, display, displays);
  if (displays == 0 && display == 0)
    printf("%s\r\n", buf);
  else if (displays == 0)
    {
      /* no displays but a display - must have forked.
       * send message to backend!
       */
      char *tty = D_usertty;
      display = 0;
      SendErrorMsg(tty, buf);
      sleep(2);
      _exit(1);
    }
  else
    for (display = displays; display; display = display->d_next)
      {
        if (D_status)
	  RemoveStatus();
        FinitTerm();
        Flush();
#ifdef UTMPOK
        RestoreLoginSlot();
#endif
        SetTTY(D_userfd, &D_OldMode);
        fcntl(D_userfd, F_SETFL, 0);
        write(D_userfd, buf, strlen(buf));
        write(D_userfd, "\n", 1);
        freetty();
	if (D_userpid)
	  Kill(D_userpid, SIG_BYE);
      }
#ifdef MULTIUSER
  if (tty_oldmode >= 0)
    {
# ifdef USE_SETEUID
      if (setuid(own_uid))
        xseteuid(own_uid);	/* may be a loop. sigh. */
# else
      setuid(own_uid);
# endif
      debug1("Panic: changing back modes from %s\n", attach_tty);
      chmod(attach_tty, tty_oldmode);
    }
#endif
  eexit(1);
}


/*
 * '^' is allowed as an escape mechanism for control characters. jw.
 * 
 * Added time insertion using ideas/code from /\ndy Jones
 *   (andy@lingua.cltr.uq.OZ.AU) - thanks a lot!
 *
 */

static const char days[]   = "SunMonTueWedThuFriSat";
static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

char *
MakeWinMsgEv(str, win, esc, ev)
char *str;
struct win *win;
int esc;
struct event *ev;
{
  static char buf[MAXSTR];
  static int tick;
  char *s = str;
  register char *p = buf;
  register int ctrl;
  struct timeval now;
  struct tm *tm;
  int l;
  int num;
  int zeroflg;
  int qmflag = 0, omflag = 0;
  char *qmpos = 0;
 
  tick = 0;
  tm = 0;
  ctrl = 0;
  gettimeofday(&now, NULL);
  for (; *s && (l = buf + MAXSTR - 1 - p) > 0; s++, p++)
    {
      *p = *s;
      if (ctrl)
	{
	  ctrl = 0;
	  if (*s != '^' && *s >= 64)
	    *p &= 0x1f;
	  continue;
	}
      if (*s != esc)
	{
	  if (esc == '%')
	    {
	      switch (*s)
		{
#if 0
		case '~':
		  *p = BELL;
		  break;
#endif
		case '^':
		  ctrl = 1;
		  *p-- = '^';
		  break;
		default:
		  break;
		}
	    }
	  continue;
	}
      if (*++s == esc)	/* double escape ? */
	continue;
      if ((zeroflg = *s == '0') != 0)
	s++;
      num = 0;
      while(*s >= '0' && *s <= '9')
	num = num * 10 + (*s++ - '0');
      switch (*s)
	{
        case '?':
	  p--;
	  if (qmpos)
	    {
	      if ((!qmflag && !omflag) || omflag == 1)
	        p = qmpos;
	      qmpos = 0;
	      break;
	    }
	  qmpos = p;
	  qmflag = omflag = 0;
	  break;
        case ':':
	  p--;
	  if (!qmpos)
	    break;
	  if (qmflag && omflag != 1)
	    {
	      omflag = 1;
	      qmpos = p;
	    }
	  else
	    {
	      p = qmpos;
	      omflag = -1;
	    }
	  break;
	case 'd': case 'D': case 'm': case 'M': case 'y': case 'Y':
	case 'a': case 'A': case 's': case 'c': case 'C':
	  if (l < 4)
	    break;
	  if (tm == 0)
	    tm = localtime(&now.tv_sec);
	  qmflag = 1;
	  switch (*s)
	    {
	    case 'd':
	      sprintf(p, "%02d", tm->tm_mday % 100);
	      tick |= 4;
	      break;
	    case 'D':
	      sprintf(p, "%3.3s", days + 3 * tm->tm_wday);
	      tick |= 4;
	      break;
	    case 'm':
	      sprintf(p, "%02d", tm->tm_mon + 1);
	      tick |= 4;
	      break;
	    case 'M':
	      sprintf(p, "%3.3s", months + 3 * tm->tm_mon);
	      tick |= 4;
	      break;
	    case 'y':
	      sprintf(p, "%02d", tm->tm_year % 100);
	      tick |= 4;
	      break;
	    case 'Y':
	      sprintf(p, "%04d", tm->tm_year + 1900);
	      tick |= 4;
	      break;
	    case 'a':
	      sprintf(p, tm->tm_hour >= 12 ? "pm" : "am");
	      tick |= 4;
	      break;
	    case 'A':
	      sprintf(p, tm->tm_hour >= 12 ? "PM" : "AM");
	      tick |= 4;
	      break;
	    case 's':
	      sprintf(p, "%02d", tm->tm_sec);
	      tick |= 1;
	      break;
	    case 'c':
	      sprintf(p, zeroflg ? "%02d:%02d" : "%2d:%02d", tm->tm_hour, tm->tm_min);
	      tick |= 2;
	      break;
	    case 'C':
	      sprintf(p, zeroflg ? "%02d:%02d" : "%2d:%02d", (tm->tm_hour + 11) % 12 + 1, tm->tm_min);
	      tick |= 2;
	      break;
	    default:
	      break;
	    }
	  p += strlen(p) - 1;
	  break;
	case 'l':
#ifdef LOADAV
	  *p = 0;
	  if (l > 20)
	    AddLoadav(p);
	  if (*p)
	    {
	      qmflag = 1;
	      p += strlen(p) - 1;
	    }
	  else
	    *p = '?';
	  tick |= 2;
#else
	  *p = '?';
#endif
	  p += strlen(p) - 1;
	  break;
	case 'h':
	  if (win == 0 || win->w_hstatus == 0 || *win->w_hstatus == 0 || str == win->w_hstatus)
	    p--;
	  else
	    {
	      char savebuf[sizeof(buf)];
	      int oldtick = tick;

	      *p = 0;
	      strcpy(savebuf, buf);
	      MakeWinMsg(win->w_hstatus, win, '\005');
	      tick |= oldtick;		/* small hack... */
	      if (strlen(buf) < l)
		strcat(savebuf, buf);
	      strcpy(buf, savebuf);
	      if (*p)
		qmflag = 1;
	      p += strlen(p) - 1;
	    }
	  break;
	case 'w':
	case 'W':
	  {
	    struct win *oldfore = 0;
	    if (display)
	      {
		oldfore = D_fore;
		D_fore = win;
	      }
	    AddWindows(p, l - 1, *s == 'w' ? 2 : 3, -1);
	    if (display)
	      D_fore = oldfore;
	  }
	  if (*p)
	    qmflag = 1;
	  p += strlen(p) - 1;
	  break;
	case 'u':
	  *p = 0;
	  if (win)
	    AddOtherUsers(p, l - 1, win);
	  if (*p)
	    qmflag = 1;
	  p += strlen(p) - 1;
	  break;
	case 't':
	  *p = 0;
	  if (win && strlen(win->w_title) < l)
	    {
	      strcpy(p, win->w_title);
	      if (*p)
		qmflag = 1;
	    }
	  p += strlen(p) - 1;
	  break;
	case 'n':
	  s++;
	  /* FALLTHROUGH */
	default:
	  s--;
	  if (l > 10 + num)
	    {
	      if (num == 0)
		num = 1;
	      if (!win)
	        sprintf(p, "%*s", num, num > 1 ? "--" : "-");
	      else
	        sprintf(p, "%*d", num, win->w_number);
	      qmflag = 1;
	      p += strlen(p) - 1;
	    }
	  break;
	}
    }
  if (qmpos && !qmflag)
    p = qmpos + 1;
  *p = '\0';
  if (ev)
    {
      evdeq(ev);		/* just in case */
      ev->timeout.tv_sec = 0;
      ev->timeout.tv_usec = 0;
    }
  if (ev && tick)
    {
      now.tv_usec = 0;
      if (tick & 1)
	now.tv_sec++;
      else if (tick & 2)
	now.tv_sec += 60 - (now.tv_sec % 60);
      else if (tick & 4)
	now.tv_sec += 3600 - (now.tv_sec % 3600);
      ev->timeout = now;
    }
  return buf;
}

char *
MakeWinMsg(s, win, esc)
char *s;
struct win *win;
int esc;
{
  return MakeWinMsgEv(s, win, esc, (struct event *)0);
}

void
DisplaySleep(n)
int n;
{
  char buf;
  fd_set r;
  struct timeval t;

  if (!display)
    {
      debug("DisplaySleep has no display sigh\n");
      sleep(n);
      return;
    }
  t.tv_usec = 0;
  t.tv_sec = n;
  FD_ZERO(&r);
  FD_SET(D_userfd, &r);
  if (select(FD_SETSIZE, &r, (fd_set *)0, (fd_set *)0, &t) > 0)
    {
      debug("display activity stopped sleep\n");
      read(D_userfd, &buf, 1);
    }
  debug1("DisplaySleep(%d) ending\n", n);
}


#ifdef DEBUG
static void
fds1(i, j)
int i, j;
{
  while (i < j)
    {
      debug1("%d ", i);
      i++;
    }
  if ((j = open("/dev/null", 0)) >= 0)
    {
      fds1(i + 1, j);
      close(j);
    }
  else
    {
      while (dup(++i) < 0 && errno != EBADF)
        debug1("%d ", i);
      debug1(" [%d]\n", i);
    }
}

static void
fds()
{
  debug("fds: ");
  fds1(-1, -1);
}
#endif

static void
serv_read_fn(ev, data)
struct event *ev;
char *data;
{
  debug("Knock - knock!\n");
  ReceiveMsg();
}

static void
serv_select_fn(ev, data)
struct event *ev;
char *data;
{
  struct win *p;

  debug("serv_select_fn called\n");
  /* XXX: messages?? */
  if (GotSigChld)
    {
      SigChldHandler();
    }
  if (InterruptPlease)
    {
      debug("Backend received interrupt\n");
      /* This approach is rather questionable in a multi-display
       * environment */
      if (fore)
	{
	  char ibuf = intrc;
#ifdef PSEUDOS
	  write(W_UWP(fore) ? fore->w_pwin->p_ptyfd : fore->w_ptyfd, 
		&ibuf, 1);
	  debug1("Backend wrote interrupt to %d", fore->w_number);
	  debug1("%s\n", W_UWP(fore) ? " (pseudowin)" : "");
#else
	  write(fore->w_ptyfd, &ibuf, 1);
	  debug1("Backend wrote interrupt to %d\n", fore->w_number);
#endif
	}
      InterruptPlease = 0;
    }

  for (display = displays; display; display = display->d_next)
    {
      if (D_status_delayed > 0)
	{
	  D_status_delayed = -1;
	  MakeStatus(D_status_lastmsg);
	}
    }

  for (p = windows; p; p = p->w_next)
    {
      if (p->w_bell == BELL_FOUND || p->w_bell == BELL_VISUAL)
	{
	  struct canvas *cv;
	  int visual = p->w_bell == BELL_VISUAL || visual_bell;
	  p->w_bell = BELL_ON;
	  for (display = displays; display; display = display->d_next)
	    {
	      for (cv = D_cvlist; cv; cv = cv->c_next)
		if (cv->c_layer->l_bottom == &p->w_layer)
		  break;
	      if (cv == 0)
		{
		  p->w_bell = BELL_DONE;
		  D_status_delayed = -1;
		  Msg(0, "%s", MakeWinMsg(BellString, p, '%'));
		}
	      else if (visual && !D_VB && (!D_status || !D_status_bell))
		{
		  D_status_delayed = -1;
		  Msg(0, VisualBellString);
		  if (D_status)
		    {
		      D_status_bell = 1;
		      debug1("using vbell timeout %d\n", VBellWait);
		      SetTimeout(&D_statusev, VBellWait * 1000);
		    }
		}
	    }
	  /* don't annoy the user with two messages */
	  if (p->w_monitor == MON_FOUND)
	    p->w_monitor = MON_DONE;
	}
      if (p->w_monitor == MON_FOUND)
	{
	  struct canvas *cv;
	  p->w_monitor = MON_ON;
	  for (display = displays; display; display = display->d_next)
	    {
	      for (cv = D_cvlist; cv; cv = cv->c_next)
		if (cv->c_layer->l_bottom == &p->w_layer)
		  break;
	      if (cv)
		continue;	/* user already sees window */
#ifdef MULTIUSER
	      if (!(ACLBYTE(p->w_mon_notify, D_user->u_id) & ACLBIT(D_user->u_id)))
		continue;	/* user doesn't care */
#endif
	      D_status_delayed = -1;
	      Msg(0, "%s", MakeWinMsg(ActivityString, p, '%'));
	      p->w_monitor = MON_DONE;
	    }
	}
    }

  for (display = displays; display; display = display->d_next)
    {
      struct canvas *cv;
      if (D_status == STATUS_ON_WIN)
	continue;
      /* XXX: should use display functions! */
      for (cv = D_cvlist; cv; cv = cv->c_next)
	{
	  int lx, ly;

	  /* normalize window, see resize.c */
	  lx = cv->c_layer->l_x;
	  ly = cv->c_layer->l_y;
	  if (lx == cv->c_layer->l_width)
	    lx--;
	  if (ly + cv->c_yoff < cv->c_ys)
	    {
	      int i, n = cv->c_ys - (ly + cv->c_yoff);
	      cv->c_yoff = cv->c_ys - ly;
	      RethinkViewportOffsets(cv);
	      if (n > cv->c_layer->l_height)
		n = cv->c_layer->l_height;
	      CV_CALL(cv, 
		LScrollV(flayer, -n, 0, flayer->l_height - 1);
		RedisplayLine(-1, -1, -1, 1);
		for (i = 0; i < n; i++)
		  RedisplayLine(i, 0, flayer->l_width - 1, 1);
	        if (cv == cv->c_display->d_forecv)
	          SetCursor();
	      );
	    }
	  else if (ly + cv->c_yoff > cv->c_ye)
	    {
	      int i, n = ly + cv->c_yoff - cv->c_ye;
	      cv->c_yoff = cv->c_ye - ly;
	      RethinkViewportOffsets(cv);
	      if (n > cv->c_layer->l_height)
		n = cv->c_layer->l_height;
	      CV_CALL(cv, 
	        LScrollV(flayer, n, 0, cv->c_layer->l_height - 1);
		RedisplayLine(-1, -1, -1, 1);
		for (i = 0; i < n; i++)
		  RedisplayLine(i + flayer->l_height - n, 0, flayer->l_width - 1, 1);
	        if (cv == cv->c_display->d_forecv)
	          SetCursor();
	      );
	    }
	  if (lx + cv->c_xoff < cv->c_xs)
	    {
	      int i, n = cv->c_xs - (lx + cv->c_xoff);
	      if (n < (cv->c_xe - cv->c_xs + 1) / 2)
		n = (cv->c_xe - cv->c_xs + 1) / 2;
	      if (cv->c_xoff + n > cv->c_xs)
		n = cv->c_xs - cv->c_xoff;
	      cv->c_xoff += n;
	      RethinkViewportOffsets(cv);
	      if (n > cv->c_layer->l_width)
		n = cv->c_layer->l_width;
	      CV_CALL(cv, 
		RedisplayLine(-1, -1, -1, 1);
		for (i = 0; i < flayer->l_height; i++)
		  {
		    LScrollH(flayer, -n, i, 0, flayer->l_width - 1, 0);
		    RedisplayLine(i, 0, n - 1, 1);
		  }
	        if (cv == cv->c_display->d_forecv)
	          SetCursor();
	      );
	    }
	  else if (lx + cv->c_xoff > cv->c_xe)
	    {
	      int i, n = lx + cv->c_xoff - cv->c_xe;
	      if (n < (cv->c_xe - cv->c_xs + 1) / 2)
		n = (cv->c_xe - cv->c_xs + 1) / 2;
	      if (cv->c_xoff - n + cv->c_layer->l_width - 1 < cv->c_xe)
		n = cv->c_xoff + cv->c_layer->l_width - 1 - cv->c_xe;
	      cv->c_xoff -= n;
	      RethinkViewportOffsets(cv);
	      if (n > cv->c_layer->l_width)
		n = cv->c_layer->l_width;
	      CV_CALL(cv, 
		RedisplayLine(-1, -1, -1, 1);
		for (i = 0; i < flayer->l_height; i++)
		  {
		    LScrollH(flayer, n, i, 0, flayer->l_width - 1, 0);
		    RedisplayLine(i, flayer->l_width - n, flayer->l_width - 1, 1);
		  }
	        if (cv == cv->c_display->d_forecv)
	          SetCursor();
	      );
	    }
	}
    }

  for (display = displays; display; display = display->d_next)
    {
      if (D_status == STATUS_ON_WIN || D_cvlist == 0 || D_cvlist->c_next == 0)
	continue;
      debug1("serv_select_fn: Restore on cv %#x\n", (int)D_forecv);
      CV_CALL(D_forecv, Restore();SetCursor());
    }
}

static void
logflush_fn(ev, data)
struct event *ev;
char *data;
{
  struct win *p;
  char *buf;
  int n;

  if (!islogfile(NULL))
    return;		/* no more logfiles */
  logfflush(NULL);
  n = log_flush ? log_flush : (logtstamp_after + 4) / 5;
  if (n)
    {
      SetTimeout(ev, n * 1000);
      evenq(ev);	/* re-enqueue ourself */
    }
  if (!logtstamp_on)
    return;
  /* write fancy time-stamp */
  for (p = windows; p; p = p->w_next)
    {
      if (!p->w_log)
	continue;
      p->w_logsilence += n;
      if (p->w_logsilence < logtstamp_after)
	continue;
      if (p->w_logsilence - n >= logtstamp_after)
	continue;
      buf = MakeWinMsg(logtstamp_string, p, '%');
      logfwrite(p->w_log, buf, strlen(buf));
    }
}

