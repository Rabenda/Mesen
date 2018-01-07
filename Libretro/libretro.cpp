#include "stdafx.h"
#include <string>
#include <sstream>
#include <algorithm>
#include "LibretroRenderer.h"
#include "LibretroSoundManager.h"
#include "LibretroKeyManager.h"
#include "LibretroMessageManager.h"
#include "libretro.h"
#include "../Core/Console.h"
#include "../Core/VideoDecoder.h"
#include "../Core/VideoRenderer.h"
#include "../Core/EmulationSettings.h"
#include "../Core/CheatManager.h"
#include "../Core/DebuggerTypes.h"
#include "../Utilities/FolderUtilities.h"
#include "../Utilities/HexUtilities.h"

static retro_log_printf_t logCallback = nullptr;
static retro_environment_t retroEnv = nullptr;
static bool _hdPacksEnabled = false;
static string _mesenVersion = "";

//Include game database as an array of strings (need an automated way to generate the include file)
static vector<string> gameDb = {
#include "MesenDB.inc"
};

static std::unique_ptr<LibretroRenderer> _renderer;
static std::unique_ptr<LibretroSoundManager> _soundManager;
static std::unique_ptr<LibretroKeyManager> _keyManager;
static std::unique_ptr<LibretroMessageManager> _messageManager;

static const char* MesenNtscFilter = "mesen_ntsc_filter";
static const char* MesenNtscVerticalBlend = "mesen_ntsc_vertical_blend";
static const char* MesenPalette = "mesen_palette";
static const char* MesenNoSpriteLimit = "mesen_nospritelimit";
static const char* MesenOverclock = "mesen_overclock";
static const char* MesenOverclockType = "mesen_overclock_type";
static const char* MesenOverscanVertical = "mesen_overscan_vertical";
static const char* MesenOverscanHorizontal = "mesen_overscan_horizontal";
static const char* MesenAspectRatio = "mesen_aspect_ratio";
static const char* MesenRegion = "mesen_region";
static const char* MesenRamState = "mesen_ramstate";
static const char* MesenControllerTurboSpeed = "mesen_controllerturbospeed";
static const char* MesenFdsAutoSelectDisk = "mesen_fdsautoinsertdisk";
static const char* MesenFdsFastForwardLoad = "mesen_fdsfastforwardload";
static const char* MesenHdPacks = "mesen_hdpacks";
static const char* MesenScreenRotation = "mesen_screenrotation";
static const char* MesenFakeStereo = "mesen_fake_stereo";
static const char* MesenMuteTriangleUltrasonic = "mesen_mute_triangle_ultrasonic";
static const char* MesenReduceDmcPopping = "mesen_reduce_dmc_popping";
static const char* MesenSwapDutyCycle = "mesen_swap_duty_cycle";
static const char* MesenDisableNoiseModeFlag = "mesen_disable_noise_mode_flag";

uint32_t defaultPalette[0x40] { 0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00, 0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08, 0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00, 0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32, 0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF, 0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22, 0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000, 0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5, 0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000 };
uint32_t unsaturatedPalette[0x40] { 0xFF6B6B6B, 0xFF001E87, 0xFF1F0B96, 0xFF3B0C87, 0xFF590D61, 0xFF5E0528, 0xFF551100, 0xFF461B00, 0xFF303200, 0xFF0A4800, 0xFF004E00, 0xFF004619, 0xFF003A58, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFB2B2B2, 0xFF1A53D1, 0xFF4835EE, 0xFF7123EC, 0xFF9A1EB7, 0xFFA51E62, 0xFFA52D19, 0xFF874B00, 0xFF676900, 0xFF298400, 0xFF038B00, 0xFF008240, 0xFF007891, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF63ADFD, 0xFF908AFE, 0xFFB977FC, 0xFFE771FE, 0xFFF76FC9, 0xFFF5836A, 0xFFDD9C29, 0xFFBDB807, 0xFF84D107, 0xFF5BDC3B, 0xFF48D77D, 0xFF48CCCE, 0xFF555555, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFC4E3FE, 0xFFD7D5FE, 0xFFE6CDFE, 0xFFF9CAFE, 0xFFFEC9F0, 0xFFFED1C7, 0xFFF7DCAC, 0xFFE8E89C, 0xFFD1F29D, 0xFFBFF4B1, 0xFFB7F5CD, 0xFFB7F0EE, 0xFFBEBEBE, 0xFF000000, 0xFF000000 };
uint32_t yuvPalette[0x40] { 0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0700, 0xFF561D00, 0xFF333500, 0xFF0C4800, 0xFF005200, 0xFF004C18, 0xFF003E5B, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00, 0xFF6B6D00, 0xFF388700, 0xFF0D9300, 0xFF008C47, 0xFF007AA0, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF, 0xFFF26AFF, 0xFFFF6ECC, 0xFFFF8170, 0xFFEA9E22, 0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFAC2FF, 0xFFFFC4EA, 0xFFFFCCC5, 0xFFF7D8A5, 0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000 };
uint32_t nestopiaRgbPalette[0x40] { 0xFF6D6D6D, 0xFF002492, 0xFF0000DB, 0xFF6D49DB, 0xFF92006D, 0xFFB6006D, 0xFFB62400, 0xFF924900, 0xFF6D4900, 0xFF244900, 0xFF006D24, 0xFF009200, 0xFF004949, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFB6B6B6, 0xFF006DDB, 0xFF0049FF, 0xFF9200FF, 0xFFB600FF, 0xFFFF0092, 0xFFFF0000, 0xFFDB6D00, 0xFF926D00, 0xFF249200, 0xFF009200, 0xFF00B66D, 0xFF009292, 0xFF242424, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF6DB6FF, 0xFF9292FF, 0xFFDB6DFF, 0xFFFF00FF, 0xFFFF6DFF, 0xFFFF9200, 0xFFFFB600, 0xFFDBDB00, 0xFF6DDB00, 0xFF00FF00, 0xFF49FFDB, 0xFF00FFFF, 0xFF494949, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFB6DBFF, 0xFFDBB6FF, 0xFFFFB6FF, 0xFFFF92FF, 0xFFFFB6B6, 0xFFFFDB92, 0xFFFFFF49, 0xFFFFFF6D, 0xFFB6FF49, 0xFF92FF6D, 0xFF49FFDB, 0xFF92DBFF, 0xFF929292, 0xFF000000, 0xFF000000 };
uint32_t compositeDirectPalette[0x40] { 0xFF656565, 0xFF00127D, 0xFF18008E, 0xFF360082, 0xFF56005D, 0xFF5A0018, 0xFF4F0500, 0xFF381900, 0xFF1D3100, 0xFF003D00, 0xFF004100, 0xFF003B17, 0xFF002E55, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFAFAFAF, 0xFF194EC8, 0xFF472FE3, 0xFF6B1FD7, 0xFF931BAE, 0xFF9E1A5E, 0xFF993200, 0xFF7B4B00, 0xFF5B6700, 0xFF267A00, 0xFF008200, 0xFF007A3E, 0xFF006E8A, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF64A9FF, 0xFF8E89FF, 0xFFB676FF, 0xFFE06FFF, 0xFFEF6CC4, 0xFFF0806A, 0xFFD8982C, 0xFFB9B40A, 0xFF83CB0C, 0xFF5BD63F, 0xFF4AD17E, 0xFF4DC7CB, 0xFF4C4C4C, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFC7E5FF, 0xFFD9D9FF, 0xFFE9D1FF, 0xFFF9CEFF, 0xFFFFCCF1, 0xFFFFD4CB, 0xFFF8DFB1, 0xFFEDEAA4, 0xFFD6F4A4, 0xFFC5F8B8, 0xFFBEF6D3, 0xFFBFF1F1, 0xFFB9B9B9, 0xFF000000, 0xFF000000 };
uint32_t nesClassicPalette[0x40] { 0xFF60615F, 0xFF000083, 0xFF1D0195, 0xFF340875, 0xFF51055E, 0xFF56000F, 0xFF4C0700, 0xFF372308, 0xFF203A0B, 0xFF0F4B0E, 0xFF194C16, 0xFF02421E, 0xFF023154, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFA9AAA8, 0xFF104BBF, 0xFF4712D8, 0xFF6300CA, 0xFF8800A9, 0xFF930B46, 0xFF8A2D04, 0xFF6F5206, 0xFF5C7114, 0xFF1B8D12, 0xFF199509, 0xFF178448, 0xFF206B8E, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFBFBFB, 0xFF6699F8, 0xFF8974F9, 0xFFAB58F8, 0xFFD557EF, 0xFFDE5FA9, 0xFFDC7F59, 0xFFC7A224, 0xFFA7BE03, 0xFF75D703, 0xFF60E34F, 0xFF3CD68D, 0xFF56C9CC, 0xFF414240, 0xFF000000, 0xFF000000, 0xFFFBFBFB, 0xFFBED4FA, 0xFFC9C7F9, 0xFFD7BEFA, 0xFFE8B8F9, 0xFFF5BAE5, 0xFFF3CAC2, 0xFFDFCDA7, 0xFFD9E09C, 0xFFC9EB9E, 0xFFC0EDB8, 0xFFB5F4C7, 0xFFB9EAE9, 0xFFABABAB, 0xFF000000, 0xFF000000 };
uint32_t originalHardwarePalette[0x40] { 0xFF6A6D6A, 0xFF00127D, 0xFF1E008A, 0xFF3B007D, 0xFF56005D, 0xFF5A0018, 0xFF4F0D00, 0xFF381E00, 0xFF203100, 0xFF003D00, 0xFF004000, 0xFF003B1E, 0xFF002E55, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFB9BCB9, 0xFF194EC8, 0xFF472FE3, 0xFF751FD7, 0xFF931EAD, 0xFF9E245E, 0xFF963800, 0xFF7B5000, 0xFF5B6700, 0xFF267A00, 0xFF007F00, 0xFF007842, 0xFF006E8A, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF69AEFF, 0xFF9798FF, 0xFFB687FF, 0xFFE278FF, 0xFFF279C7, 0xFFF58F6F, 0xFFDDA932, 0xFFBCB70D, 0xFF88D015, 0xFF60DB49, 0xFF4FD687, 0xFF50CACE, 0xFF515451, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFCCEAFF, 0xFFDEE2FF, 0xFFEEDAFF, 0xFFFAD7FD, 0xFFFDD7F6, 0xFFFDDCD0, 0xFFFAE8B6, 0xFFF2F1A9, 0xFFDBFBA9, 0xFFCAFFBD, 0xFFC3FBD8, 0xFFC4F6F6, 0xFFBEC1BE, 0xFF000000, 0xFF000000 };
uint32_t pvmStylePalette[0x40] { 0xFF696964, 0xFF001774, 0xFF28007D, 0xFF3E006D, 0xFF560057, 0xFF5E0013, 0xFF531A00, 0xFF3B2400, 0xFF2A3000, 0xFF143A00, 0xFF003F00, 0xFF003B1E, 0xFF003050, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFB9B9B4, 0xFF1453B9, 0xFF4D2CDA, 0xFF7A1EC8, 0xFF98189C, 0xFF9D2344, 0xFFA03E00, 0xFF8D5500, 0xFF656D00, 0xFF2C7900, 0xFF008100, 0xFF007D42, 0xFF00788A, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF69A8FF, 0xFF9A96FF, 0xFFC28AFA, 0xFFEA7DFA, 0xFFF387B4, 0xFFF1986C, 0xFFE6B327, 0xFFD7C805, 0xFF90DF07, 0xFF64E53C, 0xFF45E27D, 0xFF48D5D9, 0xFF4B4B46, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFD2EAFF, 0xFFE2E2FF, 0xFFF2D8FF, 0xFFF8D2FF, 0xFFF8D9EA, 0xFFFADEB9, 0xFFF9E89B, 0xFFF3F28C, 0xFFD3FA91, 0xFFB8FCA8, 0xFFAEFACA, 0xFFCAF3F3, 0xFFBEBEB9, 0xFF000000, 0xFF000000 };
uint32_t sonyCxa2025AsPalette[0x40] { 0xFF585858, 0xFF00238C, 0xFF00139B, 0xFF2D0585, 0xFF5D0052, 0xFF7A0017, 0xFF7A0800, 0xFF5F1800, 0xFF352A00, 0xFF093900, 0xFF003F00, 0xFF003C22, 0xFF00325D, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFA1A1A1, 0xFF0053EE, 0xFF153CFE, 0xFF6028E4, 0xFFA91D98, 0xFFD41E41, 0xFFD22C00, 0xFFAA4400, 0xFF6C5E00, 0xFF2D7300, 0xFF007D06, 0xFF007852, 0xFF0069A9, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF1FA5FE, 0xFF5E89FE, 0xFFB572FE, 0xFFFE65F6, 0xFFFE6790, 0xFFFE773C, 0xFFFE9308, 0xFFC4B200, 0xFF79CA10, 0xFF3AD54A, 0xFF11D1A4, 0xFF06BFFE, 0xFF424242, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFA0D9FE, 0xFFBDCCFE, 0xFFE1C2FE, 0xFFFEBCFB, 0xFFFEBDD0, 0xFFFEC5A9, 0xFFFED18E, 0xFFE9DE86, 0xFFC7E992, 0xFFA8EEB0, 0xFF95ECD9, 0xFF91E4FE, 0xFFACACAC, 0xFF000000, 0xFF000000 };

extern "C" {
	void logMessage(retro_log_level level, const char* message)
	{
		if(logCallback) {
			logCallback(level, message);
		}
	}

	RETRO_API unsigned retro_api_version()
	{
		return RETRO_API_VERSION;
	}

	RETRO_API void retro_init()
	{
		struct retro_log_callback log;
		if(retroEnv(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) {
			logCallback = log.log;
		} else {
			logCallback = nullptr;
		}

		GameDatabase::LoadGameDb(gameDb);

		_renderer.reset(new LibretroRenderer());
		_soundManager.reset(new LibretroSoundManager());
		_keyManager.reset(new LibretroKeyManager());
		_messageManager.reset(new LibretroMessageManager(logCallback));

		EmulationSettings::SetFlags(EmulationFlags::FdsAutoLoadDisk);
		EmulationSettings::SetFlags(EmulationFlags::DisableOsd);
		EmulationSettings::SetFlags(EmulationFlags::AutoConfigureInput);
		EmulationSettings::SetSampleRate(48000);
		EmulationSettings::SetAutoSaveOptions(0, false);
		EmulationSettings::SetRewindBufferSize(0);
	}

	RETRO_API void retro_deinit()
	{
		Console::GetInstance()->SaveBatteries();
		VideoDecoder::Release();
		VideoRenderer::Release();
		Console::Release();
		_renderer.reset();
		_soundManager.reset();
		_keyManager.reset();
		_messageManager.reset();
	}

	RETRO_API void retro_set_environment(retro_environment_t env)
	{
		retroEnv = env;

		static const struct retro_variable vars[] = {
			{ MesenNtscFilter, "NTSC filter; Disabled|Composite (Blargg)|S-Video (Blargg)|RGB (Blargg)|Monochrome (Blargg)|Bisqwit 2x|Bisqwit 4x|Bisqwit 8x" },
			{ MesenNtscVerticalBlend, "NTSC filter: Vertical blending; disabled|enabled" },
			{ MesenPalette, "Palette; Default|Composite Direct (by FirebrandX)|Nes Classic|Nestopia (RGB)|Original Hardware (by FirebrandX)|PVM Style (by FirebrandX)|Sony CXA2025AS|Unsaturated v6 (by FirebrandX)|YUV v3 (by FirebrandX)" },
			{ MesenOverclock, "Overclock; None|Low|Medium|High|Very High" },
			{ MesenOverclockType, "Overclock Type; Before NMI (Recommended)|After NMI" },
			{ MesenRegion, "Region; Auto|NTSC|PAL|Dendy" },
			{ MesenOverscanVertical, "Vertical Overscan; None|8px|16px" },
			{ MesenOverscanHorizontal, "Horizontal Overscan; None|8px|16px" },
			{ MesenAspectRatio ,  "Aspect Ratio; Auto|No Stretching|NTSC|PAL|4:3|16:9" },
			{ MesenControllerTurboSpeed, "Controller Turbo Speed; Fast|Very Fast|Slow|Normal" },
			{ MesenHdPacks, "Enable HD Packs; enabled|disabled" },
			{ MesenNoSpriteLimit, "Remove sprite limit; enabled|disabled" },
			{ MesenFakeStereo, u8"Enable fake stereo effect; disabled|enabled" },
			{ MesenMuteTriangleUltrasonic, u8"Reduce popping on Triangle channel; enabled|disabled" },
			{ MesenReduceDmcPopping, u8"Reduce popping on DMC channel; enabled|disabled" },
			{ MesenSwapDutyCycle, u8"Swap Square channel duty cycles; disabled|enabled" },
			{ MesenDisableNoiseModeFlag, u8"Disable Noise channel mode flag; disabled|enabled" },
			{ MesenScreenRotation, u8"Screen Rotation; None|90 degrees|180 degrees|270 degrees" },
			{ MesenRamState, "Default power-on state for RAM; All 0s (Default)|All 1s|Random Values" },
			{ MesenFdsAutoSelectDisk, "FDS: Automatically insert disks; disabled|enabled" },
			{ MesenFdsFastForwardLoad, "FDS: Fast forward while loading; disabled|enabled" },
			{ NULL, NULL },
		};

		retroEnv(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
	}

	RETRO_API void retro_set_video_refresh(retro_video_refresh_t sendFrame)
	{
		_renderer->SetSendFrame(sendFrame);
	}

	RETRO_API void retro_set_audio_sample(retro_audio_sample_t sendAudioSample)
	{
		_soundManager->SetSendAudioSample(sendAudioSample);
	}

	RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t audioSampleBatch)
	{
	}

	RETRO_API void retro_set_input_poll(retro_input_poll_t pollInput)
	{	
		_keyManager->SetPollInput(pollInput);		
	}

	RETRO_API void retro_set_input_state(retro_input_state_t getInputState)
	{
		_keyManager->SetGetInputState(getInputState);
	}

	RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
	{
	}

	RETRO_API void retro_reset()
	{
		Console::Reset(true);
	}
	
	void setup_av_info(struct retro_system_av_info *info)
	{
		info->timing.fps = Console::GetModel() == NesModel::NTSC ? 60.098811862348404716732985230828 : 50.006977968268290848936010226333;
		info->timing.sample_rate = 48000;

		float ratio = (float)EmulationSettings::GetAspectRatio();
		if(ratio == 0.0f) {
			ratio = 1.0f;
		}
		ratio *= (float)EmulationSettings::GetOverscanDimensions().GetScreenWidth() / EmulationSettings::GetOverscanDimensions().GetScreenHeight() / 256 * 240;

		if(EmulationSettings::GetScreenRotation() % 180) {
			info->geometry.aspect_ratio = ratio == 0.0f ? 0.0f : 1.0f / ratio;
		} else {
			info->geometry.aspect_ratio = ratio;
		}

		info->geometry.base_width = EmulationSettings::GetOverscanDimensions().GetScreenWidth();
		info->geometry.base_height = EmulationSettings::GetOverscanDimensions().GetScreenHeight();

		//For HD packs:
		info->geometry.max_width = 256 * 10;
		info->geometry.max_height = 240 * 10;
	}

	void set_flag(const char* flagName, uint64_t flagValue)
	{
		struct retro_variable var = {};
		var.key = flagName;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "disabled") {
				EmulationSettings::ClearFlags(flagValue);
			} else {
				EmulationSettings::SetFlags(flagValue);
			}
		}
	}

	void update_settings()
	{
		struct retro_variable var = { };
		EmulationSettings::SetPictureSettings(0, 0, 0, 0, 0);

		_hdPacksEnabled = EmulationSettings::CheckFlag(EmulationFlags::UseHdPacks);

		set_flag(MesenNoSpriteLimit, EmulationFlags::RemoveSpriteLimit | EmulationFlags::AdaptiveSpriteLimit);
		set_flag(MesenHdPacks, EmulationFlags::UseHdPacks);
		set_flag(MesenMuteTriangleUltrasonic, EmulationFlags::SilenceTriangleHighFreq);
		set_flag(MesenReduceDmcPopping, EmulationFlags::ReduceDmcPopping);
		set_flag(MesenSwapDutyCycle, EmulationFlags::SwapDutyCycles);
		set_flag(MesenDisableNoiseModeFlag, EmulationFlags::DisableNoiseModeFlag);
		set_flag(MesenFdsAutoSelectDisk, EmulationFlags::FdsAutoInsertDisk);
		set_flag(MesenFdsFastForwardLoad, EmulationFlags::FdsFastForwardOnLoad);

		var.key = MesenFakeStereo;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "enabled") {
				EmulationSettings::SetStereoFilter(StereoFilter::Delay);
				EmulationSettings::SetStereoDelay(15);
			} else {
				EmulationSettings::SetStereoFilter(StereoFilter::None);
			}
		}
		
		bool verticalBlend = false;
		var.key = MesenNtscVerticalBlend;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			if(string(var.value) == "enabled") {
				verticalBlend = true;
			}
		}

		var.key = MesenNtscFilter;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "Disabled") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::None);
			} else if(value == "Composite (Blargg)") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::NTSC);
				EmulationSettings::SetNtscFilterSettings(0, 0, 0, 0, 0, 0, false, 0, 0, 0, verticalBlend);
			} else if(value == "S-Video (Blargg)") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::NTSC);
				EmulationSettings::SetNtscFilterSettings(-1.0, 0, -1.0, 0, 0.2, 0.2, false, 0, 0, 0, verticalBlend);
			} else if(value == "RGB (Blargg)") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::NTSC);
				EmulationSettings::SetPictureSettings(0, 0, 0, 0, 0);
				EmulationSettings::SetNtscFilterSettings(-1.0, -1.0, -1.0, 0, 0.7, 0.2, false, 0, 0, 0, verticalBlend);
			} else if(value == "Monochrome (Blargg)") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::NTSC);
				EmulationSettings::SetPictureSettings(0, 0, -1.0, 0, 0);
				EmulationSettings::SetNtscFilterSettings(-0.2, -0.1, -0.2, 0, 0.7, 0.2, false, 0, 0, 0, verticalBlend);
			} else if(value == "Bisqwit 2x") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::BisqwitNtscQuarterRes);
				EmulationSettings::SetNtscFilterSettings(0, 0, 0, 0, 0, 0, false, 0, 0, 0, verticalBlend);
			} else if(value == "Bisqwit 4x") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::BisqwitNtscHalfRes);
				EmulationSettings::SetNtscFilterSettings(0, 0, 0, 0, 0, 0, false, 0, 0, 0, verticalBlend);
			} else if(value == "Bisqwit 8x") {
				EmulationSettings::SetVideoFilterType(VideoFilterType::BisqwitNtsc);
				EmulationSettings::SetNtscFilterSettings(0, 0, 0, 0, 0, 0, false, 0, 0, 0, verticalBlend);
			}
		}

		var.key = MesenPalette;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "Default") {
				EmulationSettings::SetRgbPalette(defaultPalette);
			} else if(value == "Composite Direct (by FirebrandX)") {
				EmulationSettings::SetRgbPalette(compositeDirectPalette);
			} else if(value == "Nes Classic") {
				EmulationSettings::SetRgbPalette(nesClassicPalette);
			} else if(value == "Nestopia (RGB)") {
				EmulationSettings::SetRgbPalette(nestopiaRgbPalette);
			} else if(value == "Original Hardware (by FirebrandX)") {
				EmulationSettings::SetRgbPalette(originalHardwarePalette);
			} else if(value == "PVM Style (by FirebrandX)") {
				EmulationSettings::SetRgbPalette(pvmStylePalette);
			} else if(value == "Sony CXA2025AS") {
				EmulationSettings::SetRgbPalette(sonyCxa2025AsPalette);
			} else if(value == "Unsaturated v6 (by FirebrandX)") {
				EmulationSettings::SetRgbPalette(unsaturatedPalette);
			} else if(value == "YUV v3 (by FirebrandX)") {
				EmulationSettings::SetRgbPalette(yuvPalette);
			}
		}

		bool beforeNmi = true;
		var.key = MesenOverclockType;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "After NMI") {
				beforeNmi = false;
			}
		}

		var.key = MesenOverclock;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			int lineCount = 0;
			if(value == "None") {
				lineCount = 0;
			} else if(value == "Low") {
				lineCount = 100;
			} else if(value == "Medium") {
				lineCount = 250;
			} else if(value == "High") {
				lineCount = 500;
			} else if(value == "Very High") {
				lineCount = 1000;
			}

			if(beforeNmi) {
				EmulationSettings::SetPpuNmiConfig(lineCount, 0);
			} else {
				EmulationSettings::SetPpuNmiConfig(0, lineCount);
			}
		}

		int overscanHorizontal = 0;
		int overscanVertical = 0;		
		var.key = MesenOverscanHorizontal;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "8px") {
				overscanHorizontal = 8;
			} else if(value == "16px") {
				overscanHorizontal = 16;
			}
		}

		var.key = MesenOverscanVertical;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "8px") {
				overscanVertical = 8;
			} else if(value == "16px") {
				overscanVertical = 16;
			}
		}
		EmulationSettings::SetOverscanDimensions(overscanHorizontal, overscanHorizontal, overscanVertical, overscanVertical);

		var.key = MesenAspectRatio;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "Auto") {
				EmulationSettings::SetVideoAspectRatio(VideoAspectRatio::Auto, 1.0);
			} else if(value == "No Stretching") {
				EmulationSettings::SetVideoAspectRatio(VideoAspectRatio::NoStretching, 1.0);
			} else if(value == "NTSC") {
				EmulationSettings::SetVideoAspectRatio(VideoAspectRatio::NTSC, 1.0);
			} else if(value == "PAL") {
				EmulationSettings::SetVideoAspectRatio(VideoAspectRatio::PAL, 1.0);
			} else if(value == "4:3") {
				EmulationSettings::SetVideoAspectRatio(VideoAspectRatio::Standard, 1.0);
			} else if(value == "16:9") {
				EmulationSettings::SetVideoAspectRatio(VideoAspectRatio::Widescreen, 1.0);
			}
		}

		var.key = MesenRegion;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "Auto") {
				EmulationSettings::SetNesModel(NesModel::Auto);
			} else if(value == "NTSC") {
				EmulationSettings::SetNesModel(NesModel::NTSC);
			} else if(value == "PAL") {
				EmulationSettings::SetNesModel(NesModel::PAL);
			} else if(value == "Dendy") {
				EmulationSettings::SetNesModel(NesModel::Dendy);
			}
		}
		
		var.key = MesenRamState;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "All 0s (Default)") {
				EmulationSettings::SetRamPowerOnState(RamPowerOnState::AllZeros);
			} else if(value == "All 1s") {
				EmulationSettings::SetRamPowerOnState(RamPowerOnState::AllOnes);
			} else if(value == "Random Values") {
				EmulationSettings::SetRamPowerOnState(RamPowerOnState::Random);
			}
		}

		var.key = MesenScreenRotation;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "None") {
				EmulationSettings::SetScreenRotation(0);
			} else if(value == u8"90 degrees") {
				EmulationSettings::SetScreenRotation(90);
			} else if(value == u8"180 degrees") {
				EmulationSettings::SetScreenRotation(180);
			} else if(value == u8"270 degrees") {
				EmulationSettings::SetScreenRotation(270);
			}
		}

		int turboSpeed = 0;
		var.key = MesenControllerTurboSpeed;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
			string value = string(var.value);
			if(value == "Slow") {
				turboSpeed = 0;
			} else if(value == "Normal") {
				turboSpeed = 1;
			} else if(value == "Fast") {
				turboSpeed = 2;
			} else if(value == "Very Fast") {
				turboSpeed = 3;
			}
		}

		auto getKeyCode = [=](int port, int retroKey) {
			return (port << 8) | (retroKey + 1);
		};

		auto getKeyBindings = [=](int port) {
			KeyMappingSet keyMappings;
			keyMappings.TurboSpeed = turboSpeed;
			keyMappings.Mapping1.A = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_B);
			keyMappings.Mapping1.B = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_Y);
			keyMappings.Mapping1.TurboA = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_A);
			keyMappings.Mapping1.TurboB = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_X);
			keyMappings.Mapping1.Start = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_START);
			keyMappings.Mapping1.Select = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_SELECT);

			keyMappings.Mapping1.Up = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_UP);
			keyMappings.Mapping1.Down = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_DOWN);
			keyMappings.Mapping1.Left = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_LEFT);
			keyMappings.Mapping1.Right = getKeyCode(port, RETRO_DEVICE_ID_JOYPAD_RIGHT);

			if(port == 1) {
				keyMappings.Mapping1.Microphone = getKeyCode(0, RETRO_DEVICE_ID_JOYPAD_L3);
			}
			return keyMappings;
		};

		EmulationSettings::SetControllerKeys(0, getKeyBindings(0));
		EmulationSettings::SetControllerKeys(1, getKeyBindings(1));
		EmulationSettings::SetControllerKeys(2, getKeyBindings(2));
		EmulationSettings::SetControllerKeys(3, getKeyBindings(3));

		retro_system_av_info avInfo = {};
		setup_av_info(&avInfo);
		retroEnv(RETRO_ENVIRONMENT_SET_GEOMETRY, &avInfo);
	}

	RETRO_API void retro_run()
	{
		if(EmulationSettings::CheckFlag(EmulationFlags::ForceMaxSpeed)) {
			//Skip frames to speed up emulation while still outputting at 50/60 fps (needed for FDS fast forward while loading)
			_renderer->SetSkipMode(true);
			_soundManager->SetSkipMode(true);
			for(int i = 0; i < 9; i++) {
				//Attempt to speed up to 1000% speed
				Console::GetInstance()->RunSingleFrame();
			}
			_renderer->SetSkipMode(false);
			_soundManager->SetSkipMode(false);
		}

		Console::GetInstance()->RunSingleFrame();

		bool updated = false;
		if(retroEnv(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
			update_settings();

			bool hdPacksEnabled = EmulationSettings::CheckFlag(EmulationFlags::UseHdPacks);
			if(hdPacksEnabled != _hdPacksEnabled) {
				if(_hdPacksEnabled != hdPacksEnabled) {
					//Try to load/unload HD pack when the flag is toggled
					if(Console::GetInstance()->UpdateHdPackMode()) {
						update_settings();
					}
				}
				_hdPacksEnabled = hdPacksEnabled;
			}
		}
	}

	RETRO_API size_t retro_serialize_size()
	{
		std::stringstream ss;
		Console::SaveState(ss);
		return ss.str().size() * 2;
	}

	RETRO_API bool retro_serialize(void *data, size_t size)
	{
		std::stringstream ss;
		Console::SaveState(ss);
		
		string saveStateData = ss.str();
		memcpy(data, saveStateData.c_str(), saveStateData.size());

		return true;
	}

	RETRO_API bool retro_unserialize(const void *data, size_t size)
	{
		Console::LoadState((uint8_t*)data, (uint32_t)size);
		return true;
	}

	RETRO_API void retro_cheat_reset()
	{
		CheatManager::GetInstance()->ClearCodes();
	}

	RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *codeStr)
	{
		static const string validGgLetters = "APZLGITYEOXUKSVN";
		static const string validParLetters = "0123456789ABCDEF";

		string code = codeStr;
		std::transform(code.begin(), code.end(), code.begin(), ::toupper);
		if(code.size() == 7 && code[4] == ':') {
			string address = code.substr(0, 4);
			string value = code.substr(5, 2);
			CheatManager::GetInstance()->AddCustomCode(HexUtilities::FromHex(address), HexUtilities::FromHex(value));
		} else if(code.size() == 10 && code[4] == '?' && code[7] == ':') {
			string address = code.substr(0, 4);
			string comparison = code.substr(5, 2);
			string value = code.substr(8, 2);
			CheatManager::GetInstance()->AddCustomCode(HexUtilities::FromHex(address), HexUtilities::FromHex(value), HexUtilities::FromHex(comparison));
		} else if(code.size() == 6 || code.size() == 8) {
			//This is either a GG or PAR code
			bool isValidGgCode = true;
			bool isValidParCode = code.size() == 8;
			for(size_t i = 0; i < code.size(); i++) {
				if(validGgLetters.find(code[i]) == string::npos) {
					isValidGgCode = false;
				}
				if(validParLetters.find(code[i]) == string::npos) {
					isValidParCode = false;
				}
			}

			if(isValidGgCode) {
				CheatManager::GetInstance()->AddGameGenieCode(code);
			} else if(isValidParCode) {
				CheatManager::GetInstance()->AddProActionRockyCode(HexUtilities::FromHex(code));
			}
		}

	}

	RETRO_API bool retro_load_game(const struct retro_game_info *game)
	{
		char *systemFolder;
		if(!retroEnv(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemFolder) || !systemFolder) {
			return false;
		}

		char *saveFolder;
		if(!retroEnv(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &saveFolder)) {
			logMessage(RETRO_LOG_ERROR, "Could not find save directory.\n");
		}

		enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
		if(!retroEnv(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
			logMessage(RETRO_LOG_ERROR, "XRGB8888 is not supported.\n");
			return false;
		}

		//Expect the following structure:
		// /system/disksys.rom
		// /system/HdPacks/*
		// /saves/*.sav
		FolderUtilities::SetHomeFolder(systemFolder);
		FolderUtilities::SetFolderOverrides(saveFolder, "", "");

		update_settings();

		//Plug in 2 standard controllers by default, game database will switch the controller types for recognized games
		EmulationSettings::SetMasterVolume(10.0);
		EmulationSettings::SetControllerType(0, ControllerType::StandardController);
		EmulationSettings::SetControllerType(1, ControllerType::StandardController);
		EmulationSettings::SetControllerType(2, ControllerType::None);
		EmulationSettings::SetControllerType(3, ControllerType::None);
		bool result = Console::LoadROM(string(game->path));

		if(result) {
			vector<retro_input_descriptor> desc;

			auto addDesc = [&desc](unsigned port, unsigned button, const char* name) {
				retro_input_descriptor d = { port, RETRO_DEVICE_JOYPAD, 0, button, name };
				desc.push_back(d);
			};

			auto setupPlayerButtons = [addDesc](int port) {
				if(EmulationSettings::GetControllerType(port) != ControllerType::StandardController) {
					return;
				}

				addDesc(port, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_B, "A");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_A, "Turbo A");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_X, "Turbo B");
				addDesc(port, RETRO_DEVICE_ID_JOYPAD_Y, "B");
				if(port == 0) {
					addDesc(port, RETRO_DEVICE_ID_JOYPAD_L, "(FDS) Insert Next Disk");
					addDesc(port, RETRO_DEVICE_ID_JOYPAD_R, "(FDS) Switch Disk Side");
					addDesc(port, RETRO_DEVICE_ID_JOYPAD_L2, "(VS) Insert Coin 1");
					addDesc(port, RETRO_DEVICE_ID_JOYPAD_R2, "(VS) Insert Coin 2");
					addDesc(port, RETRO_DEVICE_ID_JOYPAD_L3, "(Famicom) Microphone");
				}
			};
			retro_input_descriptor end = { 0 };
			desc.push_back(end);

			setupPlayerButtons(0);
			setupPlayerButtons(1);
			setupPlayerButtons(2);
			setupPlayerButtons(3);

			retroEnv(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc.data());
		}

		return result;
	}

	RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
	{
		return false;
	}

	RETRO_API void retro_unload_game()
	{
		Console::GetInstance()->Stop();
	}

	RETRO_API unsigned retro_get_region()
	{
		NesModel model = Console::GetModel();
		return model == NesModel::NTSC ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
	}

	RETRO_API void retro_get_system_info(struct retro_system_info *info)
	{
		_mesenVersion = EmulationSettings::GetMesenVersionString();

		info->library_name = "Mesen";
		info->library_version = _mesenVersion.c_str();
		info->need_fullpath = true;
		info->valid_extensions = "nes|fds|unf|unif";
		info->block_extract = false;
	}

	RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
	{
		setup_av_info(info);		
	}

	RETRO_API void *retro_get_memory_data(unsigned id)
	{
		uint32_t size;
		switch(id) {
			case RETRO_MEMORY_SAVE_RAM: return Console::GetInstance()->GetRamBuffer(DebugMemoryType::SaveRam, size);
			case RETRO_MEMORY_SYSTEM_RAM: return Console::GetInstance()->GetRamBuffer(DebugMemoryType::InternalRam, size);
		}
		return nullptr;
	}

	RETRO_API size_t retro_get_memory_size(unsigned id)
	{
		uint32_t size = 0;
		switch(id) {
			case RETRO_MEMORY_SAVE_RAM: Console::GetInstance()->GetRamBuffer(DebugMemoryType::SaveRam, size); break;
			case RETRO_MEMORY_SYSTEM_RAM: Console::GetInstance()->GetRamBuffer(DebugMemoryType::InternalRam, size); break;
		}
		return size;
	}
}