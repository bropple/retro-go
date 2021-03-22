/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** map015.c: Contra 100-in-1 mapper interface
**
*/

#include <nofrendo.h>
#include <mmc.h>

static void map15_write(uint32 address, uint8 value)
{
   int bank = value & 0x3F;
   uint8 swap = (value & 0x80) >> 7;

   switch (address & 0x3)
   {
   case 0:
      mmc_bankrom(8, 0x8000, (bank << 1) + swap);
      mmc_bankrom(8, 0xA000, (bank << 1) + (swap ^ 1));
      mmc_bankrom(8, 0xC000, ((bank + 1) << 1) + swap);
      mmc_bankrom(8, 0xE000, ((bank + 1) << 1) + (swap ^ 1));

      if (value & 0x40)
         ppu_setmirroring(PPU_MIRROR_HORI);
      else
         ppu_setmirroring(PPU_MIRROR_VERT);
      break;

   case 1:
      mmc_bankrom(8, 0xC000, (bank << 1) + swap);
      mmc_bankrom(8, 0xE000, (bank << 1) + (swap ^ 1));
      break;

   case 2:
      if (swap)
      {
         mmc_bankrom(8, 0x8000, (bank << 1) + 1);
         mmc_bankrom(8, 0xA000, (bank << 1) + 1);
         mmc_bankrom(8, 0xC000, (bank << 1) + 1);
         mmc_bankrom(8, 0xE000, (bank << 1) + 1);
      }
      else
      {
         mmc_bankrom(8, 0x8000, (bank << 1));
         mmc_bankrom(8, 0xA000, (bank << 1));
         mmc_bankrom(8, 0xC000, (bank << 1));
         mmc_bankrom(8, 0xE000, (bank << 1));
      }
      break;

   case 3:
      mmc_bankrom(8, 0xC000, (bank << 1) + swap);
      mmc_bankrom(8, 0xE000, (bank << 1) + (swap ^ 1));

      if (value & 0x40)
         ppu_setmirroring(PPU_MIRROR_HORI);
      else
         ppu_setmirroring(PPU_MIRROR_VERT);
      break;

   default:
      break;
   }
}

static void map15_init(rom_t *cart)
{
   UNUSED(cart);
   mmc_bankrom(32, 0x8000, 0);
}

static const mem_write_handler_t map15_memwrite[] =
{
   { 0x8000, 0xFFFF, map15_write },
   LAST_MEMORY_HANDLER
};

mapintf_t map15_intf =
{
   15,               /* mapper number */
   "Contra 100-in-1",/* mapper name */
   map15_init,       /* init routine */
   NULL,             /* vblank callback */
   NULL,             /* hblank callback */
   NULL,             /* get state (snss) */
   NULL,             /* set state (snss) */
   NULL,             /* memory read structure */
   map15_memwrite,   /* memory write structure */
   NULL              /* external sound device */
};
