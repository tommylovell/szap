# szap
superzap utility similar to, but much more limited, than the mainframe program, AMASPZAP

The "help"', "-h", looks like this:

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
