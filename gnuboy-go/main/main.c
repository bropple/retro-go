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

#define APP_ID 20

#define AUDIO_SAMPLE_RATE   (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 16 + 1)

#define NVS_KEY_SAVE_SRAM "sram"

static short audioBuffer[AUDIO_BUFFER_LENGTH * 2];

static rg_video_frame_t frames[2];
static rg_video_frame_t *currentUpdate = &frames[0];

static rg_app_desc_t *app;

static bool fullFrame = false;
static long skipFrames = 0;

static const char *sramFile;
static bool saveSRAM = false;
static long saveSRAM_Timer = 0;

#ifdef ENABLE_NETPLAY
static bool netplay = false;
#endif
// --- MAIN

static void netplay_callback(netplay_event_t event, void *arg)
{
#ifdef ENABLE_NETPLAY
    bool new_netplay;

    switch (event)
    {
    case NETPLAY_EVENT_STATUS_CHANGED:
        new_netplay = (rg_netplay_status() == NETPLAY_STATUS_CONNECTED);

        if (netplay && !new_netplay)
        {
            rg_gui_alert("Netplay", "Connection lost!");
        }
        netplay = new_netplay;
        break;

    default:
        break;
    }
#endif
}

static bool save_state(char *pathName)
{
    // For convenience we also write the sram to its own file
    // So that it can be imported in other emulators
    sram_save(sramFile);

    if (state_save(pathName) == 0)
    {
        char *filename = rg_emu_get_path(EMU_PATH_SCREENSHOT, 0);
        if (filename)
        {
            rg_display_save_frame(filename, currentUpdate, 0, 0);
            rg_free(filename);
        }
        return true;
    }

    return false;
}

static bool load_state(char *pathName)
{
    if (state_load(pathName) != 0)
    {
        emu_reset(true);

        if (saveSRAM) sram_load(sramFile);

        return false;
    }

    // TO DO: Call rtc_sync() if a physical RTC is present
    return true;
}

static bool reset_emulation(bool hard)
{
    emu_reset(hard);
    return true;
}

static bool palette_update_cb(dialog_choice_t *option, dialog_event_t event)
{
    int pal = pal_get_dmg();
    int max = pal_count_dmg();

    if (event == RG_DIALOG_PREV) {
        pal = pal > 0 ? pal - 1 : max;
    }

    if (event == RG_DIALOG_NEXT) {
        pal = pal < max ? pal + 1 : 0;
    }

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        rg_settings_Palette_set(pal);
        pal_set_dmg(pal);
        emu_run(true);
    }

    if (pal == 0) strcpy(option->value, "GBC");
    else sprintf(option->value, "%d/%d", pal, max);

    return event == RG_DIALOG_ENTER;
}

static bool save_sram_update_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        saveSRAM = !saveSRAM;
        rg_settings_app_int32_set(NVS_KEY_SAVE_SRAM, saveSRAM);
    }

    strcpy(option->value, saveSRAM ? "Yes" : "No");

    return event == RG_DIALOG_ENTER;
}

static bool rtc_t_update_cb(dialog_choice_t *option, dialog_event_t event)
{
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

    // TO DO: Update system clock

    return event == RG_DIALOG_ENTER;
}

static bool rtc_update_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        static dialog_choice_t choices[] = {
            {'d', "Day", "000", 1, &rtc_t_update_cb},
            {'h', "Hour", "00", 1, &rtc_t_update_cb},
            {'m', "Min",  "00", 1, &rtc_t_update_cb},
            {'s', "Sec",  "00", 1, &rtc_t_update_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Set Clock", choices, 0);
    }
    sprintf(option->value, "%02d:%02d", rtc.h, rtc.m);
    return false;
}

static bool advanced_settings_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_choice_t options[] = {
            {101, "Set clock", "00:00", 1, &rtc_update_cb},
            {102, "Auto save SRAM", "No", 1, &save_sram_update_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Advanced", options, 0);
    }
    return false;
}

static inline void screen_blit(void)
{
    rg_video_frame_t *previousUpdate = &frames[currentUpdate == &frames[0]];

    fullFrame = rg_display_queue_update(currentUpdate, previousUpdate) == RG_SCREEN_UPDATE_FULL;

    // swap buffers
    currentUpdate = previousUpdate;
    fb.ptr = currentUpdate->buffer;
}

void DS3231_InjectRTC(i2c_dev_t dev){
    
    //this function 'hijacks' the RTC of the emulator once to overwrite the 
    //DS3231's time values over the GB's. The emulator keeps it ticking on
    //its own while it runs.
    
    struct tm rtcinfo = rg_rtc_getTime(dev);
    
    rtc.d = dayOfYear(rtcinfo.tm_year, rtcinfo.tm_mon + 1, rtcinfo.tm_mday);
    rtc.h = rtcinfo.tm_hour;
    rtc.m = rtcinfo.tm_min;
    rtc.s = rtcinfo.tm_sec;
    
    char message[36] = { 0 };
    sprintf(message, "%03d %02d %02d %02d", rtc.d, rtc.h, rtc.m, rtc.s);
    rg_display_clear(C_BLUE);
    rg_gui_alert("GB RTC Values",  message);
    
}

void app_main(void)
{
    i2c_dev_t dev = rg_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    rg_emu_init(&LoadState, &SaveState, &netplay_callback);
    
    rg_emu_proc_t handlers = {
        .loadState = &load_state,
        .saveState = &save_state,
        .reset = &reset_emulation,
        .netplay = &netplay_callback,
    };

    rg_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    rg_emu_init(handlers);

    app = rg_system_get_app();

    frames[0].flags = RG_PIXEL_565|RG_PIXEL_BE;
    frames[0].width = GB_WIDTH;
    frames[0].height = GB_HEIGHT;
    frames[0].stride = GB_WIDTH * 2;
    frames[1] = frames[0];

    frames[0].buffer = rg_alloc(GB_WIDTH * GB_HEIGHT * 2, MEM_ANY);
    frames[1].buffer = rg_alloc(GB_WIDTH * GB_HEIGHT * 2, MEM_ANY);

    saveSRAM = rg_settings_app_int32_get(NVS_KEY_SAVE_SRAM, 0);
    sramFile = rg_emu_get_path(EMU_PATH_SAVE_SRAM, 0);

    // Load ROM
    rom_load(app->romPath);

    // Set palette for non-gbc games (must be after rom_load)
    pal_set_dmg(rg_settings_Palette_get());

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

    emu_init();

    if (app->startAction == EMU_START_ACTION_RESUME)
    {
        rg_emu_load_state(0);
    }
    else if (saveSRAM)
    {
        sram_load(sramFile);
    }
    
    if(rg_settings_int32_get("RTCenable", 0) > 0)
    //if the RTC is enabled
    {
        DS3231_InjectRTC(dev); //replace gnuboy's RTC values with the DS3231's
    }
    while (true)
    {
        gamepad_state_t joystick = rg_input_read_gamepad();

        if (joystick.values[GAMEPAD_KEY_MENU]) {
            rg_gui_game_menu();
        }
        else if (joystick.values[GAMEPAD_KEY_VOLUME]) {
            dialog_choice_t options[] = {
                {100, "Palette", "7/7", !hw.cgb, &palette_update_cb},
                {101, "More...", "", 1, &advanced_settings_cb},
                RG_DIALOG_CHOICE_LAST
            };
            rg_gui_game_settings_menu(options);
        }

        int64_t startTime = get_elapsed_time();
        bool drawFrame = !skipFrames;

        pad_set(PAD_UP, joystick.values[GAMEPAD_KEY_UP]);
        pad_set(PAD_RIGHT, joystick.values[GAMEPAD_KEY_RIGHT]);
        pad_set(PAD_DOWN, joystick.values[GAMEPAD_KEY_DOWN]);
        pad_set(PAD_LEFT, joystick.values[GAMEPAD_KEY_LEFT]);
        pad_set(PAD_SELECT, joystick.values[GAMEPAD_KEY_SELECT]);
        pad_set(PAD_START, joystick.values[GAMEPAD_KEY_START]);
        pad_set(PAD_A, joystick.values[GAMEPAD_KEY_A]);
        pad_set(PAD_B, joystick.values[GAMEPAD_KEY_B]);

        emu_run(drawFrame);

        if (saveSRAM)
        {
            if (ram.sram_dirty)
            {
                saveSRAM_Timer = 120; // wait 2 seconds
                ram.sram_dirty = 0;
            }

            if (saveSRAM_Timer > 0 && --saveSRAM_Timer == 0)
            {
                // TO DO: Try compressing the sram file, it might reduce stuttering
                sram_save(sramFile);
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

        // Tick before submitting audio/syncing
        rg_system_tick(!drawFrame, fullFrame, elapsed);

        if (!app->speedupEnabled)
        {
            rg_audio_submit(pcm.buf, pcm.pos >> 1);
        }
    }
}
