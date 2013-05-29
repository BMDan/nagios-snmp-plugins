/*************************************************************************
 *
 * plugins for Nagios
 *
 * (C) 2002-2008
 *   Henning P. Schmiedehausen
 *   INTERMETA - Gesellschaft fuer Mehrwertdienste mbH
 *   Hutweide 15
 *   D-91054 Buckenhof
 * (C) 2010
 *   Elan Ruusamäe, <glen@delfi.ee>
 *
 *************************************************************************
 *
 * Checks the disks for a given host via snmp and the
 * ucd snmp interface.
 *
 *************************************************************************
 *
 * Distributed under GPL v2.
 *
 * $Id: disk_plugin.c,v 1.6 2002/01/27 22:10:24 henning Exp $
 *
 */

#include "snmp_common.h"

#define RCS_VERSION "$Revision: 1.6 $ ($Date: 2002/01/27 22:10:24 $)"

#define DISK_INDEX_MIB       ".1.3.6.1.4.1.2021.9.1.1"
#define DISK_PATH_MIB        ".1.3.6.1.4.1.2021.9.1.2"
#define DISK_DEVICE_MIB      ".1.3.6.1.4.1.2021.9.1.3"
#define DISK_MINIMUM_MIB     ".1.3.6.1.4.1.2021.9.1.4"
#define DISK_MINPERCENT_MIB  ".1.3.6.1.4.1.2021.9.1.5"
#define DISK_TOTAL_MIB       ".1.3.6.1.4.1.2021.9.1.6"
#define DISK_AVAIL_MIB       ".1.3.6.1.4.1.2021.9.1.7"
#define DISK_USED_MIB        ".1.3.6.1.4.1.2021.9.1.8"
#define DISK_PERCENT_MIB     ".1.3.6.1.4.1.2021.9.1.9"
#define DISK_PERCENTNODE_MIB ".1.3.6.1.4.1.2021.9.1.10"
#define DISK_ERRORFLAG_MIB   ".1.3.6.1.4.1.2021.9.1.100"
#define DISK_ERRORMSG_MIB    ".1.3.6.1.4.1.2021.9.1.101"

#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)  \
  ((byte) & 0x80 ? 1 : 0), \
  ((byte) & 0x40 ? 1 : 0), \
  ((byte) & 0x20 ? 1 : 0), \
  ((byte) & 0x10 ? 1 : 0), \
  ((byte) & 0x08 ? 1 : 0), \
  ((byte) & 0x04 ? 1 : 0), \
  ((byte) & 0x02 ? 1 : 0), \
  ((byte) & 0x01 ? 1 : 0)

int report_disk(void);

int path = 0;       // path mode (0 == default device mode)

int main (int argc, char *argv[])
{
  static struct option long_options[] = {
    { "help",      no_argument,       0, 'h' },
    { "version",   no_argument,       0, 'V' },
    { "timeout",   required_argument, 0, 't' },
    { "critical",  required_argument, 0, 'c' },
    { "community", required_argument, 0, 'C' },
    { "hostname",  required_argument, 0, 'H' },
    { "verbose",   no_argument,       0, 'v' },
    { "list",      no_argument,       0, 'l' },
    { "path",      no_argument,       0, 'p' },
    { 0, 0, 0, 0 },
  };
  int option_index = 0;
  int c;

  int ret = STATE_UNKNOWN;

  bn = strdup(basename(argv[0]));
  version = VERSION;

#define OPTS "?hVvlpt:c:w:C:H:"
  
  while(1)
  {
    c = getopt_long(argc, argv, OPTS, long_options, &option_index);

    if(c == -1 || c == EOF)
      break;

    switch(c)
    {
      case '?':
      case 'h':
        print_help();
        exit(STATE_UNKNOWN);

      case 'V':
        print_version();
        exit(STATE_UNKNOWN);


      case 't':
        if(!is_integer(optarg))
        {
          printf("%s: Timeout interval (%s)must be integer!\n",
                 bn,
                 optarg);
          exit(STATE_UNKNOWN);
        }
        
        timeout = atoi(optarg);
        if(verbose)
          printf("%s: Timeout set to %d\n", bn, timeout);
        break;

      case 'C':
        community = strdup(optarg);

        if(verbose)
          printf("%s: Community set to %s\n", bn, community);
        
        break;

      case 'c':
        if(!is_integer(optarg))
        {
          printf("%s: Minimum free-space percentage (%s) must be integer!\n",
                 bn,
                 optarg);
          exit(STATE_UNKNOWN);
        }

        manual_critical = atoi(optarg);
        if(verbose)
          printf("%s: manual_critical set to %d\n", bn, manual_critical);
        break;

      case 'H':
        hostname = strdup(optarg);

        if(verbose)
          printf("%s: Hostname set to %s\n", bn, hostname);

        break;

      case 'v':
        verbose = 1;
        printf("%s: Verbose mode activated\n", bn);
        break;

      case 'l':
        listing = 1;
        if(verbose)
            printf("%s: List mode activated\n", bn);
        break;

      case 'p':
        path = 1;
        if(verbose)
            printf("%s: Listing mount point activated\n", bn);
        break;
    }
  }

  if(!hostname || !community)
  {
    printf("%s: Hostname or Community missing!\n", bn);
    print_help();
    exit(STATE_UNKNOWN);
  }

  ret = report_disk();

  if(verbose)
    printf("%s: Returning %d\n", bn, ret);

  exit(ret);
}

//Note: This results in BFs that look like:
// 8 7 6 5 4 3 2 1 16 15 14 13 12 11 10 9 24 23 22 21 20 19 18 17 etc.
#define BITFIELD_SET(bf,pos) (bf)[(int) floor((pos)/8)] |= (unsigned char) (1<<((pos)%8))
#define BITFIELD_BIT(bf,pos) (unsigned char) ((bf)[(int) floor((pos)/8)] & (char) (1<<((pos)%8)))
#define BITFIELD_CLR(bf,pos) (bf)[(int) floor((pos)/8)] &= ~((unsigned char) (1<<((pos)%8)))

int report_disk()
{
  int cnt;

  long *errors;
  void *pnt;
  int i;
  unsigned char *gotErrors;
  unsigned char gotAnyErrors = 0;
  char **errormsg = NULL;
  char **diskname = NULL;
  long **disktotal = NULL;
  long **diskused = NULL;
  char **diskpath = NULL;
  
  if((cnt = fetch_table(DISK_INDEX_MIB, null_callback, NULL, 0)) < 0)
  {
    printf("%s: Could not fetch list of disks\n", bn);
    return STATE_UNKNOWN;
  }

  if(!cnt)  // No disks configured
  {
    printf("%s: No disks found.\n", bn);
    return STATE_WARNING;
  }

  /*  if(cnt > sizeof(gotErrors)*8)
  {
    printf("%s: Too many disks (%ld) found.\n", bn, cnt);
    return STATE_UNKNOWN;
  }*/

  if ( !( gotErrors = malloc((cnt/(sizeof(*gotErrors)*8))+1) ) )
  {
    printf("%s: Failed to allocate bytes for gotErrors.\n", bn);
    return STATE_UNKNOWN;
  }

  if (verbose)
  {
    printf( "Memsetting %ld bytes for %ld.  Sizeof=%ld.\n", (cnt/(sizeof(*gotErrors)*8))+1, cnt, sizeof(*gotErrors) );
  }
  memset( gotErrors, (unsigned char) 0, (cnt/(sizeof(*gotErrors)*8))+1 );

  if(!(errors = calloc(sizeof(long), cnt)))
  {
    printf("%s: Could not allocate memory for information\n", bn);
    return STATE_CRITICAL;
  }

  pnt = errors;

  if(fetch_table(DISK_ERRORFLAG_MIB, integer_callback, pnt, cnt) < 0)
  {
    printf("%s: Could not fetch error list!\n", bn);
    return STATE_UNKNOWN;
  }

  if( !(diskpath = calloc(sizeof(char**), cnt) ) )
  {
    printf( "%s: Could not allocate memory for disk paths.\n", bn );
    return STATE_CRITICAL;
  }

  pnt = diskpath;

  if(fetch_table(DISK_PATH_MIB, string_callback, pnt, cnt) < 0 )
  {
    printf("%s: Could not fetch disk paths.\n", bn);
    return STATE_CRITICAL;
  }

  if( !(disktotal = calloc(sizeof(long**), cnt) ) )
  {
    printf( "%s: Could not allocate memory for disk-used percentages.\n", bn );
    return STATE_CRITICAL;
  }

  pnt = disktotal;

  if(fetch_table(DISK_TOTAL_MIB, integer_callback, pnt, cnt) < 0 )
  {
    printf("%s: Could not fetch disk totals.\n", bn);
    return STATE_CRITICAL;
  }

  if( !(diskused = calloc(sizeof(long**), cnt) ) )
  {
    printf( "%s: Could not allocate memory for disk-used percentages.\n", bn );
    return STATE_CRITICAL;
  }

  pnt = diskused;

  if(fetch_table(DISK_PERCENT_MIB, integer_callback, pnt, cnt) < 0 )
  {
    printf("%s: Could not fetch disk percentages.\n", bn);
    return STATE_CRITICAL;
  }

  for(i=0; i<cnt; i++)
  {
    if(verbose)
      printf("%s: Got flag %ld for %d\n", bn, errors[i], i);

    if(disktotal[i] <= 0)
    {
      if(verbose)
      {
        printf( "%s: Skipping disk #%d due to disktotal=%ld.\n", bn, i, disktotal[i] );
      }
      continue;
    }

    if(errors[i])
    {
      // gotErrors = gotErrors | (long) pow(2,i); //Set the appropriate bit high.
      if(verbose)
      {
        //Safe because I do malloc(cnt/...)+1.  Assuming cnt >0, that is.
        printf( "Old bitfield: "BYTETOBINARYPATTERN"-"BYTETOBINARYPATTERN"\n",BYTETOBINARY(*gotErrors),BYTETOBINARY(*(gotErrors+1)) );
        //printf( "Old bitfield: "BYTETOBINARYPATTERN"\n",BYTETOBINARY((*gotErrors) ) );
      }
      BITFIELD_SET(gotErrors,i);
      if(verbose)
      {
        //Safe because I do malloc(cnt/...)+1.  Assuming cnt >0, that is.
        printf( "New bitfield: "BYTETOBINARYPATTERN"-"BYTETOBINARYPATTERN"\n",BYTETOBINARY(*gotErrors),BYTETOBINARY(*(gotErrors+1)) );
      }
      gotAnyErrors = 1;
//      break;
    }
  }

  //Cleanup time: are these *actually* errors?
  if(gotAnyErrors && manual_critical != -1) //We only run this if the OS/snmpd reports the error
  {
    gotAnyErrors = 0;

    for(i=0; i<cnt; i++)
    {
      if(BITFIELD_BIT(gotErrors,i)) //Only if the OS/snmpd reports the error for this particular disk
      {
        if(verbose)
        {
          printf( "%s: Checking diskused (%ld%%) for disk #%d.\n", bn, diskused[i], i );
          printf( "%s: %ld, %ld, %d, %d\n", bn, (long) diskused[i], 100-((long) diskused[i]), 100-((long) diskused[i]), manual_critical );
        }
        if((100-((long) diskused[i])) < manual_critical)
        {
          if(verbose)
          {
            printf( "%s: Throwing error.\n", bn );
          }
          gotAnyErrors = 1;
          //If you remove the gotErrors check, above, you'll need to manually handle the alert here.

          //break;  //optional break here for speed; but if you break, you'll (incorrectly) print disk space errors for other disks
          //that have their error bit set by snmpd but satisfy the manual_critical value.
        }
        else
        {
          BITFIELD_CLR(gotErrors,i);
        }
      }
    }
    if ( !gotAnyErrors )
    {
      printf("%s: OS reported error(s), but free-space minima (%d%%) satisfied on %d disks.\n", bn, manual_critical, cnt);
      return STATE_OK;
    }
    //else: we've got an error.  HANDLE IT (below).
  }

  errormsg = calloc(sizeof(char **), cnt);
  diskname = calloc(sizeof(char **), cnt);
  if(!errormsg || !diskname)
  {
    printf("%s: Could not allocate memory for error information\n", bn);
    return STATE_CRITICAL;
  }

  pnt = errormsg;

  if(fetch_table(DISK_ERRORMSG_MIB, string_callback, pnt, cnt) < 0)
  {
    printf("%s: Could not fetch error messages\n", bn);
    return STATE_CRITICAL;
  }

  pnt = diskname;

  if(fetch_table(path ? DISK_PATH_MIB : DISK_DEVICE_MIB, string_callback, pnt, cnt) < 0)
  {
    printf("%s: Could not fetch device list\n", bn);
    return STATE_CRITICAL;
  }

  if(gotAnyErrors == 0)
  {
    if(listing)
    {
      printf( "Checked %d disks (%s", cnt, diskname[0]);
      for(i=1; i < cnt; i++)
      {
          printf( ", %s", diskname[i] );
      }
      printf(").\n" );
    }
    else
      printf("Checked %d disks.\n", cnt);
    return STATE_OK;
  }
  
  for(i=0; i < cnt; i++)
  {
    if(errors[i]&&BITFIELD_BIT(gotErrors,i))
    {
      if ( verbose )
      {
        printf("%d: %s (%s)\n", i, errormsg[i], diskname[i]);
      }
      else
      {
        printf("%s (%s) has %d%% free.\n",diskpath[i],diskname[i],100-( (long) diskused[i]));
      }
    }
  }

  return STATE_CRITICAL;
}

//Setup VIM: ex: et ts=2 sw=2 enc=utf-8 :
