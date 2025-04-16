/*
MIT License

Copyright (c) 2025 tom lovell

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* Whew.  Do I really need that? Anyone can use it; anyway they want to; */
/* I didn't invent anything; others may have copywrite rights; don't sue me. */

/*
    This is a C program that "zap's" the contents of a file, similar but not identical
    to the IBM utility SPZAP, Superzap, IMASPZAP or AMASPZAP on the mainframe.  It is
    not nearly as sophisticated.

    The "disk" (name or dump) that the executable runs against, can any Linux file
    that you have acces to. So, it can be a device (e.g. /dev/mmcblk0) or a file
    (e.g. 2022-09-22-raspios-bullseye-armhf.img).

    This is what the makefile can look like for the following C code. It links into
    the same directory as the C source but is then moved to a more universally access-
    ible location (hopefully in everyone's PATH). It copies the executable to '~/bin'
    (which is in the Raspberry Pi $PATH for your uid, so it is easier to execute).
    Conversely, you can move the executable into '/usr/local/bin' by replacing the
    'sudo mv' command with:
        sudo mv ${PROGS} /usr/local/bin/${PROGS}
    Remember it's a makefile, so tab out to 'sudo'. Also, you won't have to 'sudo'
    the executable if it's 'chown'd/'chmod'd:

TEMPFILES = *.o *.out
PROGS = szap

all:
        gcc -o ${PROGS} ${PROGS}.c
        sudo chown root:root ${PROGS}
        sudo chmod u+s       ${PROGS}
        if [ ! -e ~/bin ]; then mkdir ~/bin/; fi
        sudo mv ${PROGS} ~/bin/${PROGS}

clean:
        -rm -f ${PROGS} ${TEMPFILES}
        sudo rm ~/bin/${PROGS}

    By doing the chown/chmod, You won't have to 'sudo'. I know, huge security hole.
    Remove the chown/chmod if your system is accessible by someone other than you.
    There is a check done to make sure you are EUID == root, as a warning in case
    the file(s) is/are inaccessible by your uid.

    Or you can just do 'gcc -o szap szap.c' and 'sudo ./szap'.  Your option.

    This program can be run interactively, but is more useful when run in a shell
    script, something like this:
      szap <<EOF
      dump /dev/sdb 0200 00  dump the 1st sector comment
      name /dev/sdb (this is unrecognised so it too is a comment)
      ver 1c2 0x07
      rep 0X1c2 0C
      dump /dev/sdb 0200 00 (hex length and hex skip...)
      EOF

    On some errors (like missing or bad filenames), the program reports the error
    and exits.  On other errors (like a bad 'ver') it reports the error, sets the
    'dryrun' flag, and continues.  The 'dryrun' flag prevents writes, but can be
    reset by (duh) 'reset'..

    The first token of the any control card is a verb: 'name', 'ver'/'verify',
    'rep', 'dump', or 'reset'.  These verbs are in the format, and are, pretty much,
    self-explanitory:
      name <filename>
      ver <offset> <data>
      verify same
      rep <offset> <data>
      dump <name> [<length> [<skip>]]
      reset

    The <offset> and <data> are hex and may be preceeded by '0x' or '0X'.  The <data>
    must be pairs of valid hex digits. Commas are not allowed in required fields: nor
    is continuation of a control card, but as control cards can be 4096 bytes long,
    that probably isn't necessary. Not much validity checking is done.

    Anything unrecognised is considered a comment.

    AS ALWAYS: MAKE SURE THAT YOU HAVE A GOOD BACKUP AND RESTORE PROCESS, as this
    program can make a system un-bootable real quick!
*/

#define VERS "0.99"

#define   _LARGEFILE_SOURCE
#define   _LARGEFILE64_SOURCE
#define  __USE_LARGEFILE64
#define  __USE_FILE_OFFSET64
#include  <sys/types.h>
#include  <unistd.h>

/*  -------------  */
/*  default flags  */
/*  -------------  */
#define  DEBUG_FLAG  0 /* '1' prints debug info, i.e., a hexdump of certain areas; '0' does not */
#define  OK_TO_WRITE 0 /* '1' write; '0' don't write */

#include  <stdlib.h>
#include  <stdio.h>
#include  <string.h>
#include  <getopt.h>    /*  for getopt_long  */
#include  <fcntl.h>     /*  for open/close  */
#define   SRCSIZE 4096
#define  DESTSIZE 2045
#define LENTODUMP 512
#define      SKIP 0

uint do_offset(char *p);
uint do_data(char *dest, char *src);
void strtolower(char *s);
void hexDump(char *desc, void *addr, int len);

int  debug=DEBUG_FLAG;  /*  'debug' is a global value, it's here so    */
                        /*  we don't have to pass it to each function  */
int  ok_to_write=OK_TO_WRITE;  /*  ditto                               */

/*  --------------  */
/*    ----------    */
/*    here we go    */
/*    ----------    */
/*  --------------  */
int main(int argc, char *argv[]) {
int  fd;  /*  file descriptor of file being 'zap'ped  */
char fn[128]="";
int  ret, i, c;
int  datalen  = 0;
int  errno, errsv;
uint len  = 0;
uint skip = 0;
uint ui;
char *p;
char buf[SRCSIZE];
char data[DESTSIZE];

    if (geteuid() != 0) {  //  this can be to 'stderr' or via 'printf', if you wish...
        fprintf(stdout, "EUID not 0; you may have to run as root, or sudo, to access a disk device or file\n");
    }

    /*  --------------------------------------------  */
    /*  process any arguments passed to this program  */
    /*  --------------------------------------------  */
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"debug",      no_argument,       0, 'x'},
            {"dryrun",     no_argument,       0, 'd'},
            {"help",       no_argument,       0, 'h'},
            {"version",    no_argument,       0, 'v'},
            {0,            0,                 0,  0}
        };  /* end of 'struct'  */

        c = getopt_long(argc, argv, "dhvonp:", long_options, &option_index);
        if (c == -1) break;

        switch (c) {
            case 'x':
                printf("**  debug mode set  **\n");
                debug=1;  /*  just set the debug flag  */
                break;

            case 'd':
                printf("**  dryrun mode set  **\n");
                ok_to_write=0;  /*  just set the dryrun mode flag  */
                break;

            case 'h':
                printf("  %s [-d] [-h] [-v] \n\n", argv[0]);
                printf("This is a C program that \"zap's\" the contents of a file, similar, but not identical\n");
                printf("to, the IBM utility SPZAP, Superzap, IMASPZAP or AMASPZAP on the mainframe.  It is\n");
                printf("not nearly as sophisticated.  It has three command line flags:\n");
                printf(" --debug (-x) - set debug mode. output some debug info.\n");
                printf(" --dryrun (-d) - set dryrun mode. ino writes will be performed.\n");
                printf(" -h - output this text and exit.\n");
                printf(" -v - display the version information and exit.\n");
                printf("Primary input is via stdin (i.e. the console) or '<<' redirection in a shell script.\n\n");
                printf("Control cards are simple:\n");
                printf("    name <filename>\n");
                printf("    ver  <offset> <data>\n");
                printf("    rep  <offset> <data>\n");
                printf("    dump <filename> <length> <skip>\n");
                printf(" (anything unrecognised is ignored)\n");
                printf("It would make sense to place the name(s) and ver(s) before the (name(s) and) rep(s),\n");
                printf("as \"failed vers\" set a switch to force a \"read-only\" mode\n");
                printf("\n");
                exit(0);

            case 'v':
                printf("'%s', version %s\n", argv[0], VERS);
                exit(0);

            default:
                printf("?? getopt returned character code %c ??\n", c);

        }  /*  end of 'switch'  */
    }  /*  end of 'while'  */

    printf("***  Superuser ZAP, '%s', version %s  ***\n", argv[0], VERS);

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        printf("> %s", buf); // (each control cards ialready has a nl at the end...)
        p = buf;

        if ((p = strtok(p, " \t\n")) == NULL) continue;
        // todo: tranlate verb to lc. strtolower is just a stub.
        strtolower(p); // tranlate verb to lc so strcmp's below are easier to do

        if (strcmp(p, "verify") == 0 || strcmp(p, "ver") == 0) {
            /*  ----------------------------------------  */
            /*  we have a 'verify' or 'ver' control card  */
            /*  ----------------------------------------  */
            /*  get next token (offset)                   */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                printf("missing offset; exiting\n");
//              exit(4);
            }
            /*  ----------------------------------------  */
            /*  convert offset                            */
            /*  ----------------------------------------  */
            skip = do_offset(p);
            if(debug) printf("--> offset in hex: %x\n", skip);

            /*  ----------------------------------------  */
            /*  get next token (data)                     */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                printf("missing data; exiting\n");
//              exit(4);
            }
            /*  ----------------------------------------  */
            /*  convert data                              */
            /*  ----------------------------------------  */
            datalen = do_data(data, p);
            if(debug) printf("--> datalen in hex = %x\n", datalen);
            /*  ----------------------------------------  */
            /*  position the file to the offset           */
            /*  ----------------------------------------  */
            if (skip != 0) lseek64(fd, skip, SEEK_SET);
            /*  ----------------------------------------  */
            /*  then read datalen bytes into buf          */
            /*  ----------------------------------------  */
            read(fd, buf, datalen);
            /*  ----------------------------------------  */
            /*  compare what's on disk to data in ver     */
            /*  cmd and display if they don't agree       */
            /*  ----------------------------------------  */
            if (!memcmp(data, buf, datalen) == 0) {
                printf("'data' discompares; no writes will be performed\n");
                if(debug) hexDump("--> hexDump of data in ver", data, datalen);
                hexDump("--> hexDump of data in named file", buf, datalen);
                ok_to_write = 0;
            } else {
            /*  ----------------------------------------  */
            /*  else, they agree; display if debug        */
            /*  ----------------------------------------  */
                if(debug) hexDump("--> hexDump of data in ver", data, datalen);
	    }

            continue;

        } else if (strcmp(p, "rep") == 0) {
            /*  ----------------------------------------  */
            /*  we have a 'rep' control card              */
            /*  ----------------------------------------  */
            /*  get next token (offset)                   */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                printf("missing offset; exiting\n");
//              exit(4);
            }
            /*  ----------------------------------------  */
            /*  convert offset                            */
            /*  ----------------------------------------  */
            skip = do_offset(p);
            if(debug) printf("--> offset in hex: %x\n", skip);

            /*  ----------------------------------------  */
            /*  get next token (data)                     */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                printf("missing offset; exiting\n");
//              exit(4);
            }
            /*  ----------------------------------------  */
            /*  convert data                              */
            /*  ----------------------------------------  */
            datalen = do_data(data, p);
            if(debug) {
                printf("--. datalen in hex = %x\n", datalen);
                hexDump("--> hexDump of data: ", data, datalen);
            }
            /*  ----------------------------------------  */
            /*  position the file to the offset           */
            /*  ----------------------------------------  */
            if (skip != 0) lseek64(fd, skip, SEEK_SET);
            /*  ----------------------------------------  */
            /*  write the data to the file                */
            /*  ----------------------------------------  */
            if(ok_to_write) printf("write will be done\n");
            else            printf("write will NOT be done\n");
            if(ok_to_write) write(fd, buf, datalen);

            continue;

        } else if (strcmp(p, "name") == 0) {
            /*  ----------------------------------------  */
            /*  we have a 'name' control card             */
            /*  ----------------------------------------  */
            /*  get next token (file name)                */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                printf("<fn> missing; exiting.\n");
//              exit(4);
            } else {
                if (strcmp(fn, "")) {
                    if(debug) printf("closing %s\n", fn);
                    close(fd);
                }
                strcpy(fn, p); // don't tr fn as linux is case sensitive and fn may be mixed case
                if ((fd = open(fn, O_RDWR | O_LARGEFILE)) == -1) {
                    errsv = errno;
                    fprintf(stderr, "The input file '%s' could not be opened\n", fn);
                    fprintf(stderr, " errno is '%i - %s'\n", errsv, strerror(errsv));
                    exit(4);
                }
            }
            continue;

        } else if (strcmp(p, "dump") == 0) {
            /*  ----------------------------------------  */
            /*  we have a dump control card               */
            /*  ----------------------------------------  */
            /*  get next token (file name)                */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                printf("<fn> missing; exiting.\n");
                continue;  //  temp - ignore bad card
                exit(4);
            } else {
                if (strcmp(fn, "")) {
                    if(debug) printf("closing %s\n", fn);
                    close(fd);
                }
                strcpy(fn, p); // don't tr fn as linux is case sensitive and fn may be mixed case
                if ((fd = open(fn, O_RDONLY | O_LARGEFILE)) == -1) {
                    perror("open");
                    printf("(filename=%s)\n", fn);
                    continue;  //  temp - ignore bad card
                    exit(4);
                }
            }
            /*  ----------------------------------------  */
            /*  get next token (length)                   */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                len = LENTODUMP;
                if(debug) printf("--> length missing; default to %i\n", LENTODUMP);
            } else {
                len = do_offset(p); //  get length
                if(debug) printf("--> length is specified and equals %i\n", len);
		//  todo: validate length...
            }
            /*  ----------------------------------------  */
            /*  get next token (skip)                     */
            /*  ----------------------------------------  */
            if ((p = strtok(NULL, " \t\n")) == NULL) {
                skip = SKIP;
                if(debug) printf("--> skip missing; default to %i\n", SKIP);
            } else {
                skip = do_offset(p); //  get skip
                if(debug) printf("--> skip is specified and equals %i\n", skip);
            }
            /*  -------  */
            /*  lseek64  */
            /*  -------  */
	    if (skip != 0) lseek64(fd, skip, SEEK_SET);
	    /*  ----  */
            /*  read  */
	    /*  ----  */
	    read(fd, data, len);
            /*  ---------  */
            /*  then dump  */
            /*  ---------  */
	    hexDump("dump", data, len);

            continue;

        } else if (strcmp(p, "reset") == 0) {
            /*  ----------------------------------------  */
            /*  we have a 'reset' control card            */
            /*  ----------------------------------------  */
            ok_to_write = 1;

            continue;

        } else {
            /*  -----------------  */
            /*  unknown statement  */
            /*  -----------------  */
            printf("--> (the above assumed to be a comment)\n");
        }

    } // end of 'while (fgets(buf,'

    printf("*** end of control cards ***\n");
    if (!strcmp(fn, "")) {
        if(debug) printf("--> closing %s\n", fn);
        close(fd);
    }
    exit(EXIT_SUCCESS);
} // end of 'main()'

/*  ---------------------------------------------------  */
/*  ---------------------------------------------------  */
/*                                                       */
/*  do a conversion from ascii hex to little endian int  */
/*                                                       */
/*  ---------------------------------------------------  */
/*  ---------------------------------------------------  */
uint do_offset(char *p) {
uint ui;
int  ret;
    if ((ret = sscanf(p, "%x", &ui)) != 1) {
        perror("sscanf offset");
        ret = EOF;
    }
    if (ret == EOF) ui = 0;
    return ui;
}

/*  ------------------------------------------------------------  */
/*  ------------------------------------------------------------  */
/*                                                                */
/*  do a conversion from ascii hex to a big endian binary string  */
/*                                                                */
/*  ------------------------------------------------------------  */
/*  ------------------------------------------------------------  */
uint do_data(char *destP, char *srcP) {
// must be "pairs" and 0-9, A-F, or a-f
int    ret, i;
size_t destlen;
size_t srclen;
    /*  ----------------------------------------------------------------  */
    /*  initial size of src data; may be reduced if prefixed by 0x or 0X  */
    /*  ----------------------------------------------------------------  */
    srclen = strlen(srcP);
    /*  -------------------------------------  */
    /*  not necessary, but we'll do it anyway  */
    /*  -------------------------------------  */
    memset(destP, 0, DESTSIZE);
    /*  --------------------------------  */
    /*  remove 0x or 0X, if it exists     */
    /*  --------------------------------  */
    if (((memcmp(srcP, "0x", 2)) == 0) || (memcmp(srcP, "0X", 2)) == 0) {
        srcP += 2;
        srclen -= 2;
    }
    /*  ------------------------------------------  */
    /*  is it pairs of ascii hex characters         */
    /*  ------------------------------------------  */
        if ((srclen % 2) == 1) {
        printf("'data' must be pairs of hex bytes; srclen is %i; exiting\n", (int) srclen);
        hexDump("srcP: ", srcP, srclen);
        exit(4);
    }
    /*  ------------------------------------------  */
    /*  now that we know srclen we can set destlen  */
    /*  ------------------------------------------  */
    destlen = srclen / 2;
    /*  ----------------------  */
    /*  now process the source  */
    /*  ----------------------  */
    for (i = 0; i < destlen; i++) {
        /*  -----------  */
        /*  sscanf data  */
        /*  -----------  */
        if ((ret = sscanf(srcP, "%02x", (unsigned int *) destP)) != 1) {
            perror("sscanf data error");
            exit(4);
        }
        /*  -------------  */
        /*  bump pointers. */
        /*  -------------  */
        srcP += 2;
        destP++;
    }
    /*  --------------  */
    /*  return destlen  */
    /*  --------------  */
    return destlen;
}

/*  ------------------------------------------  */
/*  ------------------------------------------  */
/*  function to convert a string to lower case  */
/*  ------------------------------------------  */
/*  ------------------------------------------  */
void strtolower(char *p) {
}

/*  ---------------------------------------  */
/*  ---------------------------------------  */
/*  function to hexprint an area of storage  */
/*  ---------------------------------------  */
/*  ---------------------------------------  */

/*
MIT License

Copyright (c) 2019 tom lovell

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <stdio.h>

void hexDump(char *desc, void *addr, int len) {
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    // Output description if given.
    if (desc != NULL)
        printf ("%s\n", desc);

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with lne offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the addr. of the data in storage
//          printf ("  %p, ", pc+i);
            // and the offset into that area
            printf ("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf ("  %s\n", buff);
}
