#include "global.h"
/*
-----------------------------------------------------------------------------
 File: StepMania.cpp

 Desc: Entry point for program.

 Copyright (c) 2001-2002 by the person(s) listed below.  All rights reserved.
	Chris Danford
-----------------------------------------------------------------------------
*/

#include "StepMania.h"

//
// Rage global classes
//
#include "RageLog.h"
#include "RageTextureManager.h"
#include "RageSoundManager.h"
#include "RageSounds.h"
#include "RageInput.h"
#include "RageTimer.h"
#include "RageException.h"
#include "RageMath.h"
#include "RageDisplay.h"

#include "arch/arch.h"
#include "arch/LoadingWindow/LoadingWindow.h"
#include "time.h"

#include "SDL_utils.h"

//
// StepMania global classes
//
#include "ThemeManager.h"
#include "NoteSkinManager.h"
#include "PrefsManager.h"
#include "SongManager.h"
#include "GameState.h"
#include "AnnouncerManager.h"
#include "ProfileManager.h"
#include "ScreenManager.h"
#include "GameManager.h"
#include "FontManager.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "InputQueue.h"
#include "SongCacheIndex.h"
#include "BannerCache.h"
#include "UnlockSystem.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "RageFile.h"


#if defined(_XBOX)
	#ifdef DEBUG
	#pragma comment(lib, "SDL-1.2.5/lib/xboxSDLmaind.lib")
	#else
	#pragma comment(lib, "SDL-1.2.5/lib/xboxSDLmain.lib")
	#endif	
#elif defined(_WINDOWS)
	#ifdef DEBUG
	#pragma comment(lib, "SDL-1.2.5/lib/SDLmaind.lib")
	#else
	#pragma comment(lib, "SDL-1.2.5/lib/SDLmain.lib")
	#endif	
#endif

#ifdef _WINDOWS
HWND g_hWndMain = NULL;
#endif

int g_argc = 0;
char **g_argv = NULL;

static bool g_bHasFocus = true;
static bool g_bQuitting = false;

CString DirOfExecutable;

static void ChangeToDirOfExecutable(const char *argv0)
{
	DirOfExecutable = argv0;
	// strip off executable name
	unsigned n = DirOfExecutable.find_last_of("/\\");
	if( n != DirOfExecutable.npos )
		DirOfExecutable.erase(n);
	else
		DirOfExecutable.erase();

	bool IsAbsolutePath = false;
	if( DirOfExecutable.size() == 0 || (DirOfExecutable[0] == '/' || DirOfExecutable[0] == '\\') )
		IsAbsolutePath = true;
#if defined(_WIN32)
	if( DirOfExecutable.size() > 2 && DirOfExecutable[1] == ':' && 
		(DirOfExecutable[2] == '/' || DirOfExecutable[2] == '\\') )
		IsAbsolutePath = true;
#endif

	if( !IsAbsolutePath )
		DirOfExecutable = GetCwd() + "/" + DirOfExecutable;

#ifndef _XBOX
	/* Make sure the current directory is the root program directory
	 * We probably shouldn't do this; rather, we should know where things
	 * are and use paths as needed, so we don't depend on the binary being
	 * in the same place as "Songs" ... */
	if( !DoesFileExist("Songs") )
	{
		chdir( DirOfExecutable );
		FlushDirCache();
	}
#endif
}

static RageDisplay::VideoModeParams GetCurVideoModeParams()
{
	return RageDisplay::VideoModeParams(
			PREFSMAN->m_bWindowed,
			PREFSMAN->m_iDisplayWidth,
			PREFSMAN->m_iDisplayHeight,
			PREFSMAN->m_iDisplayColorDepth,
			PREFSMAN->m_iRefreshRate,
			PREFSMAN->m_bVsync,
			PREFSMAN->m_bInterlaced,
			PREFSMAN->m_bAntiAliasing,
			THEME->GetMetric("Common","WindowTitle"),
			THEME->GetPathToG("Common window icon")
#ifdef _XBOX
			, PREFSMAN->m_bPAL
#endif
	);
}

static void StoreActualGraphicOptions( bool initial )
{
	// find out what we actually have
	PREFSMAN->m_bWindowed = DISPLAY->GetVideoModeParams().windowed;
	PREFSMAN->m_iDisplayWidth = DISPLAY->GetVideoModeParams().width;
	PREFSMAN->m_iDisplayHeight = DISPLAY->GetVideoModeParams().height;
	PREFSMAN->m_iDisplayColorDepth = DISPLAY->GetVideoModeParams().bpp;
	PREFSMAN->m_iRefreshRate = DISPLAY->GetVideoModeParams().rate;
	PREFSMAN->m_bVsync = DISPLAY->GetVideoModeParams().vsync;

	CString log = ssprintf("%s %dx%d %d color %d texture %dHz %s %s",
		PREFSMAN->m_bWindowed ? "Windowed" : "Fullscreen",
		PREFSMAN->m_iDisplayWidth, 
		PREFSMAN->m_iDisplayHeight, 
		PREFSMAN->m_iDisplayColorDepth, 
		PREFSMAN->m_iTextureColorDepth, 
		PREFSMAN->m_iRefreshRate,
		PREFSMAN->m_bVsync ? "Vsync" : "NoVsync",
		PREFSMAN->m_bAntiAliasing? "AA" : "NoAA" );
	if( initial )
		LOG->Info( "%s", log.c_str() );
	else
		SCREENMAN->SystemMessage( log );
}

void ApplyGraphicOptions()
{ 
	bool bNeedReload = false;

	bNeedReload |= DISPLAY->SetVideoMode( GetCurVideoModeParams() );

	bNeedReload |= TEXTUREMAN->SetPrefs( 
		PREFSMAN->m_iTextureColorDepth, 
		PREFSMAN->m_iMovieColorDepth,
		PREFSMAN->m_bDelayedTextureDelete, 
		PREFSMAN->m_iMaxTextureResolution );

	if( bNeedReload )
		TEXTUREMAN->ReloadAll();

	StoreActualGraphicOptions( false );

	/* Give the input handlers a chance to re-open devices as necessary. */
	INPUTMAN->WindowReset();
}

void ExitGame()
{
	g_bQuitting = true;
}

void ResetGame( bool ReturnToFirstScreen )
{
	GAMESTATE->Reset();
	ReadGamePrefsFromDisk();
	INPUTMAPPER->ReadMappingsFromDisk();

	NOTESKIN->RefreshNoteSkinData( GAMESTATE->m_CurGame );

	/*
	GameState::Reset() will switch the NoteSkin
	for( int p=0; p<NUM_PLAYERS; p++ )
	{
		PlayerNumber pn = (PlayerNumber)p;
		if( !NOTESKIN->DoesNoteSkinExist( NOTESKIN->GetCurNoteSkinName(pn) ) )
		{
			CStringArray asNoteSkinNames;
			NOTESKIN->GetNoteSkinNames( asNoteSkinNames );
			NOTESKIN->SwitchNoteSkin( pn, asNoteSkinNames[0] );
		}
	}
	*/
	if( !THEME->DoesThemeExist( THEME->GetCurThemeName() ) )
	{
		CString sGameName = GAMESTATE->GetCurrentGameDef()->m_szName;
		if( THEME->DoesThemeExist( sGameName ) )
			THEME->SwitchThemeAndLanguage( sGameName, THEME->GetCurLanguage() );
		else
			THEME->SwitchThemeAndLanguage( "default", THEME->GetCurLanguage() );
		TEXTUREMAN->DoDelayedDelete();
	}
	SaveGamePrefsToDisk();

	PREFSMAN->m_bFirstRun = false;


	//
	// update last seen joysticks
	//
	vector<InputDevice> vDevices;
	vector<CString> vDescriptions;
	INPUTMAN->GetDevicesAndDescriptions(vDevices,vDescriptions);

	vector<CString> vOldDescriptions;
	split( PREFSMAN->m_sLastSeenInputDevices, ",", vOldDescriptions );
	CString sInputDevices = join( ",", vDescriptions );

	if( PREFSMAN->m_sLastSeenInputDevices != sInputDevices )
	{
		LOG->Info( "Input devices changed from '%s' to '%s'.", PREFSMAN->m_sLastSeenInputDevices.c_str(), sInputDevices.c_str() );

		if( PREFSMAN->m_bAutoMapOnJoyChange )
		{
			LOG->Info( "Remapping joysticks." );
			INPUTMAPPER->AutoMapJoysticksForCurrentGame();
		}

		PREFSMAN->m_sLastSeenInputDevices = sInputDevices;
	}


	if( ReturnToFirstScreen )
		SCREENMAN->SetNewScreen( THEME->GetMetric("Common","InitialScreen") );
}

static void GameLoop();

static bool ChangeAppPri()
{
	if(PREFSMAN->m_iBoostAppPriority == 0)
		return false;

	/* If -1 and this is a debug build, don't.  It makes the debugger sluggish. */
#ifdef DEBUG
	if(PREFSMAN->m_iBoostAppPriority == -1)
		return false;
#endif

	return true;
}

static void BoostAppPri()
{
	if(!ChangeAppPri())
		return;

#ifdef _WINDOWS
	/* We just want a slight boost, so we don't skip needlessly if something happens
	 * in the background.  We don't really want to be high-priority--above normal should
	 * be enough.  However, ABOVE_NORMAL_PRIORITY_CLASS is only supported in Win2000
	 * and later. */
	OSVERSIONINFO version;
	version.dwOSVersionInfoSize=sizeof(version);
	if(!GetVersionEx(&version))
	{
		LOG->Warn(werr_ssprintf(GetLastError(), "GetVersionEx failed"));
		return;
	}

#ifndef ABOVE_NORMAL_PRIORITY_CLASS
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000
#endif

	DWORD pri = HIGH_PRIORITY_CLASS;
	if(version.dwMajorVersion >= 5)
		pri = ABOVE_NORMAL_PRIORITY_CLASS;

	/* Be sure to boost the app, not the thread, to make sure the
	 * sound thread stays higher priority than the main thread. */
	SetPriorityClass(GetCurrentProcess(), pri);
#endif
}

static void CheckSettings()
{
#if defined(WIN32)
	/* Has the amount of memory changed? */
	MEMORYSTATUS mem;
	GlobalMemoryStatus(&mem);

	const int Memory = mem.dwTotalPhys / 1048576;

	if( PREFSMAN->m_iLastSeenMemory == Memory )
		return;
	
	LOG->Trace( "Memory changed from %i to %i; settings changed", PREFSMAN->m_iLastSeenMemory, Memory );
	PREFSMAN->m_iLastSeenMemory = Memory;

	/* Let's consider 128-meg systems low-memory, and 256-meg systems high-memory.
	 * Cut off at 192.  This is somewhat conservative; many 128-meg systems can
	 * deal with higher memory profile settings, but some can't. 
	 *
	 * Actually, Windows lops off a meg or two; cut off a little lower to treat
	 * 192-meg systems as high-memory. */
	const bool HighMemory = (Memory >= 190);
	const bool LowMemory = (Memory < 100); /* 64 and 96-meg systems */

	/* Two memory-consuming features that we can disable are texture caching and
	 * preloaded banners.  Texture caching can use a lot of memory; disable it for
	 * low-memory systems. */
	PREFSMAN->m_bDelayedTextureDelete = HighMemory;

	/* Preloaded banners takes about 9k per song. Although it's smaller than the
	 * actual song data, it still adds up with a lot of songs. Disable it for 64-meg
	 * systems. */
	PREFSMAN->m_bBannerCache = !LowMemory;

	PREFSMAN->SaveGlobalPrefsToDisk();
#endif
}

#if defined(WIN32)
#include "RageDisplay_D3D.h"
#endif

#if !defined(_XBOX)
#include "RageDisplay_OGL.h"
#endif

#include "archutils/Win32/VideoDriverInfo.h"
#include "regex.h"

static const CString D3DURL = "http://search.microsoft.com/gomsuri.asp?n=1&c=rp_BestBets&siteid=us&target=http://www.microsoft.com/downloads/details.aspx?FamilyID=a19bed22-0b25-4e5d-a584-6389d8a3dad0&displaylang=en";


struct VideoCardDefaults
{
	char szDriverRegex[200];
	char szVideoRenderers[200];
	int iWidth;
	int iHeight;
	int iDisplayColor;
	int iTextureColor;
	bool bAntiAliasing;
} const g_VideoCardDefaults[] = 
{
	{
		"Voodoo3|3dfx",
		"d3d,opengl",
		640,
		480,
		16,
		16,
		0	// broken, causes black screen
	},
	{
		"GeForce|Radeon",
		"opengl,d3d",
		640,
		480,
		32,
		32,	// 32 bit textures are faster to load
		1	// hardware accelerated
	},
	{
		"TNT|Vanta|M64",
		"opengl,d3d",
		640,
		480,
		16,	// ease out on the fill rate a bit
		16,	// Athlon 1.2+TNT demonstration w/ movies: 70fps w/ 32bit textures, 86fps w/ 16bit textures
		1	// hardware accelerated
	},
	{
		"G200|G250|G400",
		"d3d,opengl",
		640,
		480,
		16,
		16,
		0	// broken, causes black screen
	},
	{
		"Savage",
		"d3d",
			// OpenGL is unusable on my Savage IV with even the latest drivers.  
			// It draws 30 frames of gibberish then crashes.  This happens even with
			// simple NeHe demos.  -Chris
		640,
		480,
		16,
		16,
		false
	},
	{
		"XPERT@PLAY|IIC|RAGE PRO|RAGE LT PRO",	// Rage Pro chip, Rage IIC chip
		"d3d",
			// OpenGL is not hardware accelerated, despite the fact that the 
			// drivers come with an ICD.  Also, the WinXP driver performance 
			// is terrible and supports only 640.  The ATI driver is usable.
			// -Chris
		400,	// lower resolution for 60fps
		300,
		16,
		16,
		0
	},
	{
		"RAGE MOBILITY-M1",
		"d3d,opengl",	// Vertex alpha is broken in OpenGL, but not D3D. -Chris
		400,	// lower resolution for 60fps
		300,
		16,
		16,
		0
	},
	{
		"Intel.*82810|Intel.*82815",
		"opengl,d3d",// OpenGL is 50%+ faster than D3D w/ latest Intel drivers.  -Chris

		512,	// lower resolution for 60fps
		384,
		16,
		16,
		0
	},
	{
		// Cards that have problems with OpenGL:
		// ASSERT fail somewhere in RageDisplay_OpenGL "Trident Video Accelerator CyberBlade"
		// bug 764499: ASSERT fail after glDeleteTextures for "SiS 650_651_740"
		// bug 764830: ASSERT fail after glDeleteTextures for "VIA Tech VT8361/VT8601 Graphics Controller"
		// bug 791950: AV in glsis630!DrvSwapBuffers for "SiS 630/730"
		"Trident Video Accelerator CyberBlade|VIA.*VT|SiS 6*",
		"d3d,opengl",
		640,
		480,
		16,
		16,
		0
	},
	{
		"Voodoo3|3dfx",
		"d3d,opengl",
		640,
		480,
		16,
		16,
		0
	},
	{
		"OpenGL",	// This matches all drivers in Mac and Linux. -Chris
		"opengl",
		640,
		480,
		16,
		16,
		1		// Right now, they've got to have NVidia or ATi Cards anyway..
	},
	{
		// Default graphics settings used for all cards that don't match above.
		// This must be the very last entry!
		"",
		"opengl,d3d",
		640,
		480,
		16,
		16,
		0
		// AA is slow on some cards, so let's selectively enable it on cards we know are hardware
		// accelerated.  Enabling AA on a G400 slows screenSelectMusic from 45fps to 35fps.
	},
};


static CString GetVideoDriverName()
{
#if defined(_WINDOWS)
	return GetPrimaryVideoDriverName();
#else
    return "OpenGL";
#endif
}

static void CheckVideoDefaultSettings()
{
	// Video card changed since last run
	CString sVideoDriver = GetVideoDriverName();
	
	LOG->Trace( "Last seen video driver: " + PREFSMAN->m_sLastSeenVideoDriver );

	const VideoCardDefaults* pDefaults = NULL;
	
	for( int i=0; i<ARRAYSIZE(g_VideoCardDefaults); i++ )
	{
		pDefaults = &g_VideoCardDefaults[i];

		CString sDriverRegex = pDefaults->szDriverRegex;
		Regex regex( sDriverRegex );
		if( regex.Compare(sVideoDriver) )
		{
			LOG->Trace( "Card matches '%s'.", sDriverRegex.size()? sDriverRegex.c_str():"(unknown card)" );
			break;
		}
	}

	ASSERT( pDefaults );	// we must have matched at least one

	CString sVideoRenderers = pDefaults->szVideoRenderers;

	bool SetDefaultVideoParams=false;
	if( PREFSMAN->m_sVideoRenderers == "" )
	{
		SetDefaultVideoParams = true;
		LOG->Trace( "Applying defaults for %s.", sVideoDriver.c_str() );
	}
	else if( PREFSMAN->m_sLastSeenVideoDriver != sVideoDriver ) 
	{
		SetDefaultVideoParams = true;
		LOG->Trace( "Video card has changed from %s to %s.  Applying new defaults.", PREFSMAN->m_sLastSeenVideoDriver.c_str(), sVideoDriver.c_str() );
	}
		
	if( SetDefaultVideoParams )
	{
		PREFSMAN->m_sVideoRenderers = pDefaults->szVideoRenderers;
		PREFSMAN->m_iDisplayWidth = pDefaults->iWidth;
		PREFSMAN->m_iDisplayHeight = pDefaults->iHeight;
		PREFSMAN->m_iDisplayColorDepth = pDefaults->iDisplayColor;
		PREFSMAN->m_iTextureColorDepth = pDefaults->iTextureColor;
		PREFSMAN->m_iMovieColorDepth = pDefaults->iTextureColor;
		PREFSMAN->m_bAntiAliasing = pDefaults->bAntiAliasing;

		// Update last seen video card
		PREFSMAN->m_sLastSeenVideoDriver = GetVideoDriverName();
	}
	else if( PREFSMAN->m_sVideoRenderers.CompareNoCase(sVideoRenderers) )
	{
		LOG->Warn("Video renderer list has been changed from '%s' to '%s'",
				sVideoRenderers.c_str(), PREFSMAN->m_sVideoRenderers.c_str() );
	}
}

RageDisplay *CreateDisplay()
{
	/* We never want to bother users with having to decide which API to use.
	 *
	 * Some cards simply are too troublesome with OpenGL to ever use it, eg. Voodoos.
	 * If D3D8 isn't installed on those, complain and refuse to run (by default).
	 * For others, always use OpenGL.  Allow forcing to D3D as an advanced option.
	 *
	 * If we're missing acceleration when we load D3D8 due to a card being in the
	 * D3D list, it means we need drivers and that they do exist.
	 *
	 * If we try to load OpenGL and we're missing acceleration, it may mean:
	 *  1. We're missing drivers, and they just need upgrading.
	 *  2. The card doesn't have drivers, and it should be using D3D8.  In other words,
	 *     it needs an entry in this table.
	 *  3. The card doesn't have drivers for either.  (Sorry, no S3 868s.)  Can't play.
	 * 
	 * In this case, fail to load; don't silently fall back on D3D.  We don't want
	 * people unknowingly using D3D8 with old drivers (and reporting obscure bugs
	 * due to driver problems).  We'll probably get bug reports for all three types.
	 * #2 is the only case that's actually a bug.
	 *
	 * Actually, right now we're falling back.  I'm not sure which behavior is better.
	 */

	CheckVideoDefaultSettings();

	RageDisplay::VideoModeParams params(GetCurVideoModeParams());

	CString error = "There was an error while initializing your video card.\n\n"
		"   PLEASE DO NOT FILE THIS ERROR AS A BUG!\n\n"
		"Video Driver: "+GetVideoDriverName()+"\n\n";

	LOG->Info( "Video renderers: '%s'", PREFSMAN->m_sVideoRenderers.c_str() );

	CStringArray asRenderers;
	split( PREFSMAN->m_sVideoRenderers, ",", asRenderers, true );

	if( asRenderers.empty() )
		RageException::Throw("No video renderers attempted.");

	for( unsigned i=0; i<asRenderers.size(); i++ )
	{
		CString sRenderer = asRenderers[i];

		if( sRenderer.CompareNoCase("opengl")==0 )
		{
#if defined(SUPPORT_OPENGL)
			error += "Initializing OpenGL...\n";
			try {
				return new RageDisplay_OGL( params, PREFSMAN->m_bAllowUnacceleratedRenderer );
			} catch(RageException e) {
				error += CString(e.what()) + "\n";
				continue;
			};
#endif
		}
		else if( sRenderer.CompareNoCase("d3d")==0 )
		{
#if defined(SUPPORT_D3D)
			error += "Initializing Direct3D...\n";
			try {
				return new RageDisplay_D3D( params );
			} catch( const exception &e ) {
				error += CString(e.what()) + "\n";
			};
#endif
		}
		else
			RageException::Throw("Unknown video renderer value: %s", sRenderer.c_str() );
	}

	RageException::Throw( error );
}

static void CheckSDLVersion( int major, int minor, int patch )
{
	const SDL_version *ver = SDL_Linked_Version();
	if( ver->major > major ||
	   (ver->major == major && ver->minor > minor) ||
	   (ver->major == major && ver->minor == minor && ver->patch >= patch))
		return;

	RageException::Throw( "SDL %i.%i.%i is required, but you only appear to "
		"have SDL %i.%i.%i installed.  Please upgrade your installation of SDL or download "
		"it from:\n\n\thttp://www.libsdl.org/",
		major, minor, patch, ver->major, ver->minor, ver->patch );
}

static void RestoreAppPri()
{
	if(!ChangeAppPri())
		return;

#ifdef _WINDOWS
	SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
#endif
}


#define GAMEPREFS_INI_PATH BASE_PATH "Data" SLASH "GamePrefs.ini"

void ChangeCurrentGame( Game g )
{
	SaveGamePrefsToDisk();
	INPUTMAPPER->SaveMappingsToDisk();	// save mappings before switching the game

	GAMESTATE->m_CurGame = g;

	ReadGamePrefsFromDisk( false );
	INPUTMAPPER->ReadMappingsFromDisk();

	/* Save the newly-selected game. */
	SaveGamePrefsToDisk();
}

void ReadGamePrefsFromDisk( bool bSwitchToLastPlayedGame )
{
	if( !GAMESTATE )
		return;

	CString sGameName = GAMESTATE->GetCurrentGameDef()->m_szName;
	IniFile ini;
	ini.SetPath( GAMEPREFS_INI_PATH );
	ini.ReadFile();	// it's OK if this fails

	CString sAnnouncer = sGameName, sTheme = sGameName, sNoteSkin = sGameName;

	// if these calls fail, the three strings will keep the initial values set above.
	ini.GetValue( sGameName, "Announcer",			sAnnouncer );
	ini.GetValue( sGameName, "Theme",				sTheme );
	ini.GetValue( sGameName, "DefaultModifiers",	PREFSMAN->m_sDefaultModifiers );

	// it's OK to call these functions with names that don't exist.
	ANNOUNCER->SwitchAnnouncer( sAnnouncer );
	THEME->SwitchThemeAndLanguage( sTheme, PREFSMAN->m_sLanguage );

//	NOTESKIN->SwitchNoteSkin( sNoteSkin );

	if( bSwitchToLastPlayedGame )
	{
		Game game;
		if( ini.GetValue("Options", "Game", (int&)game) )
			GAMESTATE->m_CurGame = game;
	}
}


void SaveGamePrefsToDisk()
{
	if( !GAMESTATE )
		return;

	CString sGameName = GAMESTATE->GetCurrentGameDef()->m_szName;
	IniFile ini;
	ini.SetPath( GAMEPREFS_INI_PATH );
	ini.ReadFile();	// it's OK if this fails

	ini.SetValue( sGameName, "Announcer",			ANNOUNCER->GetCurAnnouncerName() );
	ini.SetValue( sGameName, "Theme",				THEME->GetCurThemeName() );
	ini.SetValue( sGameName, "DefaultModifiers",	PREFSMAN->m_sDefaultModifiers );
	ini.SetValue( "Options", "Game",				GAMESTATE->m_CurGame );

	ini.WriteFile();
}



#define UNLOCKS_PATH BASE_PATH "Data" SLASH "Unlocks.dat"


int main(int argc, char* argv[])
{
	g_argc = argc;
	g_argv = argv;

	/* Set up arch hooks first.  This may set up crash handling. */
	HOOKS = MakeArchHooks();

	CString  g_sErrorString = "";

#ifndef DEBUG
	try{
#endif

	ChangeToDirOfExecutable(argv[0]);

	/* Set this up second.  Do this early, since it's needed for RageException::Throw. 
	 * Do it after ChangeToDirOfExecutable, so the log ends up in the right place. */
	LOG			= new RageLog();

	/* Whew--we should be able to crash safely now! */

	atexit(SDL_Quit);   /* Clean up on exit */

	/* Fire up the SDL, but don't actually start any subsystems.
	 * We use our own error handlers. */
	SDL_Init( SDL_INIT_NOPARACHUTE );

	LoadingWindow *loading_window = MakeLoadingWindow();
	if( loading_window == NULL )
	{
		LOG->Trace("Couldn't open any loading windows.\n");
		exit(1);
	}

	loading_window->Paint();

	srand( time(NULL) );	// seed number generator	
	
	//
	// Create game objects
	//
	GAMESTATE	= new GameState;
	PREFSMAN	= new PrefsManager;

	if( PREFSMAN->m_bShowLogWindow )
		LOG->ShowConsole();

	CheckSDLVersion( 1,2,6 );
	
	/* This should be done after PREFSMAN is set up, so it can use HOOKS->MessageBoxOK,
	 * but before we do more complex things that might crash. */
	HOOKS->DumpDebugInfo();

	LOG->SetLogging( PREFSMAN->m_bLogging );
	LOG->SetFlushing( PREFSMAN->m_bForceLogFlush );
	LOG->SetTimestamping( PREFSMAN->m_bTimestamping );

	CheckSettings();

	GAMEMAN		= new GameManager;
	THEME		= new ThemeManager;
	NOTESKIN	= new NoteSkinManager;
	SOUNDMAN	= new RageSoundManager(PREFSMAN->m_sSoundDrivers);
	SOUNDMAN->SetPrefs(PREFSMAN->m_fSoundVolume);
	SOUND		= new RageSounds;
	ANNOUNCER	= new AnnouncerManager;
	PROFILEMAN	= new ProfileManager;
	INPUTFILTER	= new InputFilter;
	INPUTMAPPER	= new InputMapper;
	INPUTQUEUE	= new InputQueue;
	SONGINDEX	= new SongCacheIndex;
	BANNERCACHE = new BannerCache;
	
	/* depends on SONGINDEX: */
	SONGMAN		= new SongManager( loading_window );		// this takes a long time to load
	delete loading_window;		// destroy this before init'ing Display

	/* XXX: Why do we reload global prefs?  PREFSMAN loads them in the ctor. -glenn */
	PREFSMAN->ReadGlobalPrefsFromDisk( true );
	ReadGamePrefsFromDisk();

	DISPLAY = CreateDisplay();
	TEXTUREMAN	= new RageTextureManager();
	TEXTUREMAN->SetPrefs( 
		PREFSMAN->m_iTextureColorDepth, 
		PREFSMAN->m_iMovieColorDepth,
		PREFSMAN->m_bDelayedTextureDelete, 
		PREFSMAN->m_iMaxTextureResolution );

	StoreActualGraphicOptions( true );

	/* Now that we've started DISPLAY, we can set up event masks. */
	mySDL_EventState(SDL_QUIT, SDL_ENABLE);
	mySDL_EventState(SDL_ACTIVEEVENT, SDL_ENABLE);

	/* Grab the window manager specific information */
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if ( SDL_GetWMInfo(&info) < 0 ) 
		RageException::Throw( "SDL_GetWMInfo failed" );

#ifdef _WINDOWS
	g_hWndMain = info.window;
#endif

	/* This initializes objects that change the SDL event mask, and has other
	 * dependencies on the SDL video subsystem, so it must be initialized after
	 * DISPLAY and setting the default SDL event mask. */
	INPUTMAN	= new RageInput;

	// These things depend on the TextureManager, so do them after!
	FONT		= new FontManager;
	SCREENMAN	= new ScreenManager;

	/* People may want to do something else while songs are loading, so do
	 * this after loading songs. */
	BoostAppPri();

	ResetGame();

	/* Load the unlocks into memory */
	GAMESTATE->m_pUnlockingSys->LoadFromDATFile( UNLOCKS_PATH );

	/* Initialize which courses are ranking courses here. */
	SONGMAN->UpdateRankingCourses();

	/* Run the main loop. */
	GameLoop();

	PREFSMAN->SaveGlobalPrefsToDisk();
	SaveGamePrefsToDisk();

#ifndef DEBUG
	}
	catch( const RageException &e )
	{
		g_sErrorString = e.what();
	}
	catch( const exception &e )
	{
		if( LOG )
			LOG->Warn("Unhandled exception: \"%s\"", e.what() );

		/* Re-throw, so we get an unhandled exception crash and get a backtrace (at least
		 * in Windows). */
		throw;
	}
#endif

	SAFE_DELETE( SCREENMAN );
	/* Delete INPUTMAN before the other INPUTFILTER handlers, or an input
	 * driver may try to send a message to INPUTFILTER after we delete it. */
	SAFE_DELETE( INPUTMAN );
	SAFE_DELETE( INPUTQUEUE );
	SAFE_DELETE( INPUTMAPPER );
	SAFE_DELETE( INPUTFILTER );
	SAFE_DELETE( SONGMAN );
	SAFE_DELETE( BANNERCACHE );
	SAFE_DELETE( SONGINDEX );
	SAFE_DELETE( PREFSMAN );
	SAFE_DELETE( GAMESTATE );
	SAFE_DELETE( GAMEMAN );
	SAFE_DELETE( NOTESKIN );
	SAFE_DELETE( THEME );
	SAFE_DELETE( ANNOUNCER );
	SAFE_DELETE( PROFILEMAN );
	SAFE_DELETE( SOUND );
	SAFE_DELETE( SOUNDMAN );
	SAFE_DELETE( FONT );
	SAFE_DELETE( TEXTUREMAN );
	SAFE_DELETE( DISPLAY );
	SAFE_DELETE( LOG );
	
	if( g_sErrorString != "" )
		HOOKS->MessageBoxError( g_sErrorString ); // throw up a pretty error dialog

	SAFE_DELETE( HOOKS );

	return 0;
}

/* Returns true if the key has been handled and should be discarded, false if
 * the key should be sent on to screens. */
bool HandleGlobalInputs( DeviceInput DeviceI, InputEventType type, GameInput GameI, MenuInput MenuI, StyleInput StyleI )
{
	/* None of the globals keys act on types other than FIRST_PRESS */
	if( type != IET_FIRST_PRESS ) 
		return false;

	switch( MenuI.button )
	{
	case MENU_BUTTON_OPERATOR:

		/* Global operator key, to get quick access to the options menu. Don't
		 * do this if we're on a "system menu", which includes the editor
		 * (to prevent quitting without storing changes). */
		if( !GAMESTATE->m_bIsOnSystemMenu )
		{
			SCREENMAN->SystemMessage( "OPERATOR" );
			SCREENMAN->SetNewScreen( "ScreenOptionsMenu" );
		}
		return true;

	case MENU_BUTTON_COIN:
		/* Handle a coin insertion. */
		if( GAMESTATE->m_bEditing )	// no coins while editing
			break;
		GAMESTATE->m_iCoins++;
		SCREENMAN->RefreshCreditsMessages();
		SOUND->PlayOnce( THEME->GetPathToS("Common coin") );
		return false;	// Attract need to know because they go to TitleMenu on > 1 credit
	}

#ifndef DARWIN
	if(DeviceI == DeviceInput(DEVICE_KEYBOARD, SDLK_F4))
	{
		if( INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_RALT)) ||
			INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_LALT)) )
		{
			// pressed Alt+F4
			ExitGame();
			return true;
		}
	}
#else
	if(DeviceI == DeviceInput(DEVICE_KEYBOARD, SDLK_q))
	{
		if(INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, SDLK_RMETA)) ||
			INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, SDLK_LMETA)))
		{
			// pressed CMD-Q
			ExitGame();
			return true;
		}
	}
#endif

#ifndef DARWIN
	/* The default Windows message handler will capture the desktop window upon
	 * pressing PrntScrn, or will capture the foregroud with focus upon pressing
	 * Alt+PrntScrn.  Windows will do this whether or not we save a screenshot 
	 * ourself by dumping the frame buffer.  */
	// "if pressing PrintScreen and not pressing Alt"
	if( DeviceI == DeviceInput(DEVICE_KEYBOARD, SDLK_PRINT) &&
		!INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_RALT)) &&
		!INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_LALT)) )
#else
	/* Using F12 seems like a bad idea because this key already has a function
	 * in ScreenGameplay and in the editor (and those users don't care whether 
	 * Alt is being held.  Is there another key on the standard Mac keyboard 
	 * that could be used for this purpose?  -Chris */ 
	if( DeviceI == DeviceInput(DEVICE_KEYBOARD, SDLK_F12) &&
	    ( INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_RMETA)) ||
	      INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_LMETA))) )
#endif
	{
		// Save Screenshot.
		CString sPath;
		
		FlushDirCache();
		for( int i=0; i<10000; i++ )
		{
			sPath = ssprintf("screen%04d.bmp",i);
			if( !DoesFileExist(sPath) )
				break;
		}
		DISPLAY->SaveScreenshot( sPath );
		SCREENMAN->SystemMessage( "Saved screenshot: " + sPath );
		return true;
	}

	if(DeviceI == DeviceInput(DEVICE_KEYBOARD, SDLK_RETURN))
	{
		if( INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, SDLK_RALT)) ||
			INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, SDLK_LALT)) )
		{
			/* alt-enter */
			PREFSMAN->m_bWindowed = !PREFSMAN->m_bWindowed;
			ApplyGraphicOptions();
			return true;
		}
	}

	return false;
}

static void HandleInputEvents(float fDeltaTime)
{
	INPUTFILTER->Update( fDeltaTime );
	
	static InputEventArray ieArray;
	ieArray.clear();	// empty the array
	INPUTFILTER->GetInputEvents( ieArray );

	/* If we don't have focus, discard input. */
	if( !g_bHasFocus )
		return;

	for( unsigned i=0; i<ieArray.size(); i++ )
	{
		DeviceInput DeviceI = (DeviceInput)ieArray[i];
		InputEventType type = ieArray[i].type;
		GameInput GameI;
		MenuInput MenuI;
		StyleInput StyleI;

		INPUTMAPPER->DeviceToGame( DeviceI, GameI );
		
		if( GameI.IsValid()  &&  type == IET_FIRST_PRESS )
			INPUTQUEUE->RememberInput( GameI );
		if( GameI.IsValid() )
		{
			INPUTMAPPER->GameToMenu( GameI, MenuI );
			INPUTMAPPER->GameToStyle( GameI, StyleI );
		}

		// HACK:  Numlock is read is being pressed if the NumLock light is on.
		// Filter out all NumLock repeat messages
		if( DeviceI.device == DEVICE_KEYBOARD && DeviceI.button == SDLK_NUMLOCK && type != IET_FIRST_PRESS )
			continue;	// skip

		if( HandleGlobalInputs(DeviceI, type, GameI, MenuI, StyleI ) )
			continue;	// skip
		
		SCREENMAN->Input( DeviceI, type, GameI, MenuI, StyleI );
	}
}

static void HandleSDLEvents()
{
	// process all queued events
	SDL_Event event;
	while(SDL_GetEvent(event, SDL_QUITMASK|SDL_ACTIVEEVENTMASK))
	{
		switch(event.type)
		{
		case SDL_QUIT:
			LOG->Trace("SDL_QUIT: shutting down");
			ExitGame();
			break;

		case SDL_ACTIVEEVENT:
			{
				/* We don't care about mouse focus. */
				if(event.active.state == SDL_APPMOUSEFOCUS)
					break;

				Uint8 i = SDL_GetAppState();
				
				g_bHasFocus = i&SDL_APPINPUTFOCUS && i&SDL_APPACTIVE;
				LOG->Trace("App %s focus (%i%i)", g_bHasFocus? "has":"doesn't have",
					i&SDL_APPINPUTFOCUS, i&SDL_APPACTIVE);

				if(g_bHasFocus)
					BoostAppPri();
				else
				{
					RestoreAppPri();

					/* If we lose focus, we may lose input events, especially key
					 * releases. */
					INPUTFILTER->Reset();
				}
			}
		}
	}
}

static void GameLoop()
{
	RageTimer timer;
	while(!g_bQuitting)
	{
		/* This needs to be called before anything that handles SDL events. */
		SDL_PumpEvents();
		HandleSDLEvents();

		/*
		 * Update
		 */
		float fDeltaTime = timer.GetDeltaTime();
		
		if( INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_TAB) ) ) {
			if( INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_BACKQUOTE) ) )
				fDeltaTime = 0; /* both; stop time */
			else
				fDeltaTime *= 4;
		}
		else if( INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, SDLK_BACKQUOTE) ) )
		{
			fDeltaTime /= 4;
		}

		DISPLAY->Update( fDeltaTime );
		TEXTUREMAN->Update( fDeltaTime );
		GAMESTATE->Update( fDeltaTime );
		SCREENMAN->Update( fDeltaTime );
		SOUNDMAN->Update( fDeltaTime );

		/* Important:  Process input AFTER updating game logic, or input will be acting on song beat from last frame */
		HandleInputEvents( fDeltaTime );

		HOOKS->Update( fDeltaTime );

		/*
		 * Render
		 */
		SCREENMAN->Draw();

		/* If we don't have focus, give up lots of CPU. */
		if( !g_bHasFocus )
			SDL_Delay( 10 );// give some time to other processes and threads
#if defined(_WINDOWS)
		/* In Windows, we want to give up some CPU for other threads.  Most OS's do
		 * this more intelligently. */
		else
			SDL_Delay( 1 );	// give some time to other processes and threads
#endif
	}
}
