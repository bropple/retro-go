#include <rg_system.h>
#include <sys/time.h>
#include <string.h>

#include "../components/gnuboy/loader.h"
#include "../components/gnuboy/hw.h"
#include "../components/gnuboy/lcd.h"
#include "../components/gnuboy/cpu.h"
#include "../components/gnuboy/mem.h"
#include "../components/gnuboy/sound.h"
#include "../components/gnuboy/regs.h"
#include "../components/gnuboy/rtc.h"
#include "../components/gnuboy/emu.h"

#define AUDIO_SAMPLE_RATE   (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 16 + 1)

static short audioBuffer[AUDIO_BUFFER_LENGTH * 2];

static rg_video_frame_t frames[2];
static rg_video_frame_t *currentUpdate = &frames[0];

static rg_app_desc_t *app;

static bool fullFrame = false;
static long skipFrames = 20; // The 20 is to hide startup flicker in some games

static const char *sramFile;
static long autoSaveSRAM = 0;
static long autoSaveSRAM_Timer = 0;

#ifdef ENABLE_NETPLAY
static bool netplay = false;
#endif

static const char *SETTING_SAVESRAM         = "SaveSRAM";
static const char *SETTING_PALETTE          = "Palette";
static const char *SETTING_RTC_ENABLE       = "RTCenable"; //System enable of DS3231M hardware RTC
static const char *SETTING_RTC_DST          = "RTCdst";
static const char *SETTING_RTC_GB_ENABLE    = "RTCgbEnable"; //Gameboy specific enable of hardware RTC, handy if you want to change time in-game briefly.
static const char *SETTING_RTC_GB_AUTOSYNC  = "RTCgbAutosync"; //Automatic time synchronization during gameplay.

bool RTCgbEnable = false;
bool RTCautoSync = false;
// --- MAIN

bool rtc_gameTimeUpdate()
{    
    /*  The purpose of this function is to overwrite the time value of 
     *  Pokemon Crystal, Pokemon Silver Pokemon Gold, and Pokemon Prism (and maybe other games) with
     *  the DS3231's own time value in order to synchronize time more like a real cartridge.
     * 
     *  This should also work with ROM hacks of a game, as long as the ROM name
     *  in the header is the same.
     * 
     *  Return values:
     *         true: If the game is recognized and the time has successfully been synced.
     *         false: If the game is NOT recognized or there was a hardware problem with time retrieval.
     */
    
    // G/S/C default time value: SUNDAY 12:00:00 AM -> Day 0 00:00:00;
    // Prism default time value: SUNDAY JAN 1 2000 12:00:00 AM
    
    //we will first set the start time in the game to zero so the default time value is in effect, and is known.
    
    if((rg_settings_get_int32(SETTING_RTC_ENABLE, 0) == 1) && (rg_settings_get_app_int32(SETTING_RTC_GB_ENABLE, 0) == 1))
    {
        time_t now = time(NULL);
        struct tm time = *(localtime(&now));
        
        if (rg_settings_get_int32(SETTING_RTC_DST, 0) == 1) //if DST mode is toggled add an hour
        {
            //if(time->tm_hour == 23) time->tm_hour = 0;
            //else time->tm_hour++;
            time = rg_rtc_handleDST(time);
        }
        
        if(strncmp(rom.name, "PM_CRYSTAL", 10) == 0)
        { //if the game is Pokemon Crystal or closely related ROM hack
            mem_write(0xD4B6, 0x00); //wStartDay
            mem_write(0xD4B7, 0x00); //wStartHour
            mem_write(0xD4B8, 0x00); //wStartMinute
            mem_write(0xD4B9, 0x00); //wStartSecond
            
            //crystal, gold, and silver don't have very advanced timekeeping,
            //so getting the day of the week is enough
            rtc.d = time.tm_wday--;
        }
        else if(strncmp(rom.name, "POKEMON_SLVAAXE", 15) == 0 || strncmp(rom.name, "POKEMON_GLDAAUE", 15) == 0)
        { //the addresses for the same variables are different in gold/silver.
            mem_write(0xD1DC, 0x00); //wStartDay
            mem_write(0xD1DD, 0x00); //wStartHour
            mem_write(0xD1DE, 0x00); //wStartMinute
            mem_write(0xD1DF, 0x00); //wStartSecond
            
            rtc.d = time.tm_wday--;
        }
        else if(strncmp(rom.name, "PM_PRISM", 8) == 0)
        {   //Prism is very different.
            mem_write(0xDFE8, 0x00); //wRTCbaseDay
            mem_write(0xDFE9, 0x00); //wRTCbaseHours
            mem_write(0xDFEA, 0x00); //wRTCbaseMinutes
            mem_write(0xDFEB, 0x00); //wRTCbaseSeconds
            mem_write(0xDFEC, time.tm_year-100); //wRTCbaseYear, because default year is 2000 we can just do this to get the right year.
            mem_write(0xDFED, 0x00); //wRTCbaseMonth
            
            //prism keeps track of month, day, and year.
            
            rtc.d = time.tm_yday;
        }
        
        else return false; //the game is not a recognized RTC game.
        
        //With the start time set to zero, direct time setting works as expected.
        //prism is more complicated than this... or not!
        
        rtc.h = time.tm_hour;
        rtc.m = time.tm_min;
        rtc.s = time.tm_sec;
        
        RG_LOGI("Game time sync complete.\n");
        return true;
    }
    else
    {
        rtc_sync(); //Still sync rtc with system time.
        return false; //settings not enabled.
    }
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_display_save_frame(filename, currentUpdate, width, height);
}

static bool save_state_handler(const char *filename)
{
    return state_save(filename) == 0;
}

static bool load_state_handler(const char *filename)
{
    if (state_load(filename) != 0)
    {
        // If a state fails to load then we should behave as we do on boot
        // which is a hard reset and load sram if present
        emu_reset(true);
        sram_load(sramFile);

        return false;
    }

    skipFrames = 0;
    autoSaveSRAM_Timer = 0;
    rtc_gameTimeUpdate();
    return true;
}

static bool reset_handler(bool hard)
{
    emu_reset(hard);

    fullFrame = false;
    skipFrames = 20;
    autoSaveSRAM_Timer = 0;
    
    rtc_gameTimeUpdate();

    return true;
}

static dialog_return_t palette_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int pal = pal_get_dmg();
    int max = pal_count_dmg();

    if (event == RG_DIALOG_PREV)
        pal = pal > 0 ? pal - 1 : max;

    if (event == RG_DIALOG_NEXT)
        pal = pal < max ? pal + 1 : 0;

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        rg_settings_set_app_int32(SETTING_PALETTE, pal);
        pal_set_dmg(pal);
        emu_run(true);
    }

    if (pal == 0) strcpy(option->value, "GBC");
    else sprintf(option->value, "%d/%d", pal, max);

    return RG_DIALOG_IGNORE;
}

static dialog_return_t sram_save_now_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER)
    {
        rg_system_set_led(1);

        if (sram_save(sramFile) != 0)
        {
            rg_gui_alert("Save failed!", sramFile);
        }

        rg_system_set_led(0);

        return RG_DIALOG_SELECT;
    }

    return RG_DIALOG_IGNORE;
}

static dialog_return_t sram_autosave_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV) autoSaveSRAM--;
    if (event == RG_DIALOG_NEXT) autoSaveSRAM++;

    autoSaveSRAM = RG_MIN(RG_MAX(0, autoSaveSRAM), 999);

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        rg_settings_set_app_int32(SETTING_SAVESRAM, autoSaveSRAM);
    }

    if (autoSaveSRAM == 0) strcpy(option->value, "Off ");
    else sprintf(option->value, "%lds", autoSaveSRAM);

    return RG_DIALOG_IGNORE;
}

static dialog_return_t rtc_t_update_cb(dialog_option_t *option, dialog_event_t event)
{
    //This should only change gnuboy's emulated RTC, not the external!
    if (option->id == 'd') {
        if (event == RG_DIALOG_PREV && --rtc.d < 0) rtc.d = 364;
        if (event == RG_DIALOG_NEXT && ++rtc.d > 364) rtc.d = 0;
        sprintf(option->value, "%03d", rtc.d);
    }
    if (option->id == 'h') {
        if (event == RG_DIALOG_PREV && --rtc.h < 0) rtc.h = 23;
        if (event == RG_DIALOG_NEXT && ++rtc.h > 23) rtc.h = 0;
        sprintf(option->value, "%02d", rtc.h);
    }
    if (option->id == 'm') {
        if (event == RG_DIALOG_PREV && --rtc.m < 0) rtc.m = 59;
        if (event == RG_DIALOG_NEXT && ++rtc.m > 59) rtc.m = 0;
        sprintf(option->value, "%02d", rtc.m);
    }
    if (option->id == 's') {
        if (event == RG_DIALOG_PREV && --rtc.s < 0) rtc.s = 59;
        if (event == RG_DIALOG_NEXT && ++rtc.s > 59) rtc.s = 0;
        sprintf(option->value, "%02d", rtc.s);
    }

    return RG_DIALOG_IGNORE;
}

static dialog_return_t rtc_update_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_option_t choices[] = {
            {'d', "Day", "000", 1, &rtc_t_update_cb},
            {'h', "Hour", "00", 1, &rtc_t_update_cb},
            {'m', "Min",  "00", 1, &rtc_t_update_cb},
            {'s', "Sec",  "00", 1, &rtc_t_update_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Set Gnuboy Clock", choices, 0);
    }
    sprintf(option->value, "%02d:%02d", rtc.h, rtc.m);
    return RG_DIALOG_IGNORE;
}

static dialog_return_t rtc_HWsyncNow_cb(dialog_option_t *option, dialog_event_t event)
{
    if(event == RG_DIALOG_ENTER)
    {
        rtc_gameTimeUpdate(); //this will re-synchronize gnuboy rtc with system clock if it is changed by user
        return RG_DIALOG_SELECT;
    }
    return RG_DIALOG_IGNORE;
}

static dialog_return_t rtc_gb_enable_cb(dialog_option_t *option, dialog_event_t event) //toggles the use of the RTC in actual emulation
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        RTCgbEnable = RTCgbEnable ? false : true;
        rg_settings_set_app_int32(SETTING_RTC_GB_ENABLE, RTCgbEnable);
    }
    strcpy(option->value, RTCgbEnable ? "Yes" : "No");
    return RG_DIALOG_IGNORE;
}

static dialog_return_t rtc_autosync_cb(dialog_option_t *option, dialog_event_t event) //toggles the use of the RTC in actual emulation
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        RTCautoSync = RTCautoSync ? false : true;
        rg_settings_set_app_int32(SETTING_RTC_GB_AUTOSYNC, RTCautoSync);
    }
    strcpy(option->value, RTCautoSync ? "On" : "Off");
    return RG_DIALOG_IGNORE;
}

static dialog_return_t advanced_settings_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_option_t options[] = {
            {101, "Use HW RTC", "No", rg_settings_get_int32(SETTING_RTC_ENABLE, 0), &rtc_gb_enable_cb},
            {102, "Auto HW RTC Sync", "Off", (rg_settings_get_int32(SETTING_RTC_ENABLE, 0) && rg_settings_get_app_int32(SETTING_RTC_GB_ENABLE, 0)), &rtc_autosync_cb},
            {103, "Sync HW RTC Now", NULL, (rg_settings_get_int32(SETTING_RTC_ENABLE, 0) && rg_settings_get_app_int32(SETTING_RTC_GB_ENABLE, 0)), &rtc_HWsyncNow_cb},
            {103, "Set Gnuboy Clock", "--:--", (!(rg_settings_get_app_int32(SETTING_RTC_GB_ENABLE, 0))), &rtc_update_cb}, //cannot change when RTC is in sync mode, turn off to change anytime
            RG_DIALOG_SEPARATOR,
            {111, "Autosave SRAM", "Off", mbc.batt && mbc.ramsize, &sram_autosave_cb},
            {112, "Save SRAM Now ", NULL, mbc.batt && mbc.ramsize, &sram_save_now_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Advanced", options, 0);
    }
    return RG_DIALOG_IGNORE;
}

static void screen_blit(void)
{
    rg_video_frame_t *previousUpdate = &frames[currentUpdate == &frames[0]];

    fullFrame = rg_display_queue_update(currentUpdate, previousUpdate) == RG_UPDATE_FULL;

    // swap buffers
    currentUpdate = previousUpdate;
    fb.ptr = currentUpdate->buffer;
}

static void auto_sram_update(void)
{
    if (autoSaveSRAM > 0 && ram.sram_dirty)
    {
        rg_system_set_led(1);
        sram_update(sramFile);
        if (ram.sram_dirty)
        {
            MESSAGE_ERROR("sram still dirty after sram_update(), trying full save...\n");
            sram_save(sramFile);
        }
        rg_system_set_led(0);
    }
}

void app_main(void)
{
    rg_emu_proc_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .netplay = NULL,
        .screenshot = &screenshot_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers);
    
    RTCgbEnable = rg_settings_get_app_int32(SETTING_RTC_GB_ENABLE, 0); //boolean value governs in-game usage of HW RTC
    RTCautoSync = rg_settings_get_app_int32(SETTING_RTC_GB_AUTOSYNC, 0); //governs in-game RTC autosync for supported games.

    frames[0].flags = RG_PIXEL_565|RG_PIXEL_BE;
    frames[0].width = GB_WIDTH;
    frames[0].height = GB_HEIGHT;
    frames[0].stride = GB_WIDTH * 2;
    frames[1] = frames[0];

    frames[0].buffer = rg_alloc(GB_WIDTH * GB_HEIGHT * 2, MEM_ANY);
    frames[1].buffer = rg_alloc(GB_WIDTH * GB_HEIGHT * 2, MEM_ANY);

    autoSaveSRAM = rg_settings_get_app_int32(SETTING_SAVESRAM, 0);
    sramFile = rg_emu_get_path(RG_PATH_SAVE_SRAM, 0);

    // Load ROM
    rom_load(app->romPath);

    // Set palette for non-gbc games (must be after rom_load)
    pal_set_dmg(rg_settings_get_app_int32(SETTING_PALETTE, 0));

    // Video
    memset(&fb, 0, sizeof(fb));
    fb.w = currentUpdate->width;
    fb.h = currentUpdate->height;
    fb.pitch = currentUpdate->stride;
    fb.ptr = currentUpdate->buffer;
    fb.format = GB_PIXEL_565_BE;
    fb.enabled = 1;
    fb.blit_func = &screen_blit;

    // Audio
    memset(&pcm, 0, sizeof(pcm));
    pcm.hz = AUDIO_SAMPLE_RATE;
    pcm.stereo = 1;
    pcm.len = AUDIO_BUFFER_LENGTH * 2; // count of 16bit samples (x2 for stereo)
    pcm.buf = (n16 *)&audioBuffer;
    pcm.pos = 0;

    emu_init(app->dev);

    if (app->startAction == RG_START_ACTION_RESUME)
    {
        rg_emu_load_state(0);
    }
    else
    {
        sram_load(sramFile);
    }
    
    unsigned long frameCounter = 0;
    rtc_gameTimeUpdate();

    while (true)
    {
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & GAMEPAD_KEY_MENU) {
            auto_sram_update();
            rg_gui_game_menu();
        }
        else if (joystick & GAMEPAD_KEY_VOLUME) {
            dialog_option_t options[] = {
                {100, "Palette", "7/7", !hw.cgb, &palette_update_cb},
                {101, "More...", NULL, 1, &advanced_settings_cb},
                RG_DIALOG_CHOICE_LAST
            };
            auto_sram_update();
            rg_gui_game_settings_menu(options);
        }

        int64_t startTime = get_elapsed_time();
        bool drawFrame = !skipFrames;

        pad_set(PAD_UP, joystick & GAMEPAD_KEY_UP);
        pad_set(PAD_RIGHT, joystick & GAMEPAD_KEY_RIGHT);
        pad_set(PAD_DOWN, joystick & GAMEPAD_KEY_DOWN);
        pad_set(PAD_LEFT, joystick & GAMEPAD_KEY_LEFT);
        pad_set(PAD_SELECT, joystick & GAMEPAD_KEY_SELECT);
        pad_set(PAD_START, joystick & GAMEPAD_KEY_START);
        pad_set(PAD_A, joystick & GAMEPAD_KEY_A);
        pad_set(PAD_B, joystick & GAMEPAD_KEY_B);

        emu_run(drawFrame);

        if (autoSaveSRAM > 0)
        {
            if (ram.sram_dirty && autoSaveSRAM_Timer == 0)
            {
                autoSaveSRAM_Timer = autoSaveSRAM * 60;
            }

            if (autoSaveSRAM_Timer > 0 && --autoSaveSRAM_Timer == 0)
            {
                auto_sram_update();
                skipFrames += 5;
            }
        }

        long elapsed = get_elapsed_time_since(startTime);

        if (skipFrames == 0)
        {
            if (app->speedupEnabled)
                skipFrames = app->speedupEnabled * 2;
            else if (elapsed >= get_frame_time(60)) // Frame took too long
                skipFrames = 1;
            else if (drawFrame && fullFrame) // This could be avoided when scaling != full
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
        
        if(RTCautoSync && RTCgbEnable)
        {
            //sync the time every so often, based on frames
            if((frameCounter % 1800) == 0) 
            {
                if(rtc_gameTimeUpdate()) RG_LOGI("RTC synced automatically.\n");
            }
        }
        
        frameCounter++;

        // Tick before submitting audio/syncing
        rg_system_tick(!drawFrame, fullFrame, elapsed);

        if (!app->speedupEnabled)
        {
            rg_audio_submit(pcm.buf, pcm.pos >> 1);
        }
    }
}
