/*
 * Virtual tape driver - copyright 1998 Warren Toomey	wkt@cs.adfa.oz.au
 *
 * $Revision: 1.13 $
 * $Date: 1998/01/30 02:39:41 $
 *
 *
 * This program sits on a serial line, receives `tape' commands from a
 * PDP-11, and returns the results of those command to the other machine.
 * It was designed to allow 7th Edition UNIX to be installed on to a
 * machine without a tape drive.
 *
 * Much of the functionality of the tape protocol has been designed but
 * not yet implemented.
 *
 * Commands look like the following:
 *
 *	  +----+----+-----+------+----+----+---+----+
 *	  | 31 | 42 | cmd | rec# | blk|num | ck|sum |
 *	  +----+----+-----+------+----+----+---+----+
 *
 * Each box is an octet. The block number is a 16-bit value, with the
 * low-order byte first. Any data that is transmitted (in either direction)
 * comes as 512 octets between the block number and the checksum. The
 * checksum is a bitwise-XOR of octet pairs, excluding the checksum itself.
 * I.e checksum octet 1 holds 31 XOR cmd XOR blklo [ XOR odd data octets ], and
 * checksum octet 2 holds 42 XOR rec# XOR blkhi [ XOR even data octets ].
 *
 * A write command from the client has 512 bytes of data. Similarly, a read
 * command from the server to the client has 512 bytes of data.
 *
 * The Protocol
 * ------------
 *
 * The protocol is stateless. Commands are read, write, open and close.
 *
 * The record number holds the fictitious tape record which is requested.
 * The server keeps one disk file per record. The block number holds the
 * 512-octet block number within the record.
 *
 * Assumptions: a read on a record without a previous open implies the open.
 * A command on a new record will close any previously opened record.
 * There is only one outstanding client request at any time.
 *
 * The client sends a command to the server. The server returns the command,
 * possibly with 512 octets of data, to the client. The top four bits of the
 * command byte hold the return error value. All bits off indicates no error.
 * If an error occurred, including EOF, no data bytes are returned on a read
 * command.
 *
 * If the client receives a garbled return command, it will resend the command.
 * Therefore, the server must cope with this.
 *
 * The exception command to all of this is the QUICK read command. Here,
 * the server responds with one octet followed by 512 data octets. If the
 * octet is zero, data will be sent. Otherwise, the octet contains an
 * error value in the top four bits (including EOF), and no data is sent.
 * There are no command replies or checksums. This makes it useful only
 * to load one record by hand at bootstrap.
 */

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
char *strerror(int errno);

/* Commands sent in both directions */
struct vtcmd {
  unsigned char hdr1;		/* Header, 31 followed by 42 (decimal) */
  unsigned char hdr2;
  unsigned char cmd;		/* Command, one of VTC_XXX below */
  				/* Error comes back in top 4 bits */
  unsigned char record;		/* Record we're accessing */
  unsigned char blklo;		/* Block number, in lo/hi format */
  unsigned char blkhi;
  unsigned char sum0;		/* 16-bit checksum */
  unsigned char sum1;		/* 16-bit checksum */
};

/* Header bytes */
#define VT_HDR1		31
#define VT_HDR2		42

/* Commands available */
#define VTC_QUICK	0	/* Quick read, no cksum sent */
#define VTC_OPEN	1
#define VTC_CLOSE	2
#define VTC_READ	3	/* This file only uses READ and OPEN */
#define VTC_WRITE	4
#define VTC_ACK		5

/* Errors returned */
#define VTE_NOREC	1	/* No such record available */
#define VTE_OPEN	2	/* Can't open requested block */
#define VTE_CLOSE	3	/* Can't close requested block */
#define VTE_READ	4	/* Can't read requested block */
#define VTE_WRITE	5	/* Can't write requested block */
#define VTE_NOCMD	6	/* No such command */
#define VTE_EOF		7	/* End of file: no blocks left to read */

#define BLKSIZE		512

/* Static things */
extern int errno;
struct vtcmd vtcmd;		/* Command from client */
struct vtcmd vtreply;		/* Reply to client */
char inbuf[BLKSIZE];		/* Input buffer */
char *port = NULL;		/* Device for serial port */
int portfd;			/* File descriptor for the port */
int recfd = 0;			/* File descriptor for the in-use record */
int lastrec = -1;		/* Last record used */
char *recname[256];		/* Up to 256 records on the tape */


void get_command(struct vtcmd *v)
{
  int i;
  unsigned char sum0, sum1;

getcmd:			/* Get a valid command from the client */
  /* printf("Waiting 1st char in cmd\n"); */
  read(portfd, &v->hdr1, 1);
  /* printf("Just got hdr1 0x%x\n", v->hdr1); */
  if (v->hdr1 != VT_HDR1) goto getcmd;
  read(portfd, &v->hdr2, 1);
  /* printf("Just got hdr2 0x%x\n", v->hdr2); */
  if (v->hdr2 != VT_HDR2) goto getcmd;

  read(portfd, &v->cmd, 1); read(portfd, &v->record, 1);
  read(portfd, &v->blklo, 1); read(portfd, &v->blkhi, 1);
  /* printf("Just got cmd 0x%x\n", v->cmd); */
  /* printf("Just got record 0x%x\n", v->record); */
  /* printf("Just got blklo 0x%x\n", v->blklo); */
  /* printf("Just got blkhi 0x%x\n", v->blkhi); */

  /* Calc. the cksum to date */
  sum0 = VT_HDR1 ^ v->cmd ^ v->blklo;
  sum1 = VT_HDR2 ^ v->record ^ v->blkhi;

  /* All done if a quick read */
  if (v->cmd == VTC_QUICK) return;

  /* Retrieve the block if a WRITE cmd */
  if (v->cmd == VTC_WRITE) {
    for (i = 0; i < BLKSIZE; i++) {
      read(portfd, &inbuf[i], 1); sum0 ^= inbuf[i]; i++;
      read(portfd, &inbuf[i], 1); sum1 ^= inbuf[i];
    }
  }
  /* Get the checksum */
  read(portfd, &(v->sum0), 1);
  /* printf("Just got sum0 0x%x\n", v->sum0); */
  read(portfd, &(v->sum1), 1);
  /* printf("Just got sum1 0x%x\n", v->sum1); */

  /* Try again on a bad checksum */
  if ((sum0 != v->sum0) || (sum1 != v->sum1))
    { putchar('e'); goto getcmd; }

  /* printf("Yay, a valid command!\n"); */
}


/* Reply has been mostly initialised by do_command */
void send_reply()
{
  int i;
#ifdef DELAY
  int j;
#endif

  /* Calculate the checksum */
  vtreply.sum0 = VT_HDR1 ^ vtreply.cmd ^ vtreply.blklo;
  vtreply.sum1 = VT_HDR2 ^ vtreply.record ^ vtreply.blkhi;

			/* Data only on a read with no errors */
  if (vtreply.cmd == VTC_READ) {
    for (i = 0; i < BLKSIZE; i++) {
      vtreply.sum0 ^= inbuf[i]; i++;
      vtreply.sum1 ^= inbuf[i];
    }
  }
  /* Transmit the reply */
  write(portfd, &vtreply.hdr1, 1);
  write(portfd, &vtreply.hdr2, 1);
  write(portfd, &vtreply.cmd, 1);
  write(portfd, &vtreply.record, 1);
  write(portfd, &vtreply.blklo, 1);
  write(portfd, &vtreply.blkhi, 1);

  /* printf("Sent reply: command\n"); */

  if (vtreply.cmd == VTC_READ) {
    for (i = 0; i < BLKSIZE; i++) {
      write(portfd, &inbuf[i], 1);

#ifdef DELAY
      for (j = 0; j < 5120; j++) { ; }	/* Slight delay, why is this needed? */
#endif

    }
    /* printf("Sent reply: data\n"); */
  }

  write(portfd, &vtreply.sum0, 1);
  write(portfd, &vtreply.sum1, 1);

  /* printf("Sent reply: checksum\n"); */

}



#define seterror(x)	vtreply.cmd |= (x<<4);

/* Actually do the command sent to us */
void do_command()
{
  int record, block, i, offset;

  /* First, copy the command to the reply */
  memcpy(&vtreply, &vtcmd, sizeof(vtcmd));

  record = vtcmd.record; block = (vtcmd.blkhi << 8) + vtcmd.blklo;
  offset = block * BLKSIZE;

  /* Open the record if not already open */
  if (record != lastrec) {
    if (recname[record] == NULL) { seterror(VTE_NOREC); return; }

    i = open(recname[record], O_RDWR);
    if (i == -1) { seterror(VTE_NOREC); return; }

    if (record != lastrec) close(recfd);
    recfd = i; lastrec = record;
    printf("\nOpened %s\n", recname[record]);
  }

  switch (vtcmd.cmd) {
    case VTC_OPEN:  break;
    case VTC_CLOSE: putchar('c'); close(recfd); lastrec = -1; break;
    case VTC_QUICK: putchar('q'); vtreply.cmd=0;	/* No errors yet */
    case VTC_READ:  putchar('r');
			/* printf("lseek to %d\n",offset); */
		    i= lseek(recfd, offset, SEEK_SET);
      		    if (i==-1)
      		    	{ printf(" EOF\n"); seterror(VTE_EOF); return; }
   		    i = read(recfd, &inbuf, BLKSIZE);
      		    if (i == 0)
      		        { printf(" EOF\n"); seterror(VTE_EOF); return; }
      		    if (i == -1) { seterror(VTE_READ); return; }
		    if (offset && (offset % 102400) == 0)
			printf("\n%dK sent\n", offset/1024);
      		    break;
    case VTC_WRITE: putchar('w'); i = write(recfd, &inbuf, BLKSIZE);
      		    if (i < 1) { seterror(VTE_WRITE); return; }
      	 	    break;
    default:	    putchar('?');
		    /* printf("Got unknown command %d\n", vtcmd.cmd); */
   		    seterror(VTE_NOCMD); return;
  }
  fflush(stdout);
}

/* The configuration file is .vtrc. The first line holds the name
 * of the serial device. The following lines hold the filenames which
 * are the successive tape records. Lines starting with a hash are ignored.
 * Files are not opened unless they are referenced by a client's command.
 */
void read_config()
{
  FILE *in;
  char *c;
  int i, cnt = 0;

  in = fopen(".vtrc", "r");
  if (in == NULL) {
    fprintf(stderr, "Error opening .vtrc config file: %s\n", strerror(errno));
    exit(1);
  }
  while (cnt != 256) {
    if (feof(in)) break;
    c = fgets(inbuf, BLKSIZE - 2, in);
    if (feof(in)) break;

    if (c == NULL) {
      fprintf(stderr, "Error reading .vtrc config file: %s\n", strerror(errno));
      exit(1);
    }
    if (inbuf[0] == '#') continue;

    inbuf[strlen(inbuf) - 1] = '\0';	/* Remove training newline */

    if (port == NULL) {
      port = (char *) malloc(strlen(inbuf) + 2);
      strcpy(port, inbuf); continue;
    }

    recname[cnt] = (char *) malloc(strlen(inbuf) + 2);
    strcpy(recname[cnt], inbuf); cnt++;
  }
  printf("Records are:\n");
  for (i=0; i<cnt; i++) printf("  %2d %s\n", i, recname[i]);
  printf("\n");

  fclose(in);
}

/* Open the named port and set it to raw mode.
 * Someone else deals with such things as
 * baud rate, clocal and crtscts.
 */
void open_port()
{
  struct termios t;

  printf("Opening port %s .... ", port);
  portfd = open(port, O_RDWR);
  if (portfd == -1) {
    fprintf(stderr, "Error opening device %s: %s\n", port, strerror(errno));
    exit(1);
  }
  printf("Port open\n");

  /* Get the device's terminal attributes */
  if (tcgetattr(portfd, &t) == -1) {
    fprintf(stderr, "Error getting %s attributes: %s\n", port, strerror(errno));
    exit(1);
  }

  /* Set raw - code stolen from 4.4BSD libc/termios.c */
  t.c_iflag &= ~(IMAXBEL | IXOFF | INPCK | BRKINT | PARMRK | ISTRIP |
		 INLCR | IGNCR | ICRNL | IXON | IGNPAR);
  t.c_iflag |= IGNBRK;
  t.c_oflag &= ~OPOST;
  t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON | ISIG | IEXTEN |
		 NOFLSH | TOSTOP | PENDIN);
  t.c_cflag &= ~(CSIZE | PARENB);
  t.c_cflag |= CS8 | CREAD;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;

  /* Set the device's terminal attributes */
  if (tcsetattr(portfd, TCSANOW, &t) == -1) {
    fprintf(stderr, "Error setting %s attributes: %s\n", port, strerror(errno));
    exit(1);
  }
}


void server_loop()
{
  int i;

  while (1) {
    /* printf("Getting a command: "); */
    get_command(&vtcmd);	/* Get a command from the client */

    do_command();		/* Do the command */

    if (vtcmd.cmd==VTC_QUICK) {	/* Only send buffer on a quick read */
	write(portfd, &vtreply.cmd, 1);
	/* printf("Just sent quick byte 0%o\n", vtreply.cmd); */
	for (i=0; i< BLKSIZE; i++) {
	  write(portfd, &inbuf[i], 1);
 	}
	fflush(stdout); continue;
    }

    send_reply();		/* Send the reply */

  }
}


void main()
{
  printf("Virtual tape server, $Revision: 1.13 $ \n");

  read_config();

  open_port();

  server_loop();
  exit(0);
}
