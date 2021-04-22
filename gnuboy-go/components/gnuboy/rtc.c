#include <sys/time.h>
#include <stdio.h>
#include <time.h>

#include "emu.h"
#include "mem.h"
#include "rtc.h"

rtc_t rtc;

// Set in the far future for VBA-M support
#define RT_BASE 1893456000

static const char *SETTING_RTC_DST      = "RTCdst";


void rtc_reset(bool hard, i2c_dev_t dev)
{
	if (hard)
	{
		memset(&rtc, 0, sizeof(rtc));
		rtc_sync(dev);
	}
}

void rtc_sync(i2c_dev_t dev)
{
	time_t timer = time(NULL);
	struct tm *info = localtime(&timer);

	rtc.d = info->tm_yday;
	rtc.h = info->tm_hour;
	rtc.m = info->tm_min;
	rtc.s = info->tm_sec;

	MESSAGE_INFO("Clock set to day %03d at %02d:%02d:%02d\n", rtc.d, rtc.h, rtc.m, rtc.s);
}

void rtc_latch(byte b)
{
	if ((rtc.latch ^ b) & b & 1)
	{
		rtc.regs[0] = rtc.s;
		rtc.regs[1] = rtc.m;
		rtc.regs[2] = rtc.h;
		rtc.regs[3] = rtc.d;
		rtc.regs[4] = rtc.flags;
		rtc.regs[5] = 0xff;
		rtc.regs[6] = 0xff;
		rtc.regs[7] = 0xff;
	}
	rtc.latch = b;
}

bool DS3231_pokeTimeUpdate(i2c_dev_t dev){
    
    /*  The purpose of this function is to overwrite the time value of 
     *  Pokemon Crystal, Pokemon Silver, or Pokemon Gold with the DS3231's
     *  own time value in order to synchronize time like the actual cartridge.
     * 
     *  This should also work with ROM hacks of the game, as long as the ROM name
     *  in the header is the same.
     */
    
    //Default time value: SUNDAY 12:00:00 AM -> Day 0 00:00:00;
    
    //we will first set the start time in the game to zero so the default time value is in effect, and is known.
    
    if(strcmp(rom.name, "PM_CRYSTAL") == 0) { //if the game is Pokemon Crystal or related ROM hack
        mem_write(0xD4B6, 0x00); //wStartDay
        mem_write(0xD4B7, 0x00); //wStartHour
        mem_write(0xD4B8, 0x00); //wStartMinute
        mem_write(0xD4B9, 0x00); //wStartSecond
    }
    else if(strcmp(rom.name, "POKEMON_SLVAAXE▒") == 0 || strcmp(rom.name, "POKEMON_GLDAAUE▒") == 0){
        //the addresses for the same variables are different in gold/silver.
        mem_write(0xD1DC, 0x00); //wStartDay
        mem_write(0xD1DD, 0x00); //wStartHour
        mem_write(0xD1DE, 0x00); //wStartMinute
        mem_write(0xD1DF, 0x00); //wStartSecond
    }
    
    else return false; //the game is not a recognized pokemon game.
    
    struct tm RTCtime = rg_rtc_getTime(dev);
    //rg_rtc_debug(RTCtime);
    
    if (rg_settings_get_int32(SETTING_RTC_DST, 0) == 1) //if DST mode is toggled add an hour
    {
        if(RTCtime.tm_hour == 23) RTCtime.tm_hour = 0;
        else RTCtime.tm_hour++;
    }
    //With the start time set to zero, direct time setting works as expected.
    rtc.d = RTCtime.tm_wday + 1;
    rtc.h = RTCtime.tm_hour;
    rtc.m = RTCtime.tm_min;
    rtc.s = RTCtime.tm_sec;
    
    return true;
}

void rtc_write(byte b)
{
	MESSAGE_DEBUG("write %02X: %02X (%d)\n", rtc.sel, b, b);

	switch (rtc.sel & 0xf)
	{
	case 0x8: // Seconds
		rtc.regs[0] = b;
		rtc.s = b % 60;
		break;
	case 0x9: // Minutes
		rtc.regs[1] = b;
		rtc.m = b % 60;
		break;
	case 0xA: // Hours
		rtc.regs[2] = b;
		rtc.h = b % 24;
		break;
	case 0xB: // Days (lower 8 bits)
		rtc.regs[3] = b;
		rtc.d = ((rtc.d & 0x100) | b) % 365;
		break;
	case 0xC: // Flags (days upper 1 bit, carry, stop)
		rtc.regs[4] = b;
		rtc.flags = b;
		rtc.d = ((rtc.d & 0xff) | ((b&1)<<9)) % 365;
		break;
	}
}

void rtc_tick()
{
	if ((rtc.flags & 0x40))
		return; // rtc stop

	if (++rtc.ticks >= 60)
	{
		if (++rtc.s >= 60)
		{
			if (++rtc.m >= 60)
			{
				if (++rtc.h >= 24)
				{
					if (++rtc.d >= 365)
					{
						rtc.d = 0;
						rtc.flags |= 0x80;
					}
					rtc.h = 0;
				}
				rtc.m = 0;
			}
			rtc.s = 0;
		}
		rtc.ticks = 0;
	}
}

void rtc_save(FILE *f)
{
	int64_t rt = RT_BASE + (rtc.s + (rtc.m * 60) + (rtc.h * 3600) + (rtc.d * 86400));

	fwrite(&rtc.s, 4, 1, f);
	fwrite(&rtc.m, 4, 1, f);
	fwrite(&rtc.h, 4, 1, f);
	fwrite(&rtc.d, 4, 1, f);
	fwrite(&rtc.flags, 4, 1, f);
	for (int i = 0; i < 5; i++) {
		fwrite(&rtc.regs[i], 4, 1, f);
	}
	fwrite(&rt, 8, 1, f);
}

void rtc_load(FILE *f)
{
	int64_t rt = 0;

	// Try to read old format first
	int tmp = fscanf(f, "%d %*d %d %02d %02d %02d %02d\n%*d\n",
		&rtc.flags, &rtc.d, &rtc.h, &rtc.m, &rtc.s, &rtc.ticks);

	if (tmp >= 5)
		return;

	fread(&rtc.s, 4, 1, f);
	fread(&rtc.m, 4, 1, f);
	fread(&rtc.h, 4, 1, f);
	fread(&rtc.d, 4, 1, f);
	fread(&rtc.flags, 4, 1, f);
	for (int i = 0; i < 5; i++) {
		fread(&rtc.regs[i], 4, 1, f);
	}
	fread(&rt, 8, 1, f);
}
