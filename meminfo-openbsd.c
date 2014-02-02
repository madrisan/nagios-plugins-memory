/*
 * License: GPLv2
 * Copyright (c) 2014 Davide Madrisan <davide.madrisan@gmail.com>
 *
 * A Nagios plugin to check memory and swap usage on linux
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This software is based on the source code of the tool "free" (procps 3.2.8)
 */

#include "config.h"

# include <sys/param.h>
# include <sys/types.h>
# include <sys/mount.h>
# include <sys/sysctl.h>
# include <sys/swap.h>
# include <unistd.h>    /* getpagesize */

#include <fcntl.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nputils.h"

# define NUM_AVERAGES    3
  /* Log base 2 of 1024 is 10 (2^10 == 1024) */
# define LOG1024         10

/* these are for getting the memory statistics */
static int      pageshift;      /* log base 2 of the pagesize */

/* define pagetok in terms of pageshift */
#define pagetok(size) ((size) << pageshift)

#define SU(X) ( ((unsigned int)(X) << 10) >> shift), units

int kb_main_used;
int kb_main_cached;
int kb_main_free;
int kb_main_total;
int kb_swap_used;
int kb_swap_free;
int kb_swap_total;

int
get_system_pagesize()
{
  int pagesize;

  /*
   * get the page size with "getpagesize" and calculate pageshift from
   * it
   * fixme: we should use sysconf(_SC_PAGESIZE) instead
   */
  pagesize = getpagesize ();
  pageshift = 0;
  while (pagesize > 1)
    {
      pageshift++;
      pagesize >>= 1;
    }

  /* we only need the amount of log(2)1024 for our conversion */
  pageshift -= LOG1024;

  return (0);
}

int
swapmode (int *used, int *total)
{
  struct swapent *swdev;
  int nswap, rnswap, i;

  nswap = swapctl (SWAP_NSWAP, 0, 0);
  if (nswap == 0)
    return 0;

  swdev = calloc (nswap, sizeof (*swdev));
  if (swdev == NULL)
    return 0;

  rnswap = swapctl (SWAP_STATS, swdev, nswap);
  if (rnswap == -1)
    {
      free (swdev);
      return 0;
    }

  /* if rnswap != nswap, then what? */

  /* Total things up */
  *total = *used = 0;
  for (i = 0; i < nswap; i++)
    {
      if (swdev[i].se_flags & SWF_ENABLE)
        {
          *used += (swdev[i].se_inuse / (1024 / DEV_BSIZE));
          *total += (swdev[i].se_nblks / (1024 / DEV_BSIZE));
        }
    }
  free (swdev);
  return 1;
}

void
meminfo (void)
{
  static int vmtotal_mib[] = { CTL_VM, VM_METER };
  static int bcstats_mib[] = { CTL_VFS, VFS_GENERIC, VFS_BCACHESTAT };
  struct vmtotal vmtotal;
  struct bcachestats bcstats;
  size_t size;

  if (get_system_pagesize() == -1)
    { 
      fputs("RUNTIME ERROR: get_system_pagesize failed\n", stdout);
      exit(STATE_UNKNOWN);
    }

  /* get total -- systemwide main memory usage structure */
  size = sizeof (vmtotal);
  if (sysctl (vmtotal_mib, 2, &vmtotal, &size, NULL, 0) < 0)
    { 
      bzero (&vmtotal, sizeof (vmtotal));
      fputs("RUNTIME ERROR: sysctl failed\n", stdout);
      exit(STATE_UNKNOWN);
    } 
  size = sizeof (bcstats);
  if (sysctl (bcstats_mib, 3, &bcstats, &size, NULL, 0) < 0)
    { 
      fputs("RUNTIME ERROR: sysctl failed\n", stdout);
      bzero (&bcstats, sizeof (bcstats));
      exit(STATE_UNKNOWN);
    } 

  /* convert memory stats to Kbytes */
  kb_main_total = pagetok (vmtotal.t_rm);
  kb_main_used = pagetok (vmtotal.t_arm);
  kb_main_free = pagetok (vmtotal.t_free);;
  kb_main_cached = pagetok (bcstats.numbufpages);

  if (!swapmode (&kb_swap_used, &kb_swap_total))
    {
      kb_swap_total = 0;
      kb_swap_used = 0;
    }
  kb_swap_free = kb_swap_total - kb_swap_used;
}
