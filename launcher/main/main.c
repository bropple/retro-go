#include <rg_system.h>
#include <esp_ota_ops.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emulators.h"
#include "favorites.h"
#include "gui.h"

#define KEY_SELECTED_TAB  "SelectedTab"
#define KEY_GUI_THEME     "ColorTheme"
#define KEY_SHOW_EMPTY    "ShowEmptyTabs"
#define KEY_SHOW_PREVIEW  "ShowPreview"
#define KEY_PREVIEW_SPEED "PreviewSpeed"
#define KEY_RTC_ENABLE    "RTCenable"
#define KEY_RTC_FORMAT    "RTCformat"
#define KEY_RTC_MONTH_TXT "RTCmonthText"
#define KEY_RTC_HOUR_PREF "RTChourPref"

#define USE_CONFIG_FILE

struct tm RTCtimeBuf = { 0 }; //time buffer for use in RTC settings

static bool font_size_cb(dialog_choice_t *option, dialog_event_t event)
{
    int font_size = rg_gui_get_font_info().points;
    if (event == RG_DIALOG_PREV && font_size > 8) {
        rg_gui_set_font_size(font_size -= 4);
        gui_redraw();
    }
    if (event == RG_DIALOG_NEXT && font_size < 16) {
        rg_gui_set_font_size(font_size += 4);
        gui_redraw();
    }
    sprintf(option->value, "%d", font_size);
    if (font_size ==  8) strcpy(option->value, "Small ");
    if (font_size == 12) strcpy(option->value, "Medium");
    if (font_size == 16) strcpy(option->value, "Large ");
    return event == RG_DIALOG_ENTER;
}

static bool show_empty_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.show_empty = !gui.show_empty;
        rg_settings_int32_set(KEY_SHOW_EMPTY, gui.show_empty);
    }
    strcpy(option->value, gui.show_empty ? "Show" : "Hide");
    return event == RG_DIALOG_ENTER;
}

static bool startup_app_cb(dialog_choice_t *option, dialog_event_t event)
{
    int startup_app = rg_settings_StartupApp_get();
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        startup_app = startup_app ? 0 : 1;
        rg_settings_StartupApp_set(startup_app);
    }
    strcpy(option->value, startup_app == 0 ? "Launcher " : "Last used");
    return event == RG_DIALOG_ENTER;
}

static bool show_preview_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV) {
        if (--gui.show_preview < 0) gui.show_preview = 4;
        rg_settings_int32_set(KEY_SHOW_PREVIEW, gui.show_preview);
    }
    if (event == RG_DIALOG_NEXT) {
        if (++gui.show_preview > 4) gui.show_preview = 0;
        rg_settings_int32_set(KEY_SHOW_PREVIEW, gui.show_preview);
    }
    const char *values[] = {"None      ", "Cover,Save", "Save,Cover", "Cover     ", "Save      "};
    strcpy(option->value, values[gui.show_preview % 5]);
    return event == RG_DIALOG_ENTER;
}

static bool show_preview_speed_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.show_preview_fast = gui.show_preview_fast ? 0 : 1;
        rg_settings_int32_set(KEY_PREVIEW_SPEED, gui.show_preview_fast);
    }
    strcpy(option->value, gui.show_preview_fast ? "Short" : "Long");
    return event == RG_DIALOG_ENTER;
}

static bool color_shift_cb(dialog_choice_t *option, dialog_event_t event)
{
    int max = gui_themes_count - 1;
    if (event == RG_DIALOG_PREV) {
        if (--gui.theme < 0) gui.theme = max;
        rg_settings_int32_set(KEY_GUI_THEME, gui.theme);
        gui_redraw();
    }
    if (event == RG_DIALOG_NEXT) {
        if (++gui.theme > max) gui.theme = 0;
        rg_settings_int32_set(KEY_GUI_THEME, gui.theme);
        gui_redraw();
    }
    sprintf(option->value, "%d/%d", gui.theme + 1, max + 1);
    return event == RG_DIALOG_ENTER;
}

static bool rtc_enable_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_enable = gui.rtc_enable ? 0 : 1;
        rg_settings_int32_set(KEY_RTC_ENABLE, gui.rtc_enable);
        
    }
    strcpy(option->value, gui.rtc_enable ? "On" : "Off");
    
    return event == RG_DIALOG_ENTER;
}

static bool rtc_format_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV) {
        if (--gui.rtc_format < 0) gui.rtc_format = 2;
        rg_settings_int32_set(KEY_RTC_FORMAT, gui.rtc_format);
    }
    if (event == RG_DIALOG_NEXT) {
        if (++gui.rtc_format > 2) gui.rtc_format = 0;
        rg_settings_int32_set(KEY_RTC_FORMAT, gui.rtc_format);
    }
    const char *values[] = {"MDY", "DMY", "YMD"};
    strcpy(option->value, values[gui.rtc_format % 3]);
    return event == RG_DIALOG_ENTER;
}

static bool rtc_month_text_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_month_text = gui.rtc_month_text ? 0 : 1;
        rg_settings_int32_set(KEY_RTC_MONTH_TXT, gui.rtc_month_text);
    }
    strcpy(option->value, gui.rtc_month_text ? "On" : "Off");
    return event == RG_DIALOG_ENTER;
}

static bool rtc_hour_pref_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        gui.rtc_hour_pref = gui.rtc_hour_pref ? 0 : 1;
        rg_settings_int32_set(KEY_RTC_HOUR_PREF, gui.rtc_hour_pref);
    }
    strcpy(option->value, gui.rtc_hour_pref ? "24h" : "12h");
    return event == RG_DIALOG_ENTER;
}

static bool rtc_t_set_cb(dialog_choice_t *option, dialog_event_t event)
{
    if(option->id == 'Y') {
        //2000 min, 2090 max
        if (event == RG_DIALOG_PREV && --RTCtimeBuf.tm_year < 2000) RTCtimeBuf.tm_year = 2090;
        if (event == RG_DIALOG_NEXT && ++RTCtimeBuf.tm_year > 2090) RTCtimeBuf.tm_year = 2000;
        sprintf(option->value, "%04d", RTCtimeBuf.tm_year);
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
    if (option->id == 'C') {
        //use RTCtimeBuf struct to update RTC time
    }

    return event == RG_DIALOG_ENTER;
}

static bool rtc_set_time_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        static dialog_choice_t choices[] = {
            {'Y', "Year", "0000", 1, &rtc_t_set_cb},
            {'M', "Month", "00", 1, &rtc_t_set_cb},
            {'d', "Day of Month", "00", 1, &rtc_t_set_cb},
            {'D', "Day of Week", "0", 1, &rtc_t_set_cb},
            {'h', "Hour", "00", 1, &rtc_t_set_cb},
            {'m', "Min",  "00", 1, &rtc_t_set_cb},
            {'s', "Sec",  "00", 1, &rtc_t_set_cb},
            {'C', "Commit Changes", "", 1, &rtc_t_set_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Set RTC Date & Time", choices, 0);
    }
    return event == RG_DIALOG_ENTER;
}

static bool rtc_state_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_choice_t options[] = {
            {202, "Date Format    ", "...", gui.rtc_enable, &rtc_format_cb},
            {203, "Month Text     ", "...", gui.rtc_enable, &rtc_month_text_cb},
            {204, "12h or 24h     ", "...", gui.rtc_enable, &rtc_hour_pref_cb},
            {205, "Set Date & Time", "", gui.rtc_enable, &rtc_set_time_cb},
            RG_DIALOG_CHOICE_LAST
        };
        
        rg_gui_dialog("DS3231M RTC Date/Time Settings", options, 0);
    }
    return false;
}

static bool rtc_master_enable_cb(dialog_choice_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER) {
        dialog_choice_t options[] = {
            {200, "Master Enable     ", "...", 1, &rtc_enable_cb},
            {201, "Date/Time Settings", "", 1, &rtc_state_cb},
            RG_DIALOG_CHOICE_LAST
        };
        //putting the rest of the settings inside another sub menu so they're greyed out
        //properly when the master enable is OFF
        
        rg_gui_dialog("DS3231M RTC Settings", options, 0);
        
    }
    return false;
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

void retro_loop(i2c_dev_t dev)
{
    tab_t *tab = gui_get_current_tab();
    int debounce = 0;
    int last_key = -1;
    int selected_tab_last = -1;

    gui.selected     = rg_settings_int32_get(KEY_SELECTED_TAB, 0);
    gui.theme        = rg_settings_int32_get(KEY_GUI_THEME, 0);
    gui.show_empty   = rg_settings_int32_get(KEY_SHOW_EMPTY, 1);
    gui.show_preview = rg_settings_int32_get(KEY_SHOW_PREVIEW, 1);
    gui.show_preview_fast = rg_settings_int32_get(KEY_PREVIEW_SPEED, 0);
    
    //RTC variables
    gui.rtc_enable = rg_settings_int32_get(KEY_RTC_ENABLE, 0);
    gui.rtc_format = rg_settings_int32_get(KEY_RTC_FORMAT, 0);
    gui.rtc_month_text = rg_settings_int32_get(KEY_RTC_MONTH_TXT, 0);
    gui.rtc_hour_pref = rg_settings_int32_get(KEY_RTC_HOUR_PREF, 0);
    
    int last_rtc_enable = gui.rtc_enable;
    
    while (true)
    {
        if (gui.selected != selected_tab_last)
        {
            int direction = (gui.selected - selected_tab_last) < 0 ? -1 : 1;

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

            selected_tab_last = gui.selected;
        }

        gui.joystick = rg_input_read_gamepad();

        if (gui.idle_counter > 0 && gui.joystick.bitmask == 0)
        {
            gui_event(TAB_IDLE, tab);

            if (gui.idle_counter % 100 == 0)
                gui_draw_status(tab);
        }

        if (last_key >= 0) {
            if (!gui.joystick.values[last_key]) {
                last_key = -1;
                debounce = 0;
            } else if (debounce++ > 12) {
                debounce = 12;
                last_key = -1;
            }
        } else {
            for (int i = 0; i < GAMEPAD_KEY_MAX; i++)
                if (gui.joystick.values[i]) last_key = i;

            if (last_key == GAMEPAD_KEY_MENU) {
                dialog_choice_t choices[] = {
                    {0, "Ver.", "build string", 1, NULL},
                    {0, "Date", "", 1, NULL},
                    {0, "By", "ducalex", 1, NULL},
                    RG_DIALOG_SEPARATOR,
                    {1, "Reboot to firmware", "", 1, NULL},
                    {2, "Reset settings", "", 1, NULL},
                    {0, "Close", "", 1, NULL},
                    RG_DIALOG_CHOICE_LAST
                };

                const esp_app_desc_t *app = esp_ota_get_app_description();
                sprintf(choices[0].value, "%.30s", app->version);
                sprintf(choices[1].value, "%s %.5s", app->date, app->time);

                if (strstr(app->version, "-0-") == strrchr(app->version, '-') - 2)
                    sprintf(strstr(choices[0].value, "-0-") , " (%s)", strrchr(app->version, '-') + 1);

                int sel = rg_gui_dialog("Retro-Go", choices, -1);
                if (sel == 1) {
                    rg_system_switch_app(RG_APP_FACTORY);
                }
                else if (sel == 2) {
                    if (rg_gui_confirm("Reset all settings?", NULL, false)) {
                        rg_settings_reset();
                        rg_system_restart();
                    }
                }
                gui_redraw();
            }
            else if (last_key == GAMEPAD_KEY_VOLUME) {
                dialog_choice_t choices[] = {
                    RG_DIALOG_SEPARATOR,
                    {0, "Color theme", "...",  1, &color_shift_cb},
                    {0, "Font size  ", "...",  1, &font_size_cb},
                    {0, "Empty tabs ", "...",  1, &show_empty_cb},
                    {0, "Preview    ", "...",  1, &show_preview_cb},
                    {0, "    - Delay", "...",  1, &show_preview_speed_cb},
                    {0, "Startup app", "...",  1, &startup_app_cb},
                    {0, "DS3231M RTC Settings", "",     1, &rtc_master_enable_cb},
                    RG_DIALOG_CHOICE_LAST
                };
                rg_gui_settings_menu(choices);
                gui_redraw();
            }
            else if (last_key == GAMEPAD_KEY_SELECT) {
                debounce = -10;
                gui.selected--;
            }
            else if (last_key == GAMEPAD_KEY_START) {
                debounce = -10;
                gui.selected++;
            }
            else if (last_key == GAMEPAD_KEY_UP) {
                gui_scroll_list(tab, LINE_UP);
            }
            else if (last_key == GAMEPAD_KEY_DOWN) {
                gui_scroll_list(tab, LINE_DOWN);
            }
            else if (last_key == GAMEPAD_KEY_LEFT) {
                gui_scroll_list(tab, PAGE_UP);
            }
            else if (last_key == GAMEPAD_KEY_RIGHT) {
                gui_scroll_list(tab, PAGE_DOWN);
            }
            else if (last_key == GAMEPAD_KEY_A) {
                gui_event(KEY_PRESS_A, tab);
            }
            else if (last_key == GAMEPAD_KEY_B) {
                gui_event(KEY_PRESS_B, tab);
            }
        }

        if (gui.joystick.bitmask) {
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
        
        //Draw the time in the main menu, only if RTC is enabled
        if(rg_settings_int32_get(KEY_RTC_ENABLE, gui.rtc_enable) > 0)
        {
            rg_gui_draw_time(rg_rtc_getTime(dev), 58, 0, gui.rtc_format, gui.rtc_month_text, gui.rtc_hour_pref);
            RTCtimeBuf = rg_rtc_getTime(dev);
            
        }
        
        usleep(15 * 1000UL);
    }
}

void app_main(void)
{
    i2c_dev_t dev = rg_system_init(0, 32000);
    rg_display_clear(0);

    emulators_init();
    favorites_init();
    
    retro_loop(dev);
}
