/*
   memfetch 0.05b - Linux on-demand process image dump
   ---------------------------------------------------
   Copyright (C) 2002, 2003 by Michal Zalewski <lcamtuf@coredump.cx>

   Licensed under terms and conditions of GNU Public License version 2.

   This is a simple code, but there are some tricks. First of all,
   mmaping /proc/pid/mem isn't as straightforward as it could seem. We
   have to 'touch' each page to ensure its physical allocation before
   being able to mmap it.

   Why some dumbass removed the ability to mmap() /proc/pid/mem in 2.4?!

   It should be easy to port this to *BSD.

 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <linux/a.out.h>
//#include <asm/page.h>
#include <getopt.h>
#include <errno.h>

// Servicable parts:
#define MAINFILE	"mfetch.lst"
#define MAPPREFIX	"map-"
#define MEMPREFIX	"mem-"
#define BINSUFFIX	".bin"
#define MAXBUF		1024
// End of servicable parts.

#define debug(x...) fprintf(stderr,x)

#define fatal(x...) { \
	fprintf(stderr,"[-] ERROR: " x); \
	if (outfile && !textout) { \
		fprintf(outfile,"** An error occured while generating this file.\n"); \
		fprintf(outfile,"** Error message: " x); \
		fclose(outfile); \
	} \
	if (tracepid>0) ptrace(PTRACE_DETACH,tracepid,NULL,(void*)lastsig); \
	exit(1); \
}

static FILE* outfile;
static int  tracepid;
static int  lastsig;
static char textout;
static char avoid_mmap;

void exitsig(int x) {
	static int sig_reentry;
	if (sig_reentry) exit(1); 
	sig_reentry=1;
	fatal("Whoops. Exiting on signal %d.\n",x);
}

void usage(char* myname) {
	debug("Usage: %s [ -sawm ] [ -S xxx ] PID\n"
			"  -s      - wait for fault signal before generating a dump\n"
			"  -a      - skip non-anonymous maps (libraries etc)\n"
			"  -w      - write index file to stdout instead of mfetch.lst\n"
			"  -m      - avoid mmap(), helps prevent hanging on some 2.2 boxes\n"
			"  -S xxx  - dump segment containing address xxx (hex) only\n",myname);
	exit(3);
}

int wait_sig(void)
{
	int st;
	while (1) {
		ptrace(PTRACE_CONT,tracepid,0,(void*)lastsig);

		if (wait(&st)<=0) {
			debug("[-] Process gone before receiving a fault signal.\n");
			exit(2);
		}

		if (WIFEXITED(st)) {
			debug("[-] Process exited with code %d before receiving a fault "
					"signal.\n",WEXITSTATUS(st));
			exit(2);
		}

		if (WIFSIGNALED(st)) {
			debug("[-] Process killed with signal %d before receiving a fault "
					"signal.\n",WTERMSIG(st));
			exit(2);
		}

		if (!WIFSTOPPED(st)) 
			fatal("Suddenly, the process disappears! You die!\n");

		lastsig=WSTOPSIG(st);
		debug("[+] Process received sig(%d)\n", lastsig);
		if (lastsig==SIGTRAP || lastsig==SIGSTOP) lastsig=0;

		switch (lastsig) {
		case SIGSEGV: 
			debug("[+] Process received SIGSEGV, let's have a look...\n"); 
			return 0;
		case SIGILL:
			debug("[+] Process received SIGILL, let's have a look...\n"); 
			return 0;
		case SIGPIPE:
			debug("[+] Process received SIGPIPE, let's have a look...\n"); 
			return 0;
		case SIGFPE:
			debug("[+] Process received SIGFPE, let's have a look...\n"); 
			return 0;
		case SIGBUS:
			debug("[+] Process received SIGBUS, let's have a look...\n"); 
			return 0;
		}
	}

	return 0;
}

int page_size;

int dump_memory(int memfile, int offset, int len, int dumpfile)
{
	int i = 0;
	int count = len/page_size;
	int* writeptr = malloc(page_size);

	if (lseek(memfile, offset, SEEK_SET)==offset)
	{
		//debug("count=%d\n", count);
		for (i=0; i<count; i++) {
			if (read(memfile, writeptr, page_size) != page_size) {
				break;
			}
			if (write(dumpfile, writeptr, page_size) != page_size) {
				break;
			}
		}
	}
	if (i==count)
		debug("[N] ");
	else {
		debug("[S] ");
		lseek(dumpfile, 0, SEEK_SET);
		for (i=0; i<count; i++) {
			int j=0;
			for (j=0; j<page_size/4; j++) {
				writeptr[j]=ptrace(PTRACE_PEEKDATA, tracepid, (void*)(offset+i*page_size+j*4), 0);
			}
			if (write(dumpfile, writeptr, page_size) != page_size) {
				break;
			}
		}
	}

	free(writeptr);

	return (i==count)?0:1;
}

int main(int argc,char* argv[]) {
	FILE* mapfile;
	static char exename[MAXBUF+1];
	static char tmp[MAXBUF+1];
	char waitsig=0,skipmap=0;
	int st,memfile,dumpcnt=0;
	unsigned int onlyseg=0;
	int opt;

	debug("memfetch 0.05b by Michal Zalewski <lcamtuf@coredump.cx>\n");

	signal(SIGINT,exitsig);
	signal(SIGQUIT,exitsig);
	signal(SIGHUP,exitsig);
	signal(SIGPIPE,exitsig);
	signal(SIGTERM,exitsig);
	signal(SIGSEGV,exitsig);
	signal(SIGBUS,exitsig);
	signal(SIGILL,exitsig);

	if (argc<2) usage(argv[0]);

	while ((opt=getopt(argc,(void*)argv, "+samwS:h"))!=-1) {
		switch(opt) {
			case 's': waitsig=1; break;
			case 'a': skipmap=1; break;
			case 'w': textout=1; break;
			case 'm': avoid_mmap=1; break;
			case 'S':
				if (sscanf(optarg,"%x",&onlyseg)!=1)
					fatal("Incorrect -S syntax (hex address expected).\n");
				break;
			default:  usage(argv[0]); break;
		}
	}

	if (skipmap && onlyseg) fatal("Options -S and -e are mutually exclusive.\n");

	if (argc-optind!=1) usage(argv[0]);

	tracepid=atoi(argv[optind]);

	if (kill(tracepid,0)) 
		fatal("Process does not exist or is not accessible.\n");

	if (ptrace(PTRACE_ATTACH,tracepid,0,0)) 
		fatal("Cannot attach to this process (already traced?).\n");

	if ( wait(&st)<=0 || !WIFSTOPPED(st) ) {
		if (errno==ECHILD && !kill(tracepid,0)) {
			if (waitsig) {
				fatal("This is a thread, -s is not compatible with threds.\n");
			}
			debug("[!] This process is likely a thread (use -m if your box hangs).\n");
		} else
			fatal("Process gone during attach (magic).\n"); 
	}

	debug("[+] Attached to PID %d",tracepid);

	sprintf(tmp,"/proc/%d/exe",tracepid);

	if (readlink(tmp,exename,MAXBUF-1)<=0) strcpy(exename,"<unknown>");

	debug(" (%s).\n",exename);

	if (waitsig) {
		debug("[*] Waiting for fault signal (SIGSEGV, SIGBUS, SIGILL, SIGPIPE "
				"or SIGFPE)...\n");
		wait_sig();
	}

leavewait: // GOTOs for president!

	page_size = getpagesize();
	printf("PAGE_SIZE is %d\n", page_size);
	sprintf(tmp,"/proc/%d/maps",tracepid);
	mapfile=fopen(tmp,"r");

	if (!mapfile)
		fatal("Cannot open %s for reading.\n",tmp); 

	if (textout) {
		outfile=stdout;
		debug("[*] Writing master information to standard output.\n\n");
		setbuffer(stdout,0,0);
	} else {
		int tmpfd;
		unlink(MAINFILE);
		tmpfd = open(MAINFILE, O_WRONLY|O_TRUNC|O_CREAT|O_EXCL, 0600);
		if (tmpfd<0)
			fatal("Cannot open output file " MAINFILE ".\n");
		debug("[*] Writing master information to " MAINFILE "...\n");
		outfile = fdopen(tmpfd,"w");
		if (!outfile)
			fatal("Stange error during fdopen().\n");
	}

	st=time(0);

	if (!textout)
		fprintf(outfile,"# This memory data dump generated by memfetch by "
				"<lcamtuf@coredump.cx>\n"
				"# PID %d, declared executable: %s\n"
				"# Date: %s\n\n",tracepid,exename,ctime((void*)&st));

	sprintf(tmp, "/proc/%d/mem", tracepid);
	memfile = open(tmp, O_RDONLY);
	if (memfile<0)
		fatal("Cannot open %s for reading.\n",tmp);

	while (fgets(tmp,1024,mapfile)) {
		int dumpfile;
		char small[256];
		unsigned int st,en,len,i;
		char* filepath;
		int* writeptr;

		if (sscanf(tmp,"%x-%x", &st, &en)!=2) {
			debug("[!] Parse error in /proc/%d/maps (mockery?): %s", tracepid, tmp);
			continue;
		}

		len = en - st;

		if ((filepath=strchr(tmp,'/'))) {
			*(filepath-1) = 0;
			sprintf(small, MAPPREFIX "%03d" BINSUFFIX, dumpcnt);
		} else {
			if (strchr(tmp,'\n'))
				*strchr(tmp,'\n')=0;
			sprintf(small, MEMPREFIX "%03d" BINSUFFIX, dumpcnt);
		}

		if (onlyseg && (onlyseg<st || onlyseg>en)) {
			if (!textout)
				debug("    Skipping %s at 0x%08x (%d bytes).\n",filepath?"map":"mem",
						st,len);
			continue;
		}

		if (filepath && skipmap) {
			if (!textout)
				debug("    Skipping map at 0x%08x (%d bytes).\n",st,len);
			continue;
		}

		fprintf(outfile,"[%03d] %s:\n"
				"     Memory range 0x%08x to 0x%08x (%d bytes)\n",
				dumpcnt,small,st,en,len);

		if (filepath)
			fprintf(outfile,"     MAPPED FROM: %s",filepath);
		fprintf(outfile,"     %s\n\n",tmp);

		unlink(small);
		dumpfile=open(small,O_WRONLY|O_TRUNC|O_CREAT|O_EXCL,0600);
		if (dumpfile<0)
			fatal("Cannot open output file %s.\n",small);

		if (!textout)
			debug("    Writing %s at 0x%08x (%d bytes)... ",filepath?"map":"mem",st,len);

		if (avoid_mmap)
			writeptr = MAP_FAILED;
		else {
			for (i=st; i<en; i+=page_size) {
				ptrace(PTRACE_PEEKDATA, tracepid, (void*)i, 0);
			}
			writeptr = mmap(0, len, PROT_READ, MAP_PRIVATE, memfile, st);
		}

		if (writeptr==MAP_FAILED) {
			if (dump_memory(memfile, st, len, dumpfile) != 0)
				fatal("Short write to %s.\n", small);
		} else {
			if (write(dumpfile,writeptr,len)!=len)
				fatal("Short write to %s.\n", small);
			munmap(writeptr, len);
		}

		dumpcnt++;
		if (!textout)
			debug("done (%s)\n",small);

		close(dumpfile);
	}

	if (!dumpcnt)
		fatal("No matching entries found in /proc/%d/maps.\n",tracepid);

	if (!textout)
		fprintf(outfile,"# End of file.\n");

	debug("[*] Done (%d matching). Have a nice day.\n", dumpcnt);

	fclose(outfile);
	ptrace(PTRACE_DETACH,tracepid,0,(void*)lastsig);

	exit(0);
}
