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
** map046.c: Mapper #46 (Pelican Game Station)
** Implemented by Firebug with information courtesy of Kevin Horton
**
*/

#include <nofrendo.h>
#include <mmc.h>
#include <nes.h>

static uint8 prg_low_bank;
static uint8 chr_low_bank;
static uint8 prg_high_bank;
static uint8 chr_high_bank;

/*************************************************/
/* Set banks from the combined register values   */
/*************************************************/
static void map46_set_banks(void)
{
  /* Set the PRG and CHR pages */
  mmc_bankrom(32, 0x8000, (prg_high_bank << 1) | (prg_low_bank));
  mmc_bankvrom(8, 0x0000, (chr_high_bank << 3) | (chr_low_bank));
}

/*********************************************************/
/* Mapper #46: Pelican Game Station (aka Rumble Station) */
/*********************************************************/
static void map46_init(rom_t *cart)
{
   UNUSED(cart);

   /* High bank switch register is set to zero on reset */
   prg_high_bank = 0x00;
   chr_high_bank = 0x00;
   map46_set_banks();
}

/******************************************/
/* Mapper #46 write handler ($6000-$FFFF) */
/******************************************/
static void map46_write(uint32 address, uint8 value)
{
   /* $8000-$FFFF: D6-D4 = lower three bits of CHR bank */
   /*              D0    = low bit of PRG bank          */
   /* $6000-$7FFF: D7-D4 = high four bits of CHR bank   */
   /*              D3-D0 = high four bits of PRG bank   */
   if (address & 0x8000)
   {
      prg_low_bank = value & 0x01;
      chr_low_bank = (value >> 4) & 0x07;
      map46_set_banks ();
   }
   else
   {
      prg_high_bank = value & 0x0F;
      chr_high_bank = (value >> 4) & 0x0F;
      map46_set_banks ();
   }
}

static const mem_write_handler_t map46_memwrite[] =
{
   { 0x6000, 0xFFFF, map46_write },
   LAST_MEMORY_HANDLER
};

mapintf_t map46_intf =
{
   46,                      /* Mapper number */
   "Pelican Game Station",  /* Mapper name */
   map46_init,              /* Initialization routine */
   NULL,                    /* VBlank callback */
   NULL,                    /* HBlank callback */
   NULL,                    /* Get state (SNSS) */
   NULL,                    /* Set state (SNSS) */
   NULL,                    /* Memory read structure */
   map46_memwrite,          /* Memory write structure */
   NULL                     /* External sound device */
};
