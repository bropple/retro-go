#include <rg_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "emulators.h"
#include "favorites.h"
#include "gui.h"

static const char *SETTING_SELECTED_TAB  = "SelectedTab";
static const char *SETTING_GUI_THEME     = "ColorTheme";
static const char *SETTING_SHOW_EMPTY    = "ShowEmptyTabs";
static const char *SETTING_SHOW_PREVIEW  = "ShowPreview";
static const char *SETTING_PREVIEW_SPEED = "PreviewSpeed";

static const char *SETTING_RTC_ENABLE    = "RTCenable";
static const char *SETTING_RTC_FORMAT    = "RTCformat";
static const char *SETTING_RTC_MONTH_TXT = "RTCmonthText";
static const char *SETTING_RTC_HOUR_PREF = "RTChourPref";
static const char *SETTING_RTC_DST       = "RTCdst";

struct tm RTCtimeBuf = { 0 }; //time buffer for use in RTC settings
bool dst = false;
static rg_app_desc_t *app; //contains external RTC device descriptor (app->dev)

static dialog_return_t font_type_cb(dialog_option_t *option, dialog_event_t event)
{
    font_info_t info = rg_gui_get_font_info();

    if (event == RG_DIALOG_PREV) {
        rg_gui_set_font_type((int)info.type - 1);
        info = rg_gui_get_font_info();
        gui_redraw();
    }
    if (event == RG_DIALOG_NEXT) {
        if (!rg_gui_set_font_type((int)info.type + 1))
            rg_gui_set_font_type(0);
        info = rg_gui_get_font_info();
        gui_redraw();
    }

    sprintf(option->value, "%s %d", info.font->name, info.height);

    return RG_DIALOG_IGNORE;
}

static dialog_return_t show_empty_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.show_empty = !gui.show_empty;
        rg_settings_set_app_int32(SETTING_SHOW_EMPTY, gui.show_empty);
    }
    strcpy(option->value, gui.show_empty ? "Show" : "Hide");
    return RG_DIALOG_IGNORE;
}

static dialog_return_t startup_app_cb(dialog_option_t *option, dialog_event_t event)
{
    int startup_app = rg_system_get_startup_app();
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        startup_app = startup_app ? 0 : 1;
        rg_system_set_startup_app(startup_app);
    }
    strcpy(option->value, startup_app == 0 ? "Launcher " : "Last used");
    return RG_DIALOG_IGNORE;
}

static dialog_return_t disk_activity_cb(dialog_option_t *option, dialog_event_t event)
{
    int disk_activity = rg_vfs_get_enable_disk_led();
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        disk_activity = !disk_activity;
        rg_vfs_set_enable_disk_led(disk_activity);
    }
    strcpy(option->value, disk_activity ? "On " : "Off");
    return RG_DIALOG_IGNORE;
}

static dialog_return_t show_preview_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV) {
        if (--gui.show_preview < 0) gui.show_preview = PREVIEW_MODE_COUNT - 1;
        rg_settings_set_app_int32(SETTING_SHOW_PREVIEW, gui.show_preview);
    }
    if (event == RG_DIALOG_NEXT) {
        if (++gui.show_preview >= PREVIEW_MODE_COUNT) gui.show_preview = 0;
        rg_settings_set_app_int32(SETTING_SHOW_PREVIEW, gui.show_preview);
    }
    const char *values[] = {"None      ", "Cover,Save", "Save,Cover", "Cover only", "Save only "};
    strcpy(option->value, values[gui.show_preview % PREVIEW_MODE_COUNT]);
    return RG_DIALOG_IGNORE;
}

static dialog_return_t show_preview_speed_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.show_preview_fast = gui.show_preview_fast ? 0 : 1;
        rg_settings_set_app_int32(SETTING_PREVIEW_SPEED, gui.show_preview_fast);
    }
    strcpy(option->value, gui.show_preview_fast ? "Short" : "Long");
    return RG_DIALOG_IGNORE;
}

static dialog_return_t color_shift_cb(dialog_option_t *option, dialog_event_t event)
{
    int max = gui_themes_count - 1;
    if (event == RG_DIALOG_PREV) {
        if (--gui.theme < 0) gui.theme = max;
        rg_settings_set_app_int32(SETTING_GUI_THEME, gui.theme);
        gui_redraw();
    }
    if (event == RG_DIALOG_NEXT) {
        if (++gui.theme > max) gui.theme = 0;
        rg_settings_set_app_int32(SETTING_GUI_THEME, gui.theme);
        gui_redraw();
    }
    sprintf(option->value, "%d/%d", gui.theme + 1, max + 1);
    return RG_DIALOG_IGNORE;
}

static inline bool tab_enabled(tab_t *tab)
{
    int disabled_tabs = 0;

    if (gui.show_empty)
        return true;

    // If all tabs are disabled then we always return true, otherwise it's an endless loop
    for (int i = 0; i < gui.tabcount; ++i)
        if (gui.tabs[i]->initialized && gui.tabs[i]->is_empty)
            disabled_tabs++;

    return (disabled_tabs == gui.tabcount) || (tab->initialized && !tab->is_empty);
}

//The following RTC settings should remain global, use rg_settings_xxx_int32 instead of the app version.
//May want to make time show up in the menus inside every emulator eventually.

static dialog_return_t rtc_enable_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_enable = gui.rtc_enable ? 0 : 1;
        rg_settings_set_int32(SETTING_RTC_ENABLE, gui.rtc_enable);

    }
    strcpy(option->value, gui.rtc_enable ? "On" : "Off");

    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_format_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV) {
        if (--gui.rtc_format < 0) gui.rtc_format = 2;
        rg_settings_set_int32(SETTING_RTC_FORMAT, gui.rtc_format);
    }
    if (event == RG_DIALOG_NEXT) {
        if (++gui.rtc_format > 2) gui.rtc_format = 0;
        rg_settings_set_int32(SETTING_RTC_FORMAT, gui.rtc_format);
    }
    const char *values[] = {"MDY", "DMY", "YMD"};
    strcpy(option->value, values[gui.rtc_format % 3]);
    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_month_text_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_month_text = gui.rtc_month_text ? 0 : 1;
        rg_settings_set_int32(SETTING_RTC_MONTH_TXT, gui.rtc_month_text);
    }
    strcpy(option->value, gui.rtc_month_text ? "On" : "Off");
    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_hour_pref_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_hour_pref = gui.rtc_hour_pref ? 0 : 1;
        rg_settings_set_int32(SETTING_RTC_HOUR_PREF, gui.rtc_hour_pref);
    }
    strcpy(option->value, gui.rtc_hour_pref ? "24h" : "12h");
    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_dst_cb(dialog_option_t *option, dialog_event_t event)
{
    gui.rtc_dst = rg_settings_get_int32(SETTING_RTC_DST, 0);
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_dst = gui.rtc_dst ? 0 : 1;
        rg_settings_set_int32(SETTING_RTC_DST, gui.rtc_dst);
    }
    strcpy(option->value, gui.rtc_dst ? "On" : "Off");
    return RG_DIALOG_ENTER;
}

//TODO: Make HW RTC time update settings work for 12 hour mode as well.

static dialog_return_t rtc_t_set_cb(dialog_option_t *option, dialog_event_t event)
{
    if(option->id == 'Y') {
        //2000 min, 2090 max
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_year < 2000) RTCtimeBuf.tm_year = 2100;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_year > 2100) RTCtimeBuf.tm_year = 2000;
        sprintf(option->value, "%04d", RTCtimeBuf.tm_year+1900);
    }
    if(option->id == 'M') {
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_mon < 0) RTCtimeBuf.tm_mon = 11;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_mon > 11) RTCtimeBuf.tm_mon = 0;
        char * values[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        strcpy(option->value, values[RTCtimeBuf.tm_mon]);
    }
    if(option->id == 'd') {
        uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        uint8_t max_day = daysInMonth[RTCtimeBuf.tm_mon];
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_mday < 1) RTCtimeBuf.tm_mday = max_day;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_mday > max_day) RTCtimeBuf.tm_mday = 1;
        sprintf(option->value, "%02d", RTCtimeBuf.tm_mday);
    }
    if (option->id == 'D') {
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_wday < 0) RTCtimeBuf.tm_wday = 6;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_wday > 6) RTCtimeBuf.tm_wday = 0;
        const char *values[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        strcpy(option->value, values[RTCtimeBuf.tm_wday]);
    }
    if (option->id == 'h') {
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_hour < 0) RTCtimeBuf.tm_hour = 23;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_hour > 23) RTCtimeBuf.tm_hour = 0;
        sprintf(option->value, "%02d", RTCtimeBuf.tm_hour);
    }
    if (option->id == 'm') {
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_min < 0) RTCtimeBuf.tm_min = 59;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_min > 59) RTCtimeBuf.tm_min = 0;
        sprintf(option->value, "%02d", RTCtimeBuf.tm_min);
    }
    if (option->id == 's') {
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_sec < 0) RTCtimeBuf.tm_sec = 59;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_sec > 59) RTCtimeBuf.tm_sec = 0;
        sprintf(option->value, "%02d", RTCtimeBuf.tm_sec);
    }
    if (option->id == 'T') {
        if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
            dst = dst ? 0 : 1;
        }
        strcpy(option->value, dst ? "On" : "Off");
    }
    if (option->id == 'C') {
        if(event == RG_DIALOG_ENTER)
        {
            //use RTCtimeBuf struct to update RTC time
            if(app->dev.enabled)
            {
                if(dst)
                {
                    //if its DST we need to subtract an hour now because the software
                    //normally takes care of this elsewhere.
                    if(RTCtimeBuf.tm_hour == 0) RTCtimeBuf.tm_hour = 23;
                    else RTCtimeBuf.tm_hour--;
                }
                rg_settings_set_int32(SETTING_RTC_DST, dst);
                gui.rtc_dst = dst;
                //ds3231_set_time(&(app->dev), &RTCtimeBuf);
                RTCtimeBuf.tm_year -= 1900;
                struct timeval tv = {mktime(&RTCtimeBuf), 0};
                settimeofday(&tv, NULL);
                return RG_DIALOG_SELECT;
            }
            else rg_gui_alert("DS3231M",  "Unable to update HW RTC time!");
        }
    }

    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_set_time_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        static dialog_option_t choices[] = {
            {'Y', "Year", "0000", 1, &rtc_t_set_cb},
            {'M', "Month", "00", 1, &rtc_t_set_cb},
            {'d', "Day of Month", "00", 1, &rtc_t_set_cb},
            {'D', "Day of Week", "0", 1, &rtc_t_set_cb},
            {'h', "Hour", "00", 1, &rtc_t_set_cb},
            {'m', "Min",  "00", 1, &rtc_t_set_cb},
            {'s', "Sec",  "00", 1, &rtc_t_set_cb},
            {'T', "DST",  "Off", 1, &rtc_t_set_cb},
            {'C', "Save Changes", NULL, 1, &rtc_t_set_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Set RTC Date & Time", choices, 0);
    }
    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_state_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_option_t options[] = {
            {202, "Date Format", "...", gui.rtc_enable, &rtc_format_cb},
            {203, "Month Text", "...", gui.rtc_enable, &rtc_month_text_cb},
            {204, "12h or 24h", "...", gui.rtc_enable, &rtc_hour_pref_cb},
            {205, "DST", "...", gui.rtc_enable, &rtc_dst_cb},
            {206, "Set Date & Time", NULL, gui.rtc_enable, &rtc_set_time_cb},
            RG_DIALOG_CHOICE_LAST
        };

        rg_gui_dialog("HW RTC Time Settings", options, 0);
    }
    return RG_DIALOG_ENTER;
}

static dialog_return_t rtc_master_enable_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_option_t options[] = {
            {200, "Master Enable     ", "...", 1, &rtc_enable_cb},
            {201, "Date/Time Settings", NULL, 1, &rtc_state_cb},
            RG_DIALOG_CHOICE_LAST
        };
        //putting the rest of the settings inside another sub menu so they're greyed out
        //properly when the master enable is OFF

        rg_gui_dialog("DS3231M RTC Settings", options, 0);

    }
    return RG_DIALOG_ENTER;
}

void retro_loop(i2c_dev_t dev)
{
    //the gui variables need to be initialized before the tabs -> somehow causes both theme and tab settings not to load on reboot

    gui.selected            = rg_settings_get_app_int32(SETTING_SELECTED_TAB, 0);
    gui.theme               = rg_settings_get_app_int32(SETTING_GUI_THEME, 0);
    gui.show_empty          = rg_settings_get_app_int32(SETTING_SHOW_EMPTY, 1);
    gui.show_preview        = rg_settings_get_app_int32(SETTING_SHOW_PREVIEW, 1);
    gui.show_preview_fast   = rg_settings_get_app_int32(SETTING_PREVIEW_SPEED, 0);

    //RTC variables
    gui.rtc_enable      = rg_settings_get_int32(SETTING_RTC_ENABLE, 0);
    gui.rtc_format      = rg_settings_get_int32(SETTING_RTC_FORMAT, 0);
    gui.rtc_month_text  = rg_settings_get_int32(SETTING_RTC_MONTH_TXT, 0);
    gui.rtc_hour_pref   = rg_settings_get_int32(SETTING_RTC_HOUR_PREF, 0);
    gui.rtc_dst         = rg_settings_get_int32(SETTING_RTC_DST, 0);
    
    tab_t *tab = gui_get_current_tab();
    int last_key = -1;
    int repeat = 0;
    int selected_tab_last = -1;

    //if the RTC is toggled off while installed, the system will not need to restart if it is toggled on again.
    int last_rtc_enable = gui.rtc_enable;

    if (!gui.show_empty)
    {
        // If we're hiding empty tabs then we must preload all files
        // to avoid flicker and delays when skipping empty tabs...
        for (int i = 0; i < gui.tabcount; i++)
        {
            gui_init_tab(gui.tabs[i]);
        }
    }

    rg_display_clear(C_BLACK);

    while (true)
    {
        if (gui.selected != selected_tab_last)
        {
            int direction = (gui.selected - selected_tab_last) < 0 ? -1 : 1;

            gui_event(TAB_LEAVE, tab);

            tab = gui_set_current_tab(gui.selected);

            if (!tab->initialized)
            {
                gui_redraw();
                gui_init_tab(tab);

                if (tab_enabled(tab))
                {
                    gui_draw_status(tab);
                    gui_draw_list(tab);
                }
            }
            else if (tab_enabled(tab))
            {
                gui_redraw();
            }

            if (!tab_enabled(tab))
            {
                gui.selected += direction;
                continue;
            }

            gui_event(TAB_ENTER, tab);

            selected_tab_last = gui.selected;
        }

        gui.joystick = rg_input_read_gamepad();

        if (gui.idle_counter > 0 && (gui.joystick & GAMEPAD_KEY_ANY) == 0)
        {
            gui_event(TAB_IDLE, tab);

            if (gui.idle_counter % 100 == 0)
                gui_draw_status(tab);
        }

        if ((gui.joystick & last_key) && repeat > 0)
        {
            last_key |= (1 << 24); // No repeat
            if (--repeat == 0)
            {
                last_key &= GAMEPAD_KEY_ANY;
                repeat = 4;
            }
        }
        else
        {
            last_key = gui.joystick & GAMEPAD_KEY_ANY;
            repeat = 25;
        }

        if (last_key == GAMEPAD_KEY_MENU) {
            char buildstr[32], datestr[32];

            const dialog_option_t options[] = {
                {0, "Ver.", buildstr, 1, NULL},
                {0, "Date", datestr, 1, NULL},
                {0, "By", "ducalex", 1, NULL},
                RG_DIALOG_SEPARATOR,
                {1, "Reboot to firmware", NULL, 1, NULL},
                {2, "Reset settings", NULL, 1, NULL},
                {3, "Clear cache", NULL, 1, NULL},
                {0, "Close", NULL, 1, NULL},
                RG_DIALOG_CHOICE_LAST
            };

            const rg_app_desc_t *app = rg_system_get_app();
            sprintf(buildstr, "%.30s", app->version);
            sprintf(datestr, "%s %.5s", app->buildDate, app->buildTime);

            char *rel_hash = strstr(buildstr, "-0-g");
            if (rel_hash)
            {
                rel_hash[0] = ' ';
                rel_hash[1] = ' ';
                rel_hash[2] = ' ';
                rel_hash[3] = '(';
                strcat(buildstr, ")");
            }

            int sel = rg_gui_dialog("Retro-Go", options, -1);
            if (sel == 1) {
                rg_system_switch_app(RG_APP_FACTORY);
            }
            else if (sel == 2) {
                if (rg_gui_confirm("Reset all settings?", NULL, false)) {
                    rg_settings_reset();
                    rg_system_restart();
                }
            }
            else if (sel == 3) {
                rg_vfs_delete(CRC_CACHE_PATH);
                rg_system_restart();
            }
            gui_redraw();
        }
        else if (last_key == GAMEPAD_KEY_VOLUME) {
            const dialog_option_t options[] = {
                RG_DIALOG_SEPARATOR,
                {0, "Color theme", "...", 1, &color_shift_cb},
                {0, "Font type  ", "...", 1, &font_type_cb},
                {0, "Empty tabs ", "...", 1, &show_empty_cb},
                {0, "Preview    ", "...", 1, &show_preview_cb},
                {0, "    - Delay", "...", 1, &show_preview_speed_cb},
                {0, "Startup app", "...", 1, &startup_app_cb},
                {0, "Disk LED   ", "...", 1, &disk_activity_cb},
                {0, "HW RTC Settings", NULL, 1, &rtc_master_enable_cb},
                RG_DIALOG_CHOICE_LAST
            };
            rg_gui_settings_menu(options);
            gui_redraw();
        }
        else if (last_key == GAMEPAD_KEY_SELECT) {
            gui.selected--;
        }
        else if (last_key == GAMEPAD_KEY_START) {
            gui.selected++;
        }
        else if (last_key == GAMEPAD_KEY_UP) {
            gui_scroll_list(tab, SCROLL_LINE_UP, 0);
        }
        else if (last_key == GAMEPAD_KEY_DOWN) {
            gui_scroll_list(tab, SCROLL_LINE_DOWN, 0);
        }
        else if (last_key == GAMEPAD_KEY_LEFT) {
            gui_scroll_list(tab, SCROLL_PAGE_UP, 0);
        }
        else if (last_key == GAMEPAD_KEY_RIGHT) {
            gui_scroll_list(tab, SCROLL_PAGE_DOWN, 0);
        }
        else if (last_key == GAMEPAD_KEY_A) {
            gui_event(KEY_PRESS_A, tab);
        }
        else if (last_key == GAMEPAD_KEY_B) {
            gui_event(KEY_PRESS_B, tab);
        }

        if (gui.joystick & GAMEPAD_KEY_ANY) {
            gui.idle_counter = 0;
        } else {
            gui.idle_counter++;
        }
        
        if((last_rtc_enable != gui.rtc_enable) && last_rtc_enable == 0)
        {
            //initialize the RTC if it isn't already -> requires reboot.
            rg_gui_alert("DS3231M",  "Restarting to initialize RTC.");
            rg_system_restart();
        }

        //Draw the time in the main menu, only if RTC is enabled in settings AND the i2c device hasn't errored
        if((rg_settings_get_int32(SETTING_RTC_ENABLE, gui.rtc_enable) == 1) && dev.errored == false)
        {
            time_t now = time(NULL);
            RTCtimeBuf = *(localtime(&now));
            rg_gui_draw_time(RTCtimeBuf, 58, 0, gui.rtc_format, gui.rtc_month_text, gui.rtc_hour_pref);
        }

        usleep(15 * 1000UL);
    }
}

void app_main(void)
{
    app = rg_system_init(32000, NULL);

    emulators_init();
    favorites_init();

    retro_loop(app->dev);
}
