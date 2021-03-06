#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>

#include <guisan.hpp>
#include <SDL_ttf.h>
#include <guisan/sdl.hpp>
#include <guisan/sdl/sdltruetypefont.hpp>
#include "SelectorEntry.hpp"

#include "sysdeps.h"
#include "config.h"
#include "options.h"
#include "memory.h"
#include "uae.h"
#include "gui_handling.h"
#include "amiberry_gfx.h"
#include "amiberry_filesys.hpp"
#include "autoconf.h"

#include "inputdevice.h"

#if defined REDQUARK
# include "malifb.h"
# include <sys/mman.h>
# include "threaddep/thread.h"
void open_gui();
void show_gui();
#endif

#if defined(ANDROID)
#include "androidsdl_event.h"
//#include <SDL_screenkeyboard.h>
//#include <SDL_android.h>
#include <android/log.h>
#endif

std::vector<int> joypad_axis_state; // Keep track of horizontal and vertical axis states

bool gui_running = false;
static int last_active_panel = 3;

#define MAX_STARTUP_TITLE 64
#define MAX_STARTUP_MESSAGE 256
static TCHAR startup_title[MAX_STARTUP_TITLE] = _T("");
static TCHAR startup_message[MAX_STARTUP_MESSAGE] = _T("");

void target_startup_msg(const TCHAR* title, const TCHAR* msg)
{
	_tcsncpy(startup_title, title, MAX_STARTUP_TITLE);
	_tcsncpy(startup_message, msg, MAX_STARTUP_MESSAGE);
}

ConfigCategory categories[] = {
	{
		"About", "data/amigainfo.ico", nullptr, nullptr, InitPanelAbout, ExitPanelAbout, RefreshPanelAbout,
		HelpPanelAbout
	},
	{"Paths", "data/paths.ico", nullptr, nullptr, InitPanelPaths, ExitPanelPaths, RefreshPanelPaths, HelpPanelPaths},
	{
		"Quickstart", "data/quickstart.ico", nullptr, nullptr, InitPanelQuickstart, ExitPanelQuickstart,
		RefreshPanelQuickstart, HelpPanelQuickstart
	},
	{
		"Configurations", "data/file.ico", nullptr, nullptr, InitPanelConfig, ExitPanelConfig, RefreshPanelConfig,
		HelpPanelConfig
	},
	{"CPU and FPU", "data/cpu.ico", nullptr, nullptr, InitPanelCPU, ExitPanelCPU, RefreshPanelCPU, HelpPanelCPU},
	{
		"Chipset", "data/cpu.ico", nullptr, nullptr, InitPanelChipset, ExitPanelChipset, RefreshPanelChipset,
		HelpPanelChipset
	},
	{"ROM", "data/chip.ico", nullptr, nullptr, InitPanelROM, ExitPanelROM, RefreshPanelROM, HelpPanelROM},
	{"RAM", "data/chip.ico", nullptr, nullptr, InitPanelRAM, ExitPanelRAM, RefreshPanelRAM, HelpPanelRAM},
	{
		"Floppy drives", "data/35floppy.ico", nullptr, nullptr, InitPanelFloppy, ExitPanelFloppy, RefreshPanelFloppy,
		HelpPanelFloppy
	},
	{"Hard drives/CD", "data/drive.ico", nullptr, nullptr, InitPanelHD, ExitPanelHD, RefreshPanelHD, HelpPanelHD},
	{
		"Display", "data/screen.ico", nullptr, nullptr, InitPanelDisplay, ExitPanelDisplay, RefreshPanelDisplay,
		HelpPanelDisplay
	},
	{"Sound", "data/sound.ico", nullptr, nullptr, InitPanelSound, ExitPanelSound, RefreshPanelSound, HelpPanelSound},
	{"Input", "data/joystick.ico", nullptr, nullptr, InitPanelInput, ExitPanelInput, RefreshPanelInput, HelpPanelInput},
	{
		"Custom controls", "data/controller.png", nullptr, nullptr, InitPanelCustom, ExitPanelCustom,
		RefreshPanelCustom, HelpPanelCustom
	},
	{"Miscellaneous", "data/misc.ico", nullptr, nullptr, InitPanelMisc, ExitPanelMisc, RefreshPanelMisc, HelpPanelMisc},
	{ "Priority", "data/misc.ico", nullptr, nullptr, InitPanelPrio, ExitPanelPrio, RefreshPanelPrio, HelpPanelPrio},
	{
		"Savestates", "data/savestate.png", nullptr, nullptr, InitPanelSavestate, ExitPanelSavestate,
		RefreshPanelSavestate, HelpPanelSavestate
	},
#ifdef ANDROID
	{ "OnScreen",         "data/screen.ico",    NULL, NULL, InitPanelOnScreen,  ExitPanelOnScreen, RefreshPanelOnScreen,  HelpPanelOnScreen },
#endif
	{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
};

enum
{
	PANEL_ABOUT,
	PANEL_PATHS,
	PANEL_QUICKSTART,
	PANEL_CONFIGURATIONS,
	PANEL_CPU,
	PANEL_CHIPSET,
	PANEL_ROM,
	PANEL_RAM,
	PANEL_FLOPPY,
	PANEL_HD,
	PANEL_DISPLAY,
	PANEL_SOUND,
	PANEL_INPUT,
	PANEL_CUSTOM,
	PANEL_MISC,
	PANEL_SAVESTATES,
#ifdef ANDROID
	PANEL_ONSCREEN,
#endif
	NUM_PANELS
};

/*
* SDL Stuff we need
*/
SDL_Joystick* gui_joystick;
SDL_Surface* gui_screen;
SDL_Event gui_event;
SDL_Event touch_event;
SDL_Window* sdl_window;
#ifdef USE_DISPMANX
DISPMANX_RESOURCE_HANDLE_T gui_resource;
DISPMANX_RESOURCE_HANDLE_T black_gui_resource;
DISPMANX_ELEMENT_HANDLE_T gui_element;
int element_present = 0;
#else
#  if defined REDQUARK
extern smp_comm_pipe *volatile display_pipe;
extern uae_sem_t display_sem;
extern bool volatile display_thread_busy;
#  else
SDL_Texture* gui_texture;
SDL_Cursor* cursor;
SDL_Surface* cursor_surface;
#  endif
#endif

/*
* Gui SDL stuff we need
*/
gcn::SDLInput* gui_input;
gcn::SDLGraphics* gui_graphics;
gcn::SDLImageLoader* gui_imageLoader;
gcn::SDLTrueTypeFont* gui_font;

/*
* Gui stuff we need
*/
gcn::Gui* uae_gui;
gcn::Container* gui_top;
gcn::Container* selectors;
gcn::Color gui_baseCol;
gcn::Color colTextboxBackground;
gcn::Color colSelectorInactive;
gcn::Color colSelectorActive;
gcn::FocusHandler* focusHdl;
gcn::Widget* activeWidget;

// Main buttons
gcn::Button* cmdQuit;
gcn::Button* cmdReset;
gcn::Button* cmdRestart;
gcn::Button* cmdStart;
gcn::Button* cmdHelp;
gcn::Button* cmdShutdown;


/* Flag for changes in rtarea:
  Bit 0: any HD in config?
  Bit 1: force because add/remove HD was clicked or new config loaded
  Bit 2: socket_emu on
  Bit 3: mousehack on
  Bit 4: rtgmem on
  Bit 5: chipmem larger than 2MB
  
  gui_rtarea_flags_onenter is set before GUI is shown, bit 1 may change during GUI display.
*/
static int gui_rtarea_flags_onenter;

static int gui_create_rtarea_flag(struct uae_prefs* p)
{
	auto flag = 0;

	if (count_HDs(p) > 0)
		flag |= 1;

	if (p->input_tablet > 0)
		flag |= 8;

	return flag;
}

void gui_force_rtarea_hdchange()
{
	gui_rtarea_flags_onenter |= 2;
}

void gui_restart()
{
	gui_running = false;
}

static void (*refresh_func_after_draw)(void) = nullptr;

void register_refresh_func(void (*func)(void))
{
	refresh_func_after_draw = func;
}

static void show_help_requested()
{
	vector<string> helptext;
	if (categories[last_active_panel].HelpFunc != nullptr && categories[last_active_panel].HelpFunc(helptext))
	{
		//------------------------------------------------
		// Show help for current panel
		//------------------------------------------------
		char title[128];
		snprintf(title, 128, "Help for %s", categories[last_active_panel].category);
		ShowHelp(title, helptext);
	}
}

void cap_fps(Uint64 start, int fps)
{
	const auto end = SDL_GetPerformanceCounter();
	const auto elapsed_ms = static_cast<float>(end - start) / static_cast<float>(SDL_GetPerformanceFrequency()) * 1000.0f;
    float d = 0.0f;
	if (fps == 60)
        d = floor(16.666f - elapsed_ms);
	else if (fps == 50)
        d = floor(20.000f - elapsed_ms);

    if( d > 0.0f ) SDL_Delay( d );
}

void update_gui_screen()
{
#ifdef USE_DISPMANX
	vc_dispmanx_resource_write_data(gui_resource, rgb_mode, gui_screen->pitch, gui_screen->pixels, &blit_rect);
	updateHandle = vc_dispmanx_update_start(0);
	vc_dispmanx_element_change_source(updateHandle, gui_element, gui_resource);
	vc_dispmanx_update_submit_sync(updateHandle);
#elif defined REDQUARK
    show_gui();
#else
	SDL_RenderClear(renderer);
	SDL_UpdateTexture(gui_texture, nullptr, gui_screen->pixels, gui_screen->pitch);
	if (amiberry_options.rotation_angle == 0 || amiberry_options.rotation_angle == 180)
		renderQuad = { 0, 0, gui_screen->w, gui_screen->h };
	else
		renderQuad = { -(GUI_WIDTH - GUI_HEIGHT) / 2, (GUI_WIDTH - GUI_HEIGHT) / 2, gui_screen->w, gui_screen->h };
	
	SDL_RenderCopyEx(renderer, gui_texture, nullptr, &renderQuad, amiberry_options.rotation_angle, nullptr, SDL_FLIP_NONE);
	SDL_RenderPresent(renderer);
#endif
}

#if defined USE_DISPMANX || REDQUARK
#else
void setup_cursor()
{
	// Detect resolution and load appropriate cursor image
	if (strcmp(sdl_video_driver, "x11") != 0 && sdlMode.w > 1280)
	{
		cursor_surface = SDL_LoadBMP(prefix_with_application_directory_path("data/cursor-x2.bmp").c_str());
	}
	else
	{
		cursor_surface = SDL_LoadBMP(prefix_with_application_directory_path("data/cursor.bmp").c_str());
	}

	if (!cursor_surface)
	{
		// Load failed. Log error.
		write_log("Could not load cursor bitmap: %s\n", SDL_GetError());
		return;
	}
	
	auto* formatted_surface = SDL_ConvertSurfaceFormat(cursor_surface, SDL_PIXELFORMAT_RGBA8888, 0);
	if (formatted_surface != nullptr)
	{
		SDL_FreeSurface(cursor_surface);

		// Create new cursor with surface
		cursor = SDL_CreateColorCursor(formatted_surface, 0, 0);
		SDL_FreeSurface(formatted_surface);
	}

	if (!cursor)
	{
		// Cursor creation failed. Log error and free surface
		write_log("Could not create color cursor: %s\n", SDL_GetError());
		cursor_surface = nullptr;
		formatted_surface = nullptr;
		SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW));
		return;
	}

	SDL_SetCursor(cursor);	
}
#endif

void amiberry_gui_init()
{
	//-------------------------------------------------
	// Create new screen for GUI
	//-------------------------------------------------
	if (!gui_screen)
	{
		gui_screen = SDL_CreateRGBSurface(0, GUI_WIDTH, GUI_HEIGHT, 16, 0, 0, 0, 0);
	}
	check_error_sdl(gui_screen == nullptr, "Unable to create GUI surface:");

#ifdef USE_DISPMANX
	displayHandle = vc_dispmanx_display_open(0);
	rgb_mode = VC_IMAGE_RGB565;
	uint32_t vc_gui_image_ptr;
	if (!gui_resource)
		gui_resource = vc_dispmanx_resource_create(rgb_mode, GUI_WIDTH, GUI_HEIGHT, &vc_gui_image_ptr);
	if (!black_gui_resource)
		black_gui_resource = vc_dispmanx_resource_create(rgb_mode, GUI_WIDTH, GUI_HEIGHT, &vc_gui_image_ptr);
	
	vc_dispmanx_rect_set(&blit_rect, 0, 0, GUI_WIDTH, GUI_HEIGHT);
	vc_dispmanx_resource_write_data(gui_resource, rgb_mode, gui_screen->pitch, gui_screen->pixels, &blit_rect);
	vc_dispmanx_resource_write_data(black_gui_resource, rgb_mode, gui_screen->pitch, gui_screen->pixels, &blit_rect);
	vc_dispmanx_rect_set(&src_rect, 0, 0, GUI_WIDTH << 16, GUI_HEIGHT << 16);
	vc_dispmanx_rect_set(&black_rect, 0, 0, modeInfo.width, modeInfo.height);
	// Full screen destination rectangle
	//vc_dispmanx_rect_set(&dst_rect, 0, 0, modeInfo.width, modeInfo.height);

	// Scaled display with correct Aspect Ratio
	const auto want_aspect = static_cast<float>(GUI_WIDTH) / static_cast<float>(GUI_HEIGHT);
	const auto real_aspect = static_cast<float>(modeInfo.width) / static_cast<float>(modeInfo.height);

	SDL_Rect viewport;
	if (want_aspect > real_aspect)
	{
		const auto scale = static_cast<float>(modeInfo.width) / static_cast<float>(GUI_WIDTH);
		viewport.x = 0;
		viewport.w = modeInfo.width;
		viewport.h = static_cast<int>(std::ceil(GUI_HEIGHT * scale));
		viewport.y = (modeInfo.height - viewport.h) / 2;
	}
	else
	{
		const auto scale = static_cast<float>(modeInfo.height) / static_cast<float>(GUI_HEIGHT);
		viewport.y = 0;
		viewport.h = modeInfo.height;
		viewport.w = static_cast<int>(std::ceil(GUI_WIDTH * scale));
		viewport.x = (modeInfo.width - viewport.w) / 2;
	}
	vc_dispmanx_rect_set(&dst_rect, viewport.x, viewport.y, viewport.w, viewport.h);

	if (!element_present)
	{
		element_present = 1;
		updateHandle = vc_dispmanx_update_start(0);

		VC_DISPMANX_ALPHA_T alpha;
		alpha.flags = DISPMANX_FLAGS_ALPHA_FROM_SOURCE;
		alpha.opacity = 255;
		alpha.mask = 0;

		if (!blackscreen_element)
			blackscreen_element = vc_dispmanx_element_add(updateHandle, displayHandle, 0,
				&black_rect, black_gui_resource, &src_rect, DISPMANX_PROTECTION_NONE, &alpha,
				nullptr, DISPMANX_NO_ROTATE);

		if (!gui_element)
			gui_element = vc_dispmanx_element_add(updateHandle, displayHandle, 1,
				&dst_rect, gui_resource, &src_rect, DISPMANX_PROTECTION_NONE, &alpha,
				nullptr, DISPMANX_NO_ROTATE);

		vc_dispmanx_update_submit_sync(updateHandle);
	}
#elif defined REDQUARK
    open_gui();

#else
	setup_cursor();

	if (sdl_window)
	{
		if (amiberry_options.rotation_angle != 0 && amiberry_options.rotation_angle != 180)
			SDL_SetWindowSize(sdl_window, GUI_HEIGHT, GUI_WIDTH);
		else
			SDL_SetWindowSize(sdl_window, GUI_WIDTH, GUI_HEIGHT);
	}

	// make the scaled rendering look smoother (linear scaling).
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	gui_texture = SDL_CreateTexture(renderer, gui_screen->format->format, SDL_TEXTUREACCESS_STREAMING, gui_screen->w,
									gui_screen->h);
	check_error_sdl(gui_texture == nullptr, "Unable to create GUI texture:");
#endif
	
	if (amiberry_options.rotation_angle == 0 || amiberry_options.rotation_angle == 180)
		SDL_RenderSetLogicalSize(renderer, GUI_WIDTH, GUI_HEIGHT);
	else
		SDL_RenderSetLogicalSize(renderer, GUI_HEIGHT, GUI_WIDTH);
	
	set_mouse_grab(false);

	//-------------------------------------------------
	// Create helpers for GUI framework
	//-------------------------------------------------
	gui_imageLoader = new gcn::SDLImageLoader();
	// The ImageLoader in use is static and must be set to be
	// able to load images
	gui_imageLoader->setRenderer(renderer);
	gcn::Image::setImageLoader(gui_imageLoader);
	gui_graphics = new gcn::SDLGraphics();
	// Set the target for the graphics object to be the screen.
	// In other words, we will draw to the screen.
	// Note, any surface will do, it doesn't have to be the screen.
	gui_graphics->setTarget(gui_screen);
	gui_input = new gcn::SDLInput();
	uae_gui = new gcn::Gui();
	uae_gui->setGraphics(gui_graphics);
	uae_gui->setInput(gui_input);
}

void amiberry_gui_halt()
{
	delete uae_gui;
	delete gui_imageLoader;
	delete gui_input;
	delete gui_graphics;

	if (gui_screen != nullptr)
	{
		SDL_FreeSurface(gui_screen);
		gui_screen = nullptr;
	}
#ifdef USE_DISPMANX
	if (element_present == 1)
	{
		element_present = 0;
		updateHandle = vc_dispmanx_update_start(0);
		vc_dispmanx_element_remove(updateHandle, gui_element);
		gui_element = 0;
		vc_dispmanx_element_remove(updateHandle, blackscreen_element);
		blackscreen_element = 0;
		vc_dispmanx_update_submit_sync(updateHandle);
	}
	
	if (gui_resource)
	{
		vc_dispmanx_resource_delete(gui_resource);
		gui_resource = 0;
	}

	if (black_gui_resource)
	{
		vc_dispmanx_resource_delete(black_gui_resource);
		black_gui_resource = 0;
	}
	if (displayHandle)
		vc_dispmanx_display_close(displayHandle);

#elif defined REDQUARK
#else
	if (gui_texture != nullptr)
	{
		SDL_DestroyTexture(gui_texture);
		gui_texture = nullptr;
	}

	if (cursor != nullptr)
	{
		SDL_FreeCursor(cursor);
		cursor = nullptr;
	}

	// Clear the screen
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
#endif
}

// Return the state of a joypad axis
// -1 (left or up), 0 (centered) or 1 (right or down)
int get_joypad_axis_state(int axis)
{
	if (!gui_joystick)
		return 0;

	const auto state = SDL_JoystickGetAxis(gui_joystick, axis);

	int result;
	if (std::abs(state) < 10000)
		result = 0;
	else
		result = state > 0 ? 1 : -1;

	return result;
}

void check_input()
{
	const auto key_for_gui = SDL_GetKeyFromName(currprefs.open_gui);
	int gotEvent = 0;
	
	while (SDL_PollEvent(&gui_event))
	{
		switch (gui_event.type)
		{
		case SDL_QUIT:
			gotEvent = 1;
			//-------------------------------------------------
			// Quit entire program via SQL-Quit
			//-------------------------------------------------
			uae_quit();
			gui_running = false;
			break;

		case SDL_JOYHATMOTION:
		case SDL_JOYBUTTONDOWN:
			if (gui_joystick)
			{
				gotEvent = 1;
				const int hat = SDL_JoystickGetHat(gui_joystick, 0);

				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].dpad_up) || hat & SDL_HAT_UP) // dpad
				{
					if (HandleNavigation(DIRECTION_UP))
						continue; // Don't change value when enter Slider -> don't send event to control
					PushFakeKey(SDLK_UP);
					break;
				}
				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].dpad_down) || hat & SDL_HAT_DOWN) // dpad
				{
					if (HandleNavigation(DIRECTION_DOWN))
						continue; // Don't change value when enter Slider -> don't send event to control
					PushFakeKey(SDLK_DOWN);
					break;
				}

				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].left_shoulder)) // dpad
				{
					for (auto z = 0; z < 10; ++z)
					{
						PushFakeKey(SDLK_UP);
					}
				}
				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].right_shoulder)) // dpad
				{
					for (auto z = 0; z < 10; ++z)
					{
						PushFakeKey(SDLK_DOWN);
					}
				}

				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].dpad_right) || hat & SDL_HAT_RIGHT)
					// dpad
				{
					if (HandleNavigation(DIRECTION_RIGHT))
						continue; // Don't change value when enter Slider -> don't send event to control
					PushFakeKey(SDLK_RIGHT);
					break;
				}
				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].dpad_left) || hat & SDL_HAT_LEFT) // dpad
				{
					if (HandleNavigation(DIRECTION_LEFT))
						continue; // Don't change value when enter Slider -> don't send event to control
					PushFakeKey(SDLK_LEFT);
					break;
				}
				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].south_button)) // need this to be X button
				{
					PushFakeKey(SDLK_RETURN);
					continue;
				}

				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].quit_button) &&
					SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].hotkey_button)) // use the HOTKEY button
				{
					uae_quit();
					gui_running = false;
					break;
				}
				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].left_trigger))
				{
					show_help_requested();
					cmdHelp->requestFocus();
					break;
				}
				if (SDL_JoystickGetButton(gui_joystick, host_input_buttons[0].menu_button)) // use the HOTKEY button
				{
					gui_running = false;
				}
			}
			break;

		case SDL_JOYAXISMOTION:
			if (gui_joystick)
			{
				gotEvent = 1;
				// Deadzone
				if (std::abs(gui_event.jaxis.value) >= 10000 || std::abs(gui_event.jaxis.value) <= 5000)
				{
					int axis_state;
					int axis = gui_event.jaxis.axis;
					int value = gui_event.jaxis.value;
					if (std::abs(value) < 10000)
						axis_state = 0;
					else
						axis_state = value > 0 ? 1 : -1;

					if (joypad_axis_state[axis] == axis_state)
					{
						// ignore repeated axis movement state
						break;
					}
					joypad_axis_state[axis] = axis_state;

					if (get_joypad_axis_state(host_input_buttons[0].lstick_axis_y) == -1)
					{
						if (HandleNavigation(DIRECTION_UP))
							continue; // Don't change value when enter Slider -> don't send event to control
						PushFakeKey(SDLK_UP);
						break;
					}
					if (get_joypad_axis_state(host_input_buttons[0].lstick_axis_y) == 1)
					{
						if (HandleNavigation(DIRECTION_DOWN))
							continue; // Don't change value when enter Slider -> don't send event to control
						PushFakeKey(SDLK_DOWN);
						break;
					}
					if (get_joypad_axis_state(host_input_buttons[0].lstick_axis_x) == 1)
					{
						if (HandleNavigation(DIRECTION_RIGHT))
							continue; // Don't change value when enter Slider -> don't send event to control
						PushFakeKey(SDLK_RIGHT);
						break;
					}
					if (get_joypad_axis_state(host_input_buttons[0].lstick_axis_x) == -1)
					{
						if (HandleNavigation(DIRECTION_LEFT))
							continue; // Don't change value when enter Slider -> don't send event to control
						PushFakeKey(SDLK_LEFT);
						break;
					}
				}
			}
			break;

		case SDL_KEYDOWN:
			gotEvent = 1;
			if (gui_event.key.keysym.sym == key_for_gui)
			{
				if (emulating && cmdStart->isEnabled())
				{
					//------------------------------------------------
					// Continue emulation
					//------------------------------------------------
					gui_running = false;
				}
				else
				{
					//------------------------------------------------
					// First start of emulator -> reset Amiga
					//------------------------------------------------
					uae_reset(0, 1);
					gui_running = false;
				}
			}
			else
				switch (gui_event.key.keysym.sym)
				{
				case SDLK_q:
					//-------------------------------------------------
					// Quit entire program via Q on keyboard
					//-------------------------------------------------
					focusHdl = gui_top->_getFocusHandler();
					activeWidget = focusHdl->getFocused();
					if (dynamic_cast<gcn::TextField*>(activeWidget) == nullptr)
					{
						// ...but only if we are not in a Textfield...
						uae_quit();
						gui_running = false;
					}
					break;

				case VK_ESCAPE:
				case VK_Red:
					gui_running = false;
					break;


				case VK_Green:
				case VK_Blue:
					//------------------------------------------------
					// Simulate press of enter when 'X' pressed
					//------------------------------------------------
					gui_event.key.keysym.sym = SDLK_RETURN;

					gui_input->pushInput(gui_event); // Fire key down
					gui_event.type = SDL_KEYUP; // and the key up
					break;

				case VK_UP:
					if (HandleNavigation(DIRECTION_UP))
						continue; // Don't change value when enter ComboBox -> don't send event to control
					break;

				case VK_DOWN:
					if (HandleNavigation(DIRECTION_DOWN))
						continue; // Don't change value when enter ComboBox -> don't send event to control
					break;

				case VK_LEFT:
					if (HandleNavigation(DIRECTION_LEFT))
						continue; // Don't change value when enter Slider -> don't send event to control
					break;

				case VK_RIGHT:
					if (HandleNavigation(DIRECTION_RIGHT))
						continue; // Don't change value when enter Slider -> don't send event to control
					break;

				case SDLK_F1:
					show_help_requested();
					cmdHelp->requestFocus();
					break;

				default:
					break;
				}
			break;

		case SDL_FINGERDOWN:
			gotEvent = 1;
			memcpy(&touch_event, &gui_event, sizeof gui_event);
			touch_event.type = SDL_MOUSEBUTTONDOWN;
			touch_event.button.which = 0;
			touch_event.button.button = SDL_BUTTON_LEFT;
			touch_event.button.state = SDL_PRESSED;
			touch_event.button.x = gui_graphics->getTarget()->w * gui_event.tfinger.x;
			touch_event.button.y = gui_graphics->getTarget()->h * gui_event.tfinger.y;
			gui_input->pushInput(touch_event);
			break;

		case SDL_FINGERUP:
			gotEvent = 1;
			memcpy(&touch_event, &gui_event, sizeof gui_event);
			touch_event.type = SDL_MOUSEBUTTONUP;
			touch_event.button.which = 0;
			touch_event.button.button = SDL_BUTTON_LEFT;
			touch_event.button.state = SDL_RELEASED;
			touch_event.button.x = gui_graphics->getTarget()->w * gui_event.tfinger.x;
			touch_event.button.y = gui_graphics->getTarget()->h * gui_event.tfinger.y;
			gui_input->pushInput(touch_event);
			break;

		case SDL_FINGERMOTION:
			gotEvent = 1;
			memcpy(&touch_event, &gui_event, sizeof gui_event);
			touch_event.type = SDL_MOUSEMOTION;
			touch_event.motion.which = 0;
			touch_event.motion.state = 0;
			touch_event.motion.x = gui_graphics->getTarget()->w * gui_event.tfinger.x;
			touch_event.motion.y = gui_graphics->getTarget()->h * gui_event.tfinger.y;
			gui_input->pushInput(touch_event);
			break;

		case SDL_KEYUP:
		case SDL_JOYBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEMOTION:
		case SDL_MOUSEWHEEL:
			gotEvent = 1;
			break;
			
		default:
			break;
		}

		//-------------------------------------------------
		// Send event to gui-controls
		//-------------------------------------------------
#ifdef ANDROID
		androidsdl_event(gui_event, gui_input);
#else
		gui_input->pushInput(gui_event);
#endif
	}
	
	if (gotEvent)
	{
		// Now we let the Gui object perform its logic.
		uae_gui->logic();
		// Now we let the Gui object draw itself.
		uae_gui->draw();

		update_gui_screen();
	}
}

void amiberry_gui_run()
{
	if (amiberry_options.gui_joystick_control)
	{
		const auto available_joysticks = SDL_NumJoysticks();
		if (available_joysticks > 0)
		{
			for (auto j = 0; j <= available_joysticks; j++)
			{
				gui_joystick = SDL_JoystickOpen(j);
				// Some joysticks have no axes or buttons (e.g. Wii Remote IR), skip those
				if (SDL_JoystickNumAxes(gui_joystick) > 0 && SDL_JoystickNumButtons(gui_joystick) > 0)
				{
					joypad_axis_state.assign(SDL_JoystickNumAxes(gui_joystick), 0);
					break;
				}

				SDL_JoystickClose(gui_joystick);
				gui_joystick = nullptr;
			}
		}
	}

	// Prepare the screen once
	uae_gui->logic();
	uae_gui->draw();
	update_gui_screen();
	
	//-------------------------------------------------
	// The main loop
	//-------------------------------------------------
	while (gui_running)
	{
		const auto start = SDL_GetPerformanceCounter();
		check_input();

		if (gui_rtarea_flags_onenter != gui_create_rtarea_flag(&changed_prefs))
			disable_resume();

		if (refresh_func_after_draw != nullptr)
		{
			void (*currFunc)() = refresh_func_after_draw;
			refresh_func_after_draw = nullptr;
			currFunc();
		}

		cap_fps(start, 60);
	}

	if (gui_joystick)
	{
		SDL_JoystickClose(gui_joystick);
		gui_joystick = nullptr;
		joypad_axis_state.clear();
	}
}

class MainButtonActionListener : public gcn::ActionListener
{
public:
	void action(const gcn::ActionEvent& actionEvent) override
	{
		if (actionEvent.getSource() == cmdShutdown)
		{
			// ------------------------------------------------
			// Shutdown the host (power off)
			// ------------------------------------------------
			uae_quit();
			gui_running = false;
			host_poweroff = true;
		}

		if (actionEvent.getSource() == cmdQuit)
		{
			//-------------------------------------------------
			// Quit entire program via click on Quit-button
			//-------------------------------------------------
			uae_quit();
			gui_running = false;
		}
		else if (actionEvent.getSource() == cmdReset)
		{
			//-------------------------------------------------
			// Reset Amiga via click on Reset-button
			//-------------------------------------------------
			uae_reset(1, 1);
			gui_running = false;
		}
		else if (actionEvent.getSource() == cmdRestart)
		{
			//-------------------------------------------------
			// Restart emulator
			//-------------------------------------------------
			char tmp[MAX_DPATH];
			get_configuration_path(tmp, sizeof tmp);
			if (strlen(last_loaded_config) > 0)
				strncat(tmp, last_loaded_config, MAX_DPATH - 1);
			else
			{
				strncat(tmp, OPTIONSFILENAME, MAX_DPATH - 1);
				strncat(tmp, ".uae", MAX_DPATH - 10);
			}
			uae_restart(-1, tmp);
			gui_running = false;
		}
		else if (actionEvent.getSource() == cmdStart)
		{
			if (emulating && cmdStart->isEnabled())
			{
				//------------------------------------------------
				// Continue emulation
				//------------------------------------------------
				gui_running = false;
			}
			else
			{
				//------------------------------------------------
				// First start of emulator -> reset Amiga
				//------------------------------------------------
				uae_reset(0, 1);
				gui_running = false;
			}
		}
		else if (actionEvent.getSource() == cmdHelp)
		{
			show_help_requested();
			cmdHelp->requestFocus();
		}
	}
};

MainButtonActionListener* mainButtonActionListener;

class PanelFocusListener : public gcn::FocusListener
{
public:
	void focusGained(const gcn::Event& event) override
	{
		for (auto i = 0; categories[i].category != nullptr; ++i)
		{
			if (event.getSource() == categories[i].selector)
			{
				categories[i].selector->setActive(true);
				categories[i].panel->setVisible(true);
				last_active_panel = i;
				cmdHelp->setVisible(categories[last_active_panel].HelpFunc != nullptr);
			}
			else
			{
				categories[i].selector->setActive(false);
				categories[i].panel->setVisible(false);
			}
		}
	}
};

PanelFocusListener* panelFocusListener;

void gui_widgets_init()
{
	int i;
	int yPos;

	//-------------------------------------------------
	// Define base colors
	//-------------------------------------------------
	gui_baseCol = gcn::Color(170, 170, 170);
	colSelectorInactive = gcn::Color(170, 170, 170);
	colSelectorActive = gcn::Color(103, 136, 187);
	colTextboxBackground = gcn::Color(220, 220, 220);

	//-------------------------------------------------
	// Create container for main page
	//-------------------------------------------------
	gui_top = new gcn::Container();
	gui_top->setDimension(gcn::Rectangle(0, 0, GUI_WIDTH, GUI_HEIGHT));
	gui_top->setBaseColor(gui_baseCol);
	uae_gui->setTop(gui_top);

	//-------------------------------------------------
	// Initialize fonts
	//-------------------------------------------------
	TTF_Init();

	try
	{
		gui_font = new gcn::SDLTrueTypeFont(prefix_with_application_directory_path("data/AmigaTopaz.ttf"), 15);
	}
	catch (const std::exception& ex)
	{
		write_log("Could not open data/AmigaTopaz.ttf!\n");
		abort();
	}
	catch (...)
	{
		write_log("An error occurred while trying to open data/AmigaTopaz.ttf!\n");
		abort();
	}

	gcn::Widget::setGlobalFont(gui_font);
	gui_font->setAntiAlias(false);

	//--------------------------------------------------
	// Create main buttons
	//--------------------------------------------------
	mainButtonActionListener = new MainButtonActionListener();

	cmdQuit = new gcn::Button("Quit");
	cmdQuit->setSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	cmdQuit->setBaseColor(gui_baseCol);
	cmdQuit->setId("Quit");
	cmdQuit->addActionListener(mainButtonActionListener);

	cmdShutdown = new gcn::Button("Shutdown");
	cmdShutdown->setSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	cmdShutdown->setBaseColor(gui_baseCol);
	cmdShutdown->setId("Shutdown");
	cmdShutdown->addActionListener(mainButtonActionListener);

	cmdReset = new gcn::Button("Reset");
	cmdReset->setSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	cmdReset->setBaseColor(gui_baseCol);
	cmdReset->setId("Reset");
	cmdReset->addActionListener(mainButtonActionListener);

	cmdRestart = new gcn::Button("Restart");
	cmdRestart->setSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	cmdRestart->setBaseColor(gui_baseCol);
	cmdRestart->setId("Restart");
	cmdRestart->addActionListener(mainButtonActionListener);

	cmdStart = new gcn::Button("Start");
	if (emulating)
		cmdStart->setCaption("Resume");
	cmdStart->setSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	cmdStart->setBaseColor(gui_baseCol);
	cmdStart->setId("Start");
	cmdStart->addActionListener(mainButtonActionListener);

	cmdHelp = new gcn::Button("Help");
	cmdHelp->setSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	cmdHelp->setBaseColor(gui_baseCol);
	cmdHelp->setId("Help");
	cmdHelp->addActionListener(mainButtonActionListener);

	//--------------------------------------------------
	// Create selector entries
	//--------------------------------------------------
	const auto workAreaHeight = GUI_HEIGHT - 2 * DISTANCE_BORDER - BUTTON_HEIGHT - DISTANCE_NEXT_Y;
	selectors = new gcn::Container();
	selectors->setSize(150, workAreaHeight - 2);
	selectors->setBaseColor(colSelectorInactive);
	selectors->setBorderSize(1);
	const auto panelStartX = DISTANCE_BORDER + selectors->getWidth() + 2 + 11;

	panelFocusListener = new PanelFocusListener();
	for (i = 0; categories[i].category != nullptr; ++i)
	{
		categories[i].selector = new gcn::SelectorEntry(categories[i].category, prefix_with_application_directory_path(categories[i].imagepath));
		categories[i].selector->setActiveColor(colSelectorActive);
		categories[i].selector->setInactiveColor(colSelectorInactive);
		categories[i].selector->setSize(150, 24);
		categories[i].selector->addFocusListener(panelFocusListener);

		categories[i].panel = new gcn::Container();
		categories[i].panel->setId(categories[i].category);
		categories[i].panel->setSize(GUI_WIDTH - panelStartX - DISTANCE_BORDER - 1, workAreaHeight - 2);
		categories[i].panel->setBaseColor(gui_baseCol);
		categories[i].panel->setBorderSize(1);
		categories[i].panel->setVisible(false);
	}

	//--------------------------------------------------
	// Initialize panels
	//--------------------------------------------------
	for (i = 0; categories[i].category != nullptr; ++i)
	{
		if (categories[i].InitFunc != nullptr)
			(*categories[i].InitFunc)(categories[i]);
	}

	//--------------------------------------------------
	// Place everything on main form
	//--------------------------------------------------
#ifndef ANDROID
	gui_top->add(cmdShutdown, DISTANCE_BORDER, GUI_HEIGHT - DISTANCE_BORDER - BUTTON_HEIGHT);
#endif
	gui_top->add(cmdQuit, DISTANCE_BORDER + BUTTON_WIDTH + DISTANCE_NEXT_X,
				 GUI_HEIGHT - DISTANCE_BORDER - BUTTON_HEIGHT);
	gui_top->add(cmdRestart, DISTANCE_BORDER + 2 * BUTTON_WIDTH + 2 * DISTANCE_NEXT_X,
				 GUI_HEIGHT - DISTANCE_BORDER - BUTTON_HEIGHT);
	gui_top->add(cmdHelp, DISTANCE_BORDER + 3 * BUTTON_WIDTH + 3 * DISTANCE_NEXT_X,
				 GUI_HEIGHT - DISTANCE_BORDER - BUTTON_HEIGHT);
	gui_top->add(cmdReset, DISTANCE_BORDER + 5 * BUTTON_WIDTH + 5 * DISTANCE_NEXT_X,
				 GUI_HEIGHT - DISTANCE_BORDER - BUTTON_HEIGHT);
	gui_top->add(cmdStart, GUI_WIDTH - DISTANCE_BORDER - BUTTON_WIDTH, GUI_HEIGHT - DISTANCE_BORDER - BUTTON_HEIGHT);

	gui_top->add(selectors, DISTANCE_BORDER + 1, DISTANCE_BORDER + 1);
	for (i = 0, yPos = 0; categories[i].category != nullptr; ++i, yPos += 24)
	{
		selectors->add(categories[i].selector, 0, yPos);
		gui_top->add(categories[i].panel, panelStartX, DISTANCE_BORDER + 1);
	}

	//--------------------------------------------------
	// Activate last active panel
	//--------------------------------------------------
	if (!emulating && amiberry_options.quickstart_start)
		last_active_panel = 2;
	categories[last_active_panel].selector->requestFocus();
	cmdHelp->setVisible(categories[last_active_panel].HelpFunc != nullptr);
}

void gui_widgets_halt()
{
	for (auto i = 0; categories[i].category != nullptr; ++i)
	{
		if (categories[i].ExitFunc != nullptr)
			(*categories[i].ExitFunc)();

		delete categories[i].selector;
		delete categories[i].panel;
	}

	delete panelFocusListener;
	delete selectors;

	delete cmdQuit;
	delete cmdShutdown;
	delete cmdReset;
	delete cmdRestart;
	delete cmdStart;
	delete cmdHelp;

	delete mainButtonActionListener;

	delete gui_font;
	delete gui_top;
}

void refresh_all_panels()
{
	for (auto i = 0; categories[i].category != nullptr; ++i)
	{
		if (categories[i].RefreshFunc != nullptr)
			(*categories[i].RefreshFunc)();
	}
}

void disable_resume()
{
	if (emulating)
	{
		cmdStart->setEnabled(false);
	}
}

void run_gui()
{
#if 0
#ifdef ANDROID
	SDL_ANDROID_SetScreenKeyboardShown(0);
	SDL_ANDROID_SetSystemMousePointerVisible(1);
#endif
#endif
	gui_running = true;
	gui_rtarea_flags_onenter = gui_create_rtarea_flag(&currprefs);

	expansion_generate_autoconfig_info(&changed_prefs);

	try
	{
		amiberry_gui_init();
		gui_widgets_init();
		if (_tcslen(startup_message) > 0)
		{
			ShowMessage(startup_title, startup_message, _T(""), _T("Ok"), _T(""));
			_tcscpy(startup_title, _T(""));
			_tcscpy(startup_message, _T(""));
			cmdStart->requestFocus();
		}
		amiberry_gui_run();
		gui_widgets_halt();
		amiberry_gui_halt();
#if 0
#ifdef ANDROID
		if (currprefs.onScreen != 0)
		{
			SDL_ANDROID_SetScreenKeyboardShown(1);
			SDL_ANDROID_SetSystemMousePointerVisible(0);
		}
#endif
#endif
	}

	// Catch all GUI framework exceptions.
	catch (gcn::Exception& e)
	{
		std::cout << e.getMessage() << std::endl;
		uae_quit();
	}

	// Catch all Std exceptions.
	catch (exception& e)
	{
		std::cout << "Std exception: " << e.what() << std::endl;
		uae_quit();
	}

	// Catch all unknown exceptions.
	catch (...)
	{
		std::cout << "Unknown exception" << std::endl;
		uae_quit();
	}

	expansion_generate_autoconfig_info(&changed_prefs);
	cfgfile_compatibility_romtype(&changed_prefs);

	if (quit_program > UAE_QUIT || quit_program < -UAE_QUIT)
	{
		//--------------------------------------------------
		// Prepare everything for Reset of Amiga
		//--------------------------------------------------
		currprefs.nr_floppies = changed_prefs.nr_floppies;

		if (gui_rtarea_flags_onenter != gui_create_rtarea_flag(&changed_prefs))
			quit_program = -UAE_RESET_HARD; // Hardreset required...
	}

	// Reset counter for access violations
	init_max_signals();
}
