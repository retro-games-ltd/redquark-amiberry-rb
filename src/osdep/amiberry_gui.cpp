#include <cstdio>
#include <strings.h>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

#include <guisan.hpp>
#include <guisan/sdl.hpp>

#include "sysdeps.h"
#include "uae.h"
#include "options.h"
#include "keybuf.h"
#include "zfile.h"
#include "gui.h"
#include "osdep/gui/SelectorEntry.hpp"
#include "gui/gui_handling.h"
#include "rommgr.h"
#include "custom.h"
#include "inputdevice.h"
#include "audio.h"
#include "sounddep/sound.h"
#include "savestate.h"
#include "blkdev.h"
#include "memory.h"
#include "amiberry_gfx.h"
#include "amiberry.h"

#ifdef AMIBERRY
#include <linux/kd.h>
#include <sys/ioctl.h>
#endif

int emulating = 0;
bool config_loaded = false;
int gui_active;

struct gui_msg
{
	int num;
	const char* msg;
};

struct gui_msg gui_msglist[] = {
  { NUMSG_NEEDEXT2,       "The software uses a non-standard floppy disk format. You may need to use a custom floppy disk image file instead of a standard one. This message will not appear again." },
  { NUMSG_NOROM,          "Could not load system ROM, trying system ROM replacement." },
  { NUMSG_NOROMKEY,       "Could not find system ROM key file." },
  { NUMSG_KSROMCRCERROR,  "System ROM checksum incorrect. The system ROM image file may be corrupt." },
  { NUMSG_KSROMREADERROR, "Error while reading system ROM." },
  { NUMSG_NOEXTROM,       "No extended ROM found." },
  { NUMSG_KS68EC020,      "The selected system ROM requires a 68EC020 or later CPU." },
  { NUMSG_KS68020,        "The selected system ROM requires a 68020 or later CPU." },
  { NUMSG_KS68030,        "The selected system ROM requires a 68030 CPU." },
  { NUMSG_STATEHD,        "WARNING: Current configuration is not fully compatible with state saves." },
  { NUMSG_KICKREP,        "You need to have a floppy disk (image file) in DF0: to use the system ROM replacement." },
  { NUMSG_KICKREPNO,      "The floppy disk (image file) in DF0: is not compatible with the system ROM replacement functionality." },
  { NUMSG_ROMNEED,        "One of the following system ROMs is required:\n\n%s\n\nCheck the System ROM path in the Paths panel and click Rescan ROMs." },
  { NUMSG_EXPROMNEED,     "One of the following expansion boot ROMs is required:\n\n%s\n\nCheck the System ROM path in the Paths panel and click Rescan ROMs." },
  { NUMSG_NOMEMORY,       "Out of memory or too much Z3 autoconfig space configured." },
  { NUMSG_NOCAPS,         "capsimg.so not found. CAPS/IPF support not available." },
  { NUMSG_OLDCAPS,        "Old version of capsimg.so found." },

  { -1, "" }
};

std::vector<ConfigFileInfo*> ConfigFilesList;
std::vector<AvailableROM*> lstAvailableROMs;
std::vector<std::string> lstMRUDiskList;
std::vector<std::string> lstMRUCDList;
std::vector<std::string> lstMRUWhdloadList;

void AddFileToDiskList(const char *file, int moveToTop)
{
	unsigned int i;

	for (i = 0; i < lstMRUDiskList.size(); ++i)
	{
		if (!stricmp(lstMRUDiskList[i].c_str(), file))
		{
			if (moveToTop)
			{
				lstMRUDiskList.erase(lstMRUDiskList.begin() + i);
				lstMRUDiskList.insert(lstMRUDiskList.begin(), file);
			}
			break;
		}
	}
	if (i >= lstMRUDiskList.size())
		lstMRUDiskList.insert(lstMRUDiskList.begin(), file);

	while (lstMRUDiskList.size() > MAX_MRU_DISKLIST)
		lstMRUDiskList.pop_back();
}

void AddFileToCDList(const char *file, int moveToTop)
{
	unsigned int i;

	for (i = 0; i < lstMRUCDList.size(); ++i)
	{
		if (!stricmp(lstMRUCDList[i].c_str(), file))
		{
			if (moveToTop)
			{
				lstMRUCDList.erase(lstMRUCDList.begin() + i);
				lstMRUCDList.insert(lstMRUCDList.begin(), file);
			}
			break;
		}
	}
	if (i >= lstMRUCDList.size())
		lstMRUCDList.insert(lstMRUCDList.begin(), file);

	while (lstMRUCDList.size() > MAX_MRU_CDLIST)
		lstMRUCDList.pop_back();
}

void AddFileToWHDLoadList(const char* file, int moveToTop)
{
	unsigned int i;

	for (i = 0; i < lstMRUWhdloadList.size(); ++i)
	{
		if (!stricmp(lstMRUWhdloadList[i].c_str(), file))
		{
			if (moveToTop)
			{
				lstMRUWhdloadList.erase(lstMRUWhdloadList.begin() + i);
				lstMRUWhdloadList.insert(lstMRUWhdloadList.begin(), file);
			}
			break;
		}
	}
	if (i >= lstMRUWhdloadList.size())
		lstMRUWhdloadList.insert(lstMRUWhdloadList.begin(), file);

	while (lstMRUWhdloadList.size() > MAX_MRU_WHDLOADLIST)
		lstMRUWhdloadList.pop_back();
}

void ClearAvailableROMList()
{
	while (!lstAvailableROMs.empty())
	{
		auto* const tmp = lstAvailableROMs[0];
		lstAvailableROMs.erase(lstAvailableROMs.begin());
		delete tmp;
	}
}

static void addrom(struct romdata* rd, const char* path)
{
	char tmpName[MAX_DPATH];
	auto* const tmp = new AvailableROM();
	getromname(rd, tmpName);
	strncpy(tmp->Name, tmpName, MAX_DPATH - 1);
	if (path != nullptr)
		strncpy(tmp->Path, path, MAX_DPATH - 1);
	tmp->ROMType = rd->type;
	lstAvailableROMs.emplace_back(tmp);
	romlist_add(path, rd);
}

struct romscandata
{
	uae_u8* keybuf;
	int keysize;
};

static struct romdata* scan_single_rom_2(struct zfile* f)
{
	uae_u8 buffer[20] = {0};
	auto cl = 0;
	struct romdata* rd = nullptr;

	zfile_fseek(f, 0, SEEK_END);
	int size = zfile_ftell(f);
	zfile_fseek(f, 0, SEEK_SET);
	if (size > 524288 * 2) /* don't skip KICK disks or 1M ROMs */
		return nullptr;
	zfile_fread(buffer, 1, 11, f);
	if (!memcmp(buffer, "KICK", 4))
	{
		zfile_fseek(f, 512, SEEK_SET);
		if (size > 262144)
			size = 262144;
	}
	else if (!memcmp(buffer, "AMIROMTYPE1", 11))
	{
		cl = 1;
		size -= 11;
	}
	else
	{
		zfile_fseek(f, 0, SEEK_SET);
	}
	auto* rombuf = xcalloc(uae_u8, size);
	if (!rombuf)
		return nullptr;
	zfile_fread(rombuf, 1, size, f);
	if (cl > 0)
	{
		decode_cloanto_rom_do(rombuf, size, size);
		cl = 0;
	}
	if (!cl)
	{
		rd = getromdatabydata(rombuf, size);
		if (!rd && (size & 65535) == 0)
		{
			for (auto i = 0; i < size; i += 2)
			{
				uae_u8 b = rombuf[i];
				rombuf[i] = rombuf[i + 1];
				rombuf[i + 1] = b;
			}
			rd = getromdatabydata(rombuf, size);
		}
	}
	free(rombuf);
	return rd;
}

static int isromext(char* path)
{
	if (!path)
		return 0;
	auto* ext = strrchr(path, '.');
	if (!ext)
		return 0;
	ext++;

	if (!stricmp(ext, "rom") || !stricmp(ext, "adf") || !stricmp(ext, "key")
		|| !stricmp(ext, "a500") || !stricmp( ext, "a600") || !stricmp(ext, "a1200") || !stricmp(ext, "a4000"))
		return 1;
	for (auto i = 0; uae_archive_extensions[i]; i++)
	{
		if (!stricmp(ext, uae_archive_extensions[i]))
			return 1;
	}
	return 0;
}

static int scan_rom_2(struct zfile* f, void* dummy)
{
	auto* const path = zfile_getname(f);

	if (!isromext(path))
		return 0;
	auto* const rd = scan_single_rom_2(f);
	if (rd)
		addrom(rd, path);
	return 0;
}

static void scan_rom(char *path)
{
	if (!isromext(path)) {
		//write_log("ROMSCAN: skipping file '%s', unknown extension\n", path);
		return;
	}
	zfile_zopen(path, scan_rom_2, 0);
}

void SymlinkROMs()
{
	symlink_roms(&changed_prefs);
}

#if defined REDQUARK
// Copy ROMs from the internal kicstarts directory to the external (SDCard/USBStick) ROMs directory.
// This ensures there is a directory on the USB stick with valid ROMs and allows the user to override them.
// 
static void PopulateExternalRoms()
{
	vector<string> files;
	char master_path[MAX_DPATH];
	char real_path[MAX_DPATH];

	snprintf(master_path, MAX_DPATH, "%s/kickstarts/", start_path_data);
	get_rom_path(real_path, MAX_DPATH);

    // If paths are the same, there is nothing to copy!
    if( strcmp( master_path, real_path ) == 0 )
        return;

	char master_file[MAX_DPATH];
	char target_file[MAX_DPATH];

	read_directory(master_path, nullptr, &files);
	for (auto & file : files)
	{
		strncpy(master_file, master_path, MAX_DPATH - 1);
		strncat(master_file, file.c_str(), MAX_DPATH - 1);

		strncpy(target_file, real_path, MAX_DPATH - 1);
		strncat(target_file, file.c_str(), MAX_DPATH - 1);

        copyfile( target_file, master_file, 0 ); // 0 == do not replace if file exists
	}
}
#endif

void RescanROMs()
{
	vector<string> files;
	char path[MAX_DPATH];

#if defined REDQUARK
    // If an external "ROMs/WHDBoot" device is in use (usb stick), make sure roms directory
    // contains some valid roms.
    PopulateExternalRoms();
#endif

	romlist_clear();

	ClearAvailableROMList();
	get_rom_path(path, MAX_DPATH);

	load_keyring(&changed_prefs, path);
	read_directory(path, nullptr, &files);
	for (auto & file : files)
	{
		char tmppath[MAX_DPATH];
		strncpy(tmppath, path, MAX_DPATH - 1);
		strncat(tmppath, file.c_str(), MAX_DPATH - 1);
		scan_rom(tmppath);
	}

	auto id = 1;
	for (;;) {
		auto* rd = getromdatabyid(id);
		if (!rd)
			break;
		if (rd->crc32 == 0xffffffff && strncmp(rd->model, "AROS", 4) == 0)
			addrom(rd, ":AROS");
		if (rd->crc32 == 0xffffffff && rd->id == 63) {
			addrom(rd, ":HRTMon");
		}
		id++;
	}
}

static void ClearConfigFileList()
{
	while (!ConfigFilesList.empty())
	{
		auto* const tmp = ConfigFilesList[0];
		ConfigFilesList.erase(ConfigFilesList.begin());
		delete tmp;
	}
}

void ReadConfigFileList(void)
{
	char path[MAX_DPATH];
	std::vector<std::string> files;
	const char *filter_rp9[] = { ".rp9", "\0" };
	const char *filter_uae[] = { ".uae", "\0" };

	ClearConfigFileList();

	// Read rp9 files
	get_rp9_path(path, MAX_DPATH);
	read_directory(path, nullptr, &files);
	FilterFiles(&files, filter_rp9);
	for (auto & file : files)
	{
		auto* tmp = new ConfigFileInfo();
		strncpy(tmp->FullPath, path, MAX_DPATH - 1);
		strncat(tmp->FullPath, file.c_str(), MAX_DPATH - 1);
		strncpy(tmp->Name, file.c_str(), MAX_DPATH - 1);
		remove_file_extension(tmp->Name);
		strncpy(tmp->Description, _T("rp9"), MAX_DPATH - 1);
		ConfigFilesList.emplace_back(tmp);
	}

	// Read standard config files
	get_configuration_path(path, MAX_DPATH);
	read_directory(path, nullptr, &files);
	FilterFiles(&files, filter_uae);
	for (auto & file : files)
	{
		auto* tmp = new ConfigFileInfo();
		strncpy(tmp->FullPath, path, MAX_DPATH - 1);
		strncat(tmp->FullPath, file.c_str(), MAX_DPATH - 1);
		strncpy(tmp->Name, file.c_str(), MAX_DPATH - 1);
		remove_file_extension(tmp->Name);
		// If the user has many (thousands) of configs, this will take a long time
		if (amiberry_options.read_config_descriptions)
		{
			auto p = cfgfile_open(tmp->FullPath, NULL);
			if (p) {
				cfgfile_get_description(p, NULL, tmp->Description, NULL, NULL, NULL, NULL, NULL);
				cfgfile_close(p);
			}
		}
		ConfigFilesList.emplace_back(tmp);
	}
}

ConfigFileInfo* SearchConfigInList(const char* name)
{
	for (auto & i : ConfigFilesList)
	{
		if (!SDL_strncasecmp(i->Name, name, MAX_DPATH))
			return i;
	}
	return nullptr;
}

static void clearallkeys (void)
{
	inputdevice_updateconfig (NULL, &changed_prefs);
}

static void prefs_to_gui()
{
	/* filesys hack */
	changed_prefs.mountitems = currprefs.mountitems;
	memcpy(&changed_prefs.mountconfig, &currprefs.mountconfig, MOUNT_CONFIG_SIZE * sizeof(struct uaedev_config_info));
	update_win_fs_mode(&currprefs);
}

static void gui_to_prefs(void)
{
	if (quit_program == -UAE_RESET_HARD) {
		memory_hardreset(2);
	}
	/* filesys hack */
	currprefs.mountitems = changed_prefs.mountitems;
	memcpy(&currprefs.mountconfig, &changed_prefs.mountconfig, MOUNT_CONFIG_SIZE * sizeof(struct uaedev_config_info));
	fixup_prefs(&changed_prefs, true);
	update_win_fs_mode(&changed_prefs);
}

static void after_leave_gui()
{
	inputdevice_copyconfig(&changed_prefs, &currprefs);
	inputdevice_config_change_test();
}

int gui_init()
{
	emulating = 0;
	auto ret = 0;

	if (lstAvailableROMs.empty())
		RescanROMs();

	prefs_to_gui();
	run_gui();
	gui_to_prefs();
	if (quit_program < 0)
		quit_program = -quit_program;
	if (quit_program == UAE_QUIT)
		ret = -2; // Quit without start of emulator

	inputdevice_acquire (TRUE);

	after_leave_gui();
	emulating = 1;
	return ret;
}

void gui_exit()
{
	sync();
	close_sound();
	save_amiberry_settings();
	ClearConfigFileList();
	ClearAvailableROMList();
}

void gui_purge_events()
{
	keybuf_init();
}

int gui_update()
{
	char tmp[MAX_DPATH];

	get_savestate_path(savestate_fname, MAX_DPATH - 1);
	get_screenshot_path(screenshot_filename, MAX_DPATH - 1);

	if (strlen(currprefs.floppyslots[0].df) > 0)
		extract_filename(currprefs.floppyslots[0].df, tmp);
	else
		strncpy(tmp, last_loaded_config, MAX_DPATH - 1);

	strncat(savestate_fname, tmp, MAX_DPATH - 1);
	strncat(screenshot_filename, tmp, MAX_DPATH - 1);
	remove_file_extension(savestate_fname);
	remove_file_extension(screenshot_filename);

  switch(currentStateNum)
  {
    case 1:
  		strncat(savestate_fname,"-1.uss", MAX_DPATH - 1);
	    strncat(screenshot_filename,"-1.png", MAX_DPATH - 1);
	    break;
    case 2:
  		strncat(savestate_fname,"-2.uss", MAX_DPATH - 1);
  		strncat(screenshot_filename,"-2.png", MAX_DPATH - 1);
  		break;
    case 3:
  		strncat(savestate_fname,"-3.uss", MAX_DPATH - 1);
  		strncat(screenshot_filename,"-3.png", MAX_DPATH - 1);
  		break;
	case 4:
		strncat(savestate_fname, "-4.uss", MAX_DPATH - 1);
		strncat(screenshot_filename, "-4.png", MAX_DPATH - 1);
		break;
	case 5:
		strncat(savestate_fname, "-5.uss", MAX_DPATH - 1);
		strncat(screenshot_filename, "-5.png", MAX_DPATH - 1);
		break;
	case 6:
		strncat(savestate_fname, "-6.uss", MAX_DPATH - 1);
		strncat(screenshot_filename, "-6.png", MAX_DPATH - 1);
		break;
	case 7:
		strncat(savestate_fname, "-7.uss", MAX_DPATH - 1);
		strncat(screenshot_filename, "-7.png", MAX_DPATH - 1);
		break;
	case 8:
		strncat(savestate_fname, "-8.uss", MAX_DPATH - 1);
		strncat(screenshot_filename, "-8.png", MAX_DPATH - 1);
		break;
	case 9:
		strncat(savestate_fname, "-9.uss", MAX_DPATH - 1);
		strncat(screenshot_filename, "-9.png", MAX_DPATH - 1);
		break;
    default: 
	   	strncat(savestate_fname,".uss", MAX_DPATH - 1);
  		strncat(screenshot_filename,".png", MAX_DPATH - 1);
  }
  return 0;
}

void gui_display(int shortcut)
{
	if (quit_program != 0)
		return;
	gui_active++;
	emulating = 1;
	pause_emulation = 1;
	pause_sound();
	blkdev_entergui();

	if (lstAvailableROMs.empty())
		RescanROMs();

	graphics_subshutdown();

	prefs_to_gui();
	run_gui();
	gui_to_prefs();

	black_screen_now();

	gui_update ();
	gui_purge_events();
	update_display(&changed_prefs);
	
	reset_sound();
	after_leave_gui();
	clearallkeys ();
	blkdev_exitgui();
	resume_sound();

	inputdevice_acquire (TRUE);
	setmouseactive(1);
	
	fpscounter_reset();
	pause_emulation = 0;
	gui_active--;
}

static void gui_flicker_led2(int led, int unitnum, int status)
{
	static int resetcounter[LED_MAX];
	uae_s8 old;
	uae_s8 *p;

	if (led == LED_HD)
		p = &gui_data.hd;
	else if (led == LED_CD)
		p = &gui_data.cd;
	else if (led == LED_MD)
		p = &gui_data.md;
	else if (led == LED_NET)
		p = &gui_data.net;
	else
		return;
	old = *p;
	if (status < 0) {
		if (old < 0) {
			gui_led(led, -1, -1);
		}
		else {
			gui_led(led, 0, -1);
		}
		return;
	}
	if (status == 0 && old < 0) {
		*p = 0;
		resetcounter[led] = 0;
		gui_led(led, 0, -1);
		return;
	}
	if (status == 0) {
		resetcounter[led]--;
		if (resetcounter[led] > 0)
			return;
	}
#ifdef RETROPLATFORM
	if (unitnum >= 0) {
		if (led == LED_HD) {
			rp_hd_activity(unitnum, status ? 1 : 0, status == 2 ? 1 : 0);
		}
		else if (led == LED_CD) {
			rp_cd_activity(unitnum, status);
		}
	}
#endif
	*p = status;
	resetcounter[led] = 4;
	if (old != *p)
		gui_led(led, *p, -1);
}

void gui_flicker_led(int led, int unitnum, int status)
{
	if (led < 0) {
		gui_flicker_led2(LED_HD, 0, 0);
		gui_flicker_led2(LED_CD, 0, 0);
		if (gui_data.net >= 0)
			gui_flicker_led2(LED_NET, 0, 0);
		if (gui_data.md >= 0)
			gui_flicker_led2(LED_MD, 0, 0);
	}
	else {
		gui_flicker_led2(led, unitnum, status);
	}
}

void gui_fps(int fps, int idle, int color)
{
	gui_data.fps = fps;
	gui_data.idle = idle;
	gui_data.fps_color = color;
	gui_led(LED_FPS, 0, -1);
	gui_led(LED_CPU, 0, -1);
	gui_led(LED_SND, (gui_data.sndbuf_status > 1 || gui_data.sndbuf_status < 0) ? 0 : 1, -1);
}

#ifdef REDQUARK

#define LED_PWR_MASK ( 1<<LED_POWER )
#define LED_FDD_MASK ( 1<<LED_DF0 | 1<<LED_DF1 | 1<<LED_DF2 | 1<<LED_DF3 | 1<<LED_HD | 1<<LED_CD )

static int fdd_gpio_fd = -1;
static int pwr_gpio_fd = -1;
static int temp_fd     = -1;
static int want_temp   = -1;
static unsigned int led_gpio_state = 0;

void gui_led_open_gpio()
{
    led_gpio_state = 0;

    if( fdd_gpio_fd < 0 && currprefs.disk_led_gpio[0] ) {
        fdd_gpio_fd = open( currprefs.disk_led_gpio, O_WRONLY );

        if( fdd_gpio_fd >= 0 )
            write( fdd_gpio_fd, led_gpio_state & LED_FDD_MASK ? "1" : "0", 1 ); // Set initial state
        else
            write_log("FDD GPIO: Could not open gpio %s for writing\n", currprefs.disk_led_gpio );
    }

    if( pwr_gpio_fd < 0 && currprefs.power_led_gpio[0] ) {
        pwr_gpio_fd = open( currprefs.power_led_gpio, O_WRONLY );

        if( pwr_gpio_fd >= 0 )
            write( pwr_gpio_fd, led_gpio_state & LED_PWR_MASK ? "1" : "0", 1 ); // Set initial state
        else
            write_log("PWR GPIO: Could not open gpio %s for writing\n", currprefs.power_led_gpio );
    }

#ifdef PLATFORM_SUN50IW6
#  define TEMPERATURE "/sys/class/thermal/thermal_zone0/temp"
#else
#  define TEMPERATURE "/sys/class/rtc/rtc0/max_user_freq" // Fake!
#endif
    if( want_temp < 0 ) {
        char *ep = getenv("MONITOR_TEMP");
        want_temp = ep != NULL ? atoi(ep) : 0;
    }
    if( (want_temp > 0) && temp_fd < 0 ) { 
        temp_fd = open( TEMPERATURE, O_RDONLY );

        if( temp_fd < 0 )
            write_log("TEMPERATURE: Could not open %s for reading\n", TEMPERATURE );
    }

    return;
}
#endif

void gui_led(int led, int on, int brightness)
{
	unsigned char kbd_led_status;

	// Check current prefs/ update if changed
	if (currprefs.kbd_led_num != changed_prefs.kbd_led_num) currprefs.kbd_led_num = changed_prefs.kbd_led_num;
	if (currprefs.kbd_led_scr != changed_prefs.kbd_led_scr) currprefs.kbd_led_scr = changed_prefs.kbd_led_scr;
	//if (currprefs.kbd_led_cap != changed_prefs.kbd_led_cap) currprefs.kbd_led_cap = changed_prefs.kbd_led_cap;

	ioctl(0, KDGETLED, &kbd_led_status);

	// Handle floppy led status
	if (led >= LED_DF0 && led <= LED_DF3)
	{
		if (currprefs.kbd_led_num == led)
		{
			if (on) kbd_led_status |= LED_NUM;
			else kbd_led_status &= ~LED_NUM;
		}
		if (currprefs.kbd_led_scr == led)
		{
			if (on) kbd_led_status |= LED_SCR;
			else kbd_led_status &= ~LED_SCR;
		}
	}

	// Handle power, hd/cd led status
	if (led == LED_POWER || led == LED_HD || led == LED_CD)
	{
		if (currprefs.kbd_led_num == led)
		{
			if (on) kbd_led_status |= LED_NUM;
			else kbd_led_status &= ~LED_NUM;
		}
		if (currprefs.kbd_led_scr == led)
		{
			if (on) kbd_led_status |= LED_SCR;
			else kbd_led_status &= ~LED_SCR;
		}
	}

	ioctl(0, KDSETLED, kbd_led_status);

#ifdef REDQUARK
    if( fdd_gpio_fd >= 0 || pwr_gpio_fd >= 0 ) {
        unsigned int o_pwr = led_gpio_state & LED_PWR_MASK;
        unsigned int o_fdd = led_gpio_state & LED_FDD_MASK;

	    if ( (led >= LED_DF0 && led <= LED_DF3) || led == LED_POWER || led == LED_HD || led == LED_CD )
        {
            if (on) led_gpio_state |=   1<<led;
            else    led_gpio_state &= ~(1<<led);
        }

        unsigned int state = led_gpio_state & LED_PWR_MASK;
        if( state != o_pwr && pwr_gpio_fd >= 0 )
            write( pwr_gpio_fd, state ? "1" : "0", 1 );

        state = led_gpio_state & LED_FDD_MASK;
        if( state != o_fdd && fdd_gpio_fd >= 0 )
            write( fdd_gpio_fd, state ? "1" : "0", 1 );
    }

    static unsigned int temp_count = 0;
    if( temp_fd >= 0 && (++temp_count % 25) == 0 ) {
        static char tb[10] = {0};
        lseek( temp_fd, 0, SEEK_SET );
        int l = read( temp_fd, tb, sizeof(tb) - 1 );
        if( l > 0 ) {
            tb[l] = '\0';
            gui_data.temperature = atoi(tb);
        }
    }
#endif
}

void gui_filename(int num, const char* name)
{
}

void gui_message(const char* format, ...)
{
	char msg[2048];
	va_list parms;

	va_start(parms, format);
	vsprintf(msg, format, parms);
	va_end(parms);

#if !defined REDQUARK
	if (!uae_gui)
#endif
	{
		// GUI screen is not initialized, output message to the console instead
		printf("%s\n", msg);
		return;
	}
	graphics_subshutdown();
	InGameMessage(msg);
}

void notify_user(int msg)
{
	auto i = 0;
	while (gui_msglist[i].num >= 0)
	{
		if (gui_msglist[i].num == msg)
		{
			gui_message(gui_msglist[i].msg);
			break;
		}
		++i;
	}
}

void notify_user_parms(int msg, const TCHAR *parms, ...)
{
	TCHAR msgtxt[MAX_DPATH];
	TCHAR tmp[MAX_DPATH];
	auto c = 0;
	va_list parms2;

	auto i = 0;
	while (gui_msglist[i].num >= 0)
	{
		if (gui_msglist[i].num == msg)
		{
			strncpy(tmp, gui_msglist[i].msg, MAX_DPATH);
			va_start(parms2, parms);
			_vsntprintf(msgtxt, sizeof msgtxt / sizeof(TCHAR), tmp, parms2);
			gui_message(msgtxt);
			va_end(parms2);
			break;
		}
		++i;
	}
}

int translate_message(int msg, TCHAR* out)
{
	auto i = 0;
	while (gui_msglist[i].num >= 0)
	{
		if (gui_msglist[i].num == msg)
		{
			strncpy(out, gui_msglist[i].msg, MAX_DPATH);
			return 1;
		}
		++i;
	}
	return 0;
}


void FilterFiles(vector<string>* files, const char* filter[])
{
	for (auto q = 0; q < files->size(); q++)
	{
		auto tmp = (*files)[q];

		auto remove = true;
		for (auto f = 0; filter[f] != nullptr && strlen(filter[f]) > 0; ++f)
		{
			if (tmp.size() >= strlen(filter[f]))
			{
				if (!stricmp(tmp.substr(tmp.size() - strlen(filter[f])).c_str(), filter[f]))
				{
					remove = false;
					break;
				}
			}
		}

		if (remove)
		{
			files->erase(files->begin() + q);
			--q;
		}
	}
}


bool DevicenameExists(const char* name)
{
	for (auto i = 0; i < MAX_HD_DEVICES; ++i)
	{
		auto* uci = &changed_prefs.mountconfig[i];
		auto* const ci = &uci->ci;

		if (ci->devname[0])
		{
			if (!strcmp(ci->devname, name))
				return true;
			if (!strcmp(ci->volname, name))
				return true;
		}
	}
	return false;
}


void CreateDefaultDevicename(char* name)
{
	auto freeNum = 0;
	auto foundFree = false;

	while (!foundFree && freeNum < 10)
	{
		sprintf(name, "DH%d", freeNum);
		foundFree = !DevicenameExists(name);
		++freeNum;
	}
}


int tweakbootpri(int bp, int ab, int dnm)
{
	if (dnm)
		return BOOTPRI_NOAUTOMOUNT;
	if (!ab)
		return BOOTPRI_NOAUTOBOOT;
	if (bp < -127)
		bp = -127;
	return bp;
}


bool hardfile_testrdb(const TCHAR* filename)
{
	auto isrdb = false;
	auto* f = zfile_fopen(filename, _T("rb"), ZFD_NORMAL);
	uae_u8 tmp[8];

	if (!f)
		return false;
	for (auto i = 0; i < 16; i++)
	{
		zfile_fseek(f, i * 512, SEEK_SET);
		memset(tmp, 0, sizeof tmp);
		zfile_fread(tmp, 1, sizeof tmp, f);
		if (!memcmp(tmp, "RDSK\0\0\0", 7) || !memcmp(tmp, "DRKS\0\0", 6) || (tmp[0] == 0x53 && tmp[1] == 0x10 && tmp[2] == 0x9b && tmp[3] == 0x13 && tmp[4] == 0 && tmp[5] == 0))
		{
			// RDSK or ADIDE "encoded" RDSK
			isrdb = true;
			break;
		}
	}
	zfile_fclose(f);
	return isrdb;
}

bool isguiactive(void)
{
	return gui_active > 0;
}
