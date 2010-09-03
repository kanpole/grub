/* bsdlabel.c - Read BSD style partition tables.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2005,2006,2007,2008,2009  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/partition.h>
#include <grub/bsdlabel.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/msdos_partition.h>

#ifdef GRUB_UTIL
#include <grub/util/misc.h>
#endif

static struct grub_partition_map grub_bsdlabel_partition_map;
static struct grub_partition_map grub_netbsdlabel_partition_map;
static struct grub_partition_map grub_openbsdlabel_partition_map;



static grub_err_t
iterate_real (grub_disk_t disk, grub_disk_addr_t sector, int freebsd,
	      struct grub_partition_map *pmap,
	      int (*hook) (grub_disk_t disk,
			   const grub_partition_t partition))
{
  struct grub_partition_bsd_disk_label label;
  struct grub_partition p;
  grub_disk_addr_t delta = 0;
  unsigned pos;

  /* Read the BSD label.  */
  if (grub_disk_read (disk, sector, 0, sizeof (label), &label))
    return grub_errno;

  /* Check if it is valid.  */
  if (label.magic != grub_cpu_to_le32 (GRUB_PC_PARTITION_BSD_LABEL_MAGIC))
    {
      grub_dprintf ("partition",
		    "bad signature (found 0x%08x, expected 0x%08x)\n",
		    label.magic,
		    grub_cpu_to_le32 (GRUB_PC_PARTITION_BSD_LABEL_MAGIC));
      return grub_error (GRUB_ERR_BAD_PART_TABLE, "no signature");
    }

  /* A kludge to determine a base of be.offset.  */
  if (GRUB_PC_PARTITION_BSD_LABEL_WHOLE_DISK_PARTITION
      < grub_cpu_to_le16 (label.num_partitions) && freebsd)
    {
      struct grub_partition_bsd_entry whole_disk_be;

      pos = sizeof (label) + sector * GRUB_DISK_SECTOR_SIZE
	+ sizeof (struct grub_partition_bsd_entry)
	* GRUB_PC_PARTITION_BSD_LABEL_WHOLE_DISK_PARTITION;

      if (grub_disk_read (disk, pos / GRUB_DISK_SECTOR_SIZE,
			  pos % GRUB_DISK_SECTOR_SIZE, sizeof (whole_disk_be),
			  &whole_disk_be))
	return grub_errno;

      delta = grub_le_to_cpu32 (whole_disk_be.offset);
    }

  pos = sizeof (label) + sector * GRUB_DISK_SECTOR_SIZE;

  grub_dprintf ("partition", "bsdlabel with %d partitions detected\n",
		grub_cpu_to_le16 (label.num_partitions));

  for (p.number = 0;
       p.number < grub_cpu_to_le16 (label.num_partitions);
       p.number++, pos += sizeof (struct grub_partition_bsd_entry))
    {
      struct grub_partition_bsd_entry be;

      if (p.number == GRUB_PC_PARTITION_BSD_LABEL_WHOLE_DISK_PARTITION)
	continue;

      p.offset = pos / GRUB_DISK_SECTOR_SIZE;
      p.index = pos % GRUB_DISK_SECTOR_SIZE;

      if (grub_disk_read (disk, p.offset, p.index, sizeof (be),  &be))
	return grub_errno;

      p.start = grub_le_to_cpu32 (be.offset);
      p.len = grub_le_to_cpu32 (be.size);
      p.partmap = pmap;

      grub_dprintf ("partition",
		    "partition %d: type 0x%x, start 0x%llx, len 0x%llx\n",
		    p.number, be.fs_type,
		    (unsigned long long) p.start,
		    (unsigned long long) p.len);

      if (p.len == 0)
	continue;

      if (p.start < delta)
	{
#ifdef GRUB_UTIL
	  char *partname;
#endif
	  grub_dprintf ("partition",
			"partition %d: invalid start (found 0x%llx, wanted >= 0x%llx)\n",
			p.number,
			(unsigned long long) p.start,
			(unsigned long long) delta);
#ifdef GRUB_UTIL
	  /* disk->partition != NULL as 0 < delta */
	  partname = grub_partition_get_name (disk->partition);
	  grub_util_warn ("Discarding improperly nested partition (%s,%s,%s%d)",
			  disk->name, partname, p.partmap->name, p.number + 1);
	  grub_free (partname);
#endif
	  continue;
	}

      p.start -= delta;

      if (hook (disk, &p))
	return grub_errno;
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
bsdlabel_partition_map_iterate (grub_disk_t disk,
				int (*hook) (grub_disk_t disk,
					     const grub_partition_t partition))
{

  if (disk->partition && grub_strcmp (disk->partition->partmap->name, "msdos")
      == 0 && disk->partition->msdostype == GRUB_PC_PARTITION_TYPE_FREEBSD)
    {
      grub_dprintf ("partition", "FreeBSD embedded iterating\n");
      return iterate_real (disk, GRUB_PC_PARTITION_BSD_LABEL_SECTOR, 1,
			   &grub_bsdlabel_partition_map, hook);
    }

  if (disk->partition 
      && (grub_strcmp (disk->partition->partmap->name, "msdos") == 0
	  || disk->partition->partmap == &grub_bsdlabel_partition_map
	  || disk->partition->partmap == &grub_netbsdlabel_partition_map
	  || disk->partition->partmap == &grub_openbsdlabel_partition_map))
    {
      grub_dprintf ("partition", "no embedded iterating\n");
      return grub_error (GRUB_ERR_BAD_PART_TABLE, "no embedding supported");
    }

  return iterate_real (disk, GRUB_PC_PARTITION_BSD_LABEL_SECTOR, 0, 
		       &grub_bsdlabel_partition_map, hook);
}

static grub_err_t
netopenbsdlabel_partition_map_iterate (grub_disk_t disk, grub_uint8_t type,
				       struct grub_partition_map *pmap,
				       int (*hook) (grub_disk_t disk,
						    const grub_partition_t partition))
{
  grub_err_t err;

  if (disk->partition && grub_strcmp (disk->partition->partmap->name, "msdos")
      == 0)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "no embedding supported");

  {
    struct grub_msdos_partition_mbr mbr;
    unsigned i;

    err = grub_disk_read (disk, 0, 0, sizeof (mbr), &mbr);
    if (err)
      return err;

    for (i = 0; i < ARRAY_SIZE (mbr.entries); i++)
      if (mbr.entries[i].type == type)
	{
	  err = iterate_real (disk, mbr.entries[i].start
			      + GRUB_PC_PARTITION_BSD_LABEL_SECTOR, 0, pmap,
			      hook);
	  if (err != GRUB_ERR_BAD_PART_TABLE)
	    return err;
	}
  }

  return grub_error (GRUB_ERR_BAD_PART_TABLE, "no bsdlabel found");
}

static grub_err_t
netbsdlabel_partition_map_iterate (grub_disk_t disk,
				   int (*hook) (grub_disk_t disk,
						const grub_partition_t partition))
{
  return netopenbsdlabel_partition_map_iterate (disk,
						GRUB_PC_PARTITION_TYPE_NETBSD,
						&grub_netbsdlabel_partition_map,
						hook);
}

static grub_err_t
openbsdlabel_partition_map_iterate (grub_disk_t disk,
				   int (*hook) (grub_disk_t disk,
						const grub_partition_t partition))
{
  return netopenbsdlabel_partition_map_iterate (disk,
						GRUB_PC_PARTITION_TYPE_OPENBSD,
						&grub_openbsdlabel_partition_map,
						hook);
}


static struct grub_partition_map grub_bsdlabel_partition_map =
  {
    .name = "bsd",
    .iterate = bsdlabel_partition_map_iterate,
  };

static struct grub_partition_map grub_openbsdlabel_partition_map =
  {
    .name = "openbsd",
    .iterate = openbsdlabel_partition_map_iterate,
  };

static struct grub_partition_map grub_netbsdlabel_partition_map =
  {
    .name = "netbsd",
    .iterate = netbsdlabel_partition_map_iterate,
  };



GRUB_MOD_INIT(part_bsd)
{
  grub_partition_map_register (&grub_bsdlabel_partition_map);
  grub_partition_map_register (&grub_netbsdlabel_partition_map);
  grub_partition_map_register (&grub_openbsdlabel_partition_map);
}

GRUB_MOD_FINI(part_bsd)
{
  grub_partition_map_unregister (&grub_bsdlabel_partition_map);
  grub_partition_map_unregister (&grub_netbsdlabel_partition_map);
  grub_partition_map_unregister (&grub_openbsdlabel_partition_map);
}
