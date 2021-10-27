/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "qcommon/q_shared.h" // Include before SDL.h due to M_PI issue...
#include <SDL.h>

#ifdef USE_SMP
#include <SDL_thread.h>
#endif

#include "renderer/tr_local.h"

#include "sdl_icon.h"
#include "SDL_syswm.h"
#include "framework/CommandSystem.h"
#include "framework/CvarSystem.h"

static Log::Logger logger("glconfig", "", Log::Level::NOTICE);

SDL_Window *window = nullptr;
static SDL_GLContext glContext = nullptr;

#ifdef USE_SMP
static void GLimp_SetCurrentContext( bool enable )
{
	if ( enable )
	{
		SDL_GL_MakeCurrent( window, glContext );
	}
	else
	{
		SDL_GL_MakeCurrent( window, nullptr );
	}
}

/*
===========================================================

SMP acceleration

===========================================================
*/

/*
 * I have no idea if this will even work...most platforms don't offer
 * thread-safe OpenGL libraries.
 */

static SDL_mutex  *smpMutex = nullptr;
static SDL_cond   *renderCommandsEvent = nullptr;
static SDL_cond   *renderCompletedEvent = nullptr;
static void ( *renderThreadFunction )() = nullptr;
static SDL_Thread *renderThread = nullptr;

/*
===============
GLimp_RenderThreadWrapper
===============
*/
ALIGN_STACK_FOR_MINGW static int GLimp_RenderThreadWrapper( void* )
{
	// These printfs cause race conditions which mess up the console output
	logger.Notice( "Render thread starting\n" );

	renderThreadFunction();

	GLimp_SetCurrentContext( false );

	logger.Notice( "Render thread terminating\n" );

	return 0;
}

/*
===============
GLimp_SpawnRenderThread
===============
*/
bool GLimp_SpawnRenderThread( void ( *function )() )
{
	static bool warned = false;

	if ( !warned )
	{
		logger.Warn( "You enable r_smp at your own risk!\n" );
		warned = true;
	}

	if ( renderThread != nullptr ) /* hopefully just a zombie at this point... */
	{
		logger.Notice( "Already a render thread? Trying to clean it up...\n" );
		GLimp_ShutdownRenderThread();
	}

	smpMutex = SDL_CreateMutex();

	if ( smpMutex == nullptr )
	{
		logger.Notice( "smpMutex creation failed: %s\n", SDL_GetError() );
		GLimp_ShutdownRenderThread();
		return false;
	}

	renderCommandsEvent = SDL_CreateCond();

	if ( renderCommandsEvent == nullptr )
	{
		logger.Notice( "renderCommandsEvent creation failed: %s\n", SDL_GetError() );
		GLimp_ShutdownRenderThread();
		return false;
	}

	renderCompletedEvent = SDL_CreateCond();

	if ( renderCompletedEvent == nullptr )
	{
		logger.Notice( "renderCompletedEvent creation failed: %s\n", SDL_GetError() );
		GLimp_ShutdownRenderThread();
		return false;
	}

	renderThreadFunction = function;
	renderThread = SDL_CreateThread( GLimp_RenderThreadWrapper, "render thread", nullptr );

	if ( renderThread == nullptr )
	{
		logger.Notice("SDL_CreateThread() returned %s", SDL_GetError() );
		GLimp_ShutdownRenderThread();
		return false;
	}

	return true;
}

/*
===============
GLimp_ShutdownRenderThread
===============
*/
void GLimp_ShutdownRenderThread()
{
	if ( renderThread != nullptr )
	{
		GLimp_WakeRenderer( nullptr );
		SDL_WaitThread( renderThread, nullptr );
		renderThread = nullptr;
		glConfig.smpActive = false;
	}

	if ( smpMutex != nullptr )
	{
		SDL_DestroyMutex( smpMutex );
		smpMutex = nullptr;
	}

	if ( renderCommandsEvent != nullptr )
	{
		SDL_DestroyCond( renderCommandsEvent );
		renderCommandsEvent = nullptr;
	}

	if ( renderCompletedEvent != nullptr )
	{
		SDL_DestroyCond( renderCompletedEvent );
		renderCompletedEvent = nullptr;
	}

	renderThreadFunction = nullptr;
}

static volatile void     *smpData = nullptr;
static volatile bool smpDataReady;

/*
===============
GLimp_RendererSleep
===============
*/
void           *GLimp_RendererSleep()
{
	void *data = nullptr;

	GLimp_SetCurrentContext( false );

	SDL_LockMutex( smpMutex );
	{
		smpData = nullptr;
		smpDataReady = false;

		// after this, the front end can exit GLimp_FrontEndSleep
		SDL_CondSignal( renderCompletedEvent );

		while ( !smpDataReady )
		{
			SDL_CondWait( renderCommandsEvent, smpMutex );
		}

		data = ( void * ) smpData;
	}
	SDL_UnlockMutex( smpMutex );

	GLimp_SetCurrentContext( true );

	return data;
}

/*
===============
GLimp_FrontEndSleep
===============
*/
void GLimp_FrontEndSleep()
{
	SDL_LockMutex( smpMutex );
	{
		while ( smpData )
		{
			SDL_CondWait( renderCompletedEvent, smpMutex );
		}
	}
	SDL_UnlockMutex( smpMutex );
}

/*
===============
GLimp_SyncRenderThread
===============
*/
void GLimp_SyncRenderThread()
{
	GLimp_FrontEndSleep();

	GLimp_SetCurrentContext( true );
}

/*
===============
GLimp_WakeRenderer
===============
*/
void GLimp_WakeRenderer( void *data )
{
	GLimp_SetCurrentContext( false );

	SDL_LockMutex( smpMutex );
	{
		ASSERT(smpData == nullptr);
		smpData = data;
		smpDataReady = true;

		// after this, the renderer can continue through GLimp_RendererSleep
		SDL_CondSignal( renderCommandsEvent );
	}
	SDL_UnlockMutex( smpMutex );
}

#else

// No SMP - stubs
void GLimp_RenderThreadWrapper( void* )
{
}

bool GLimp_SpawnRenderThread( void ( * )() )
{
	logger.Warn("SMP support was disabled at compile time" );
	return false;
}

void GLimp_ShutdownRenderThread()
{
}

void *GLimp_RendererSleep()
{
	return nullptr;
}

void GLimp_FrontEndSleep()
{
}

void GLimp_SyncRenderThread()
{
}

void GLimp_WakeRenderer( void* )
{
}

#endif

enum class rserr_t
{
  RSERR_OK,

  RSERR_INVALID_FULLSCREEN,
  RSERR_INVALID_MODE,
  RSERR_MISSING_GL,
  RSERR_OLD_GL,

  RSERR_UNKNOWN
};

cvar_t                     *r_allowResize; // make window resizable
cvar_t                     *r_centerWindow;
cvar_t                     *r_displayIndex;
cvar_t                     *r_sdlDriver;

static void GLimp_DestroyContextIfExists();
static void GLimp_DestroyWindowIfExists();

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown()
{
	logger.Debug("Shutting down OpenGL subsystem" );

	ri.IN_Shutdown();

#if defined( USE_SMP )

	if ( renderThread != nullptr )
	{
		logger.Notice( "Destroying renderer thread...\n" );
		GLimp_ShutdownRenderThread();
	}

#endif

	GLimp_DestroyContextIfExists();
	GLimp_DestroyWindowIfExists();

	SDL_QuitSubSystem( SDL_INIT_VIDEO );

	Com_Memset( &glConfig, 0, sizeof( glConfig ) );
	Com_Memset( &glState, 0, sizeof( glState ) );
}

static void GLimp_Minimize()
{
	SDL_MinimizeWindow( window );
}

/*
===============
GLimp_CompareModes
===============
*/
static int GLimp_CompareModes( const void *a, const void *b )
{
	const float ASPECT_EPSILON = 0.001f;
	SDL_Rect    *modeA = ( SDL_Rect * ) a;
	SDL_Rect    *modeB = ( SDL_Rect * ) b;
	float       aspectA = ( float ) modeA->w / ( float ) modeA->h;
	float       aspectB = ( float ) modeB->w / ( float ) modeB->h;
	int         areaA = modeA->w * modeA->h;
	int         areaB = modeB->w * modeB->h;
	float       aspectDiffA = fabs( aspectA - displayAspect );
	float       aspectDiffB = fabs( aspectB - displayAspect );
	float       aspectDiffsDiff = aspectDiffA - aspectDiffB;

	if ( aspectDiffsDiff > ASPECT_EPSILON )
	{
		return 1;
	}
	else if ( aspectDiffsDiff < -ASPECT_EPSILON )
	{
		return -1;
	}
	else
	{
		return areaA - areaB;
	}
}

/*
===============
GLimp_DetectAvailableModes
===============
*/
static bool GLimp_DetectAvailableModes()
{
	char     buf[ MAX_STRING_CHARS ] = { 0 };
	SDL_Rect modes[ 128 ];
	int      numModes = 0;
	int      i;
	SDL_DisplayMode windowMode;
	int      display;

	display = SDL_GetWindowDisplayIndex( window );

	if ( SDL_GetWindowDisplayMode( window, &windowMode ) < 0 )
	{
		logger.Warn("Couldn't get window display mode: %s", SDL_GetError() );
		return false;
	}

	for ( i = 0; i < SDL_GetNumDisplayModes( display ); i++ )
	{
		SDL_DisplayMode mode;

		if ( SDL_GetDisplayMode( display, i, &mode ) < 0 )
		{
			continue;
		}

		if ( !mode.w || !mode.h )
		{
			logger.Notice("Display supports any resolution" );
			return true;
		}

		if ( windowMode.format != mode.format || windowMode.refresh_rate != mode.refresh_rate )
		{
			continue;
		}

		modes[ numModes ].w = mode.w;
		modes[ numModes ].h = mode.h;
		numModes++;
	}

	if ( numModes > 1 )
	{
		qsort( modes, numModes, sizeof( SDL_Rect ), GLimp_CompareModes );
	}

	for ( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ].w, modes[ i ].h );

		if ( strlen( newModeString ) < sizeof( buf ) - strlen( buf ) )
		{
			Q_strcat( buf, sizeof( buf ), newModeString );
		}
		else
		{
			logger.Warn("Skipping mode %ux%x, buffer too small", modes[ i ].w, modes[ i ].h );
		}
	}

	if ( *buf )
	{
		logger.Notice("Available modes: '%s'", buf );
		ri.Cvar_Set( "r_availableModes", buf );
	}

	return true;
}

static bool GLimp_CreateWindow( bool fullscreen, bool bordered )
{
	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;

	if ( r_allowResize->integer )
	{
		flags |= SDL_WINDOW_RESIZABLE;
	}

	SDL_Surface *icon = nullptr;

	icon = SDL_CreateRGBSurfaceFrom( ( void * ) CLIENT_WINDOW_ICON.pixel_data,
		CLIENT_WINDOW_ICON.width,
		CLIENT_WINDOW_ICON.height,
		CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
		CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
		0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
		0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
	);

	const char *windowType = nullptr;

	if ( fullscreen )
	{
		flags |= SDL_WINDOW_FULLSCREEN;
		windowType = "fullscreen";
	}

	/* We need to set borderless flag even when fullscreen
	because otherwise when disabling fullscreen the window
	will be bordered while the borderless option is enabled. */

	if ( !bordered )
	{
		flags |= SDL_WINDOW_BORDERLESS;

		/* Don't tell fullscreen window is borderless,
		it's meaningless. */
		if ( ! fullscreen )
		{
			windowType = "borderless";
		}
	}

	int x, y;
	if ( r_centerWindow->integer )
	{
		// center window on specified display
		x = SDL_WINDOWPOS_CENTERED_DISPLAY( r_displayIndex->integer );
		y = SDL_WINDOWPOS_CENTERED_DISPLAY( r_displayIndex->integer );
	}
	else
	{
		x = SDL_WINDOWPOS_UNDEFINED_DISPLAY( r_displayIndex->integer );
		y = SDL_WINDOWPOS_UNDEFINED_DISPLAY( r_displayIndex->integer );
	}

	window = SDL_CreateWindow( CLIENT_WINDOW_TITLE, x, y, glConfig.vidWidth, glConfig.vidHeight, flags );

	if ( window )
	{
		int w, h;
		SDL_GetWindowPosition( window, &x, &y );
		SDL_GetWindowSize( window, &w, &h );
		logger.Debug( "SDL %s%swindow created at %d,%d with %d×%d size",
			windowType ? windowType : "",
			windowType ? " ": "",
			x, y, w, h );
	}
	else
	{
		logger.Warn( "SDL %d×%d %s%swindow not created",
			glConfig.vidWidth, glConfig.vidHeight,
			windowType ? windowType : "",
			windowType ? " ": "" );
		logger.Warn("SDL_CreateWindow failed: %s", SDL_GetError() );
		return false;
	}

	SDL_SetWindowIcon( window, icon );

	SDL_FreeSurface( icon );

	return true;
}

static void GLimp_DestroyContextIfExists()
{
	if ( glContext != nullptr )
	{
		SDL_GL_DeleteContext( glContext );
		glContext = nullptr;
	}

}

static void GLimp_DestroyWindowIfExists()
{
	// Do not let orphaned context alive.
	GLimp_DestroyContextIfExists();

	if ( window != nullptr )
	{
		int x, y, w, h;
		SDL_GetWindowPosition( window, &x, &y );
		SDL_GetWindowSize( window, &w, &h );
		logger.Debug("Destroying %d×%d SDL window at %d,%d", w, h, x, y );
		SDL_DestroyWindow( window );
		window = nullptr;
	}
}

enum glProfile {
	undefinedProfile = 0,
	compatibilityProfile = 1,
	coreProfile = 2,
};

typedef struct {
	int major;
	int minor;
	int profile;
	int colorBits;
} glConfiguration;

static bool operator!=(const glConfiguration& c1, const glConfiguration& c2) {
	return c1.major != c2.major
		|| c1.minor != c2.minor
		|| c1.profile != c2.profile
		|| c1.colorBits != c2.colorBits;
}

static const char* GLimp_getProfileName( int profile )
{
	return profile == coreProfile ? "core" : "compatibility";
}

static SDL_GLContext GLimp_CreateContext( const glConfiguration &configuration )
{
	int perChannelColorBits = configuration.colorBits == 24 ? 8 : 4;

	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	if ( !r_glAllowSoftware->integer )
	{
		SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );
	}

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, configuration.major );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, configuration.minor );

	if ( configuration.profile == coreProfile )
	{
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
	}
	else
	{
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
	}

	if ( r_glDebugProfile->integer )
	{
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
	}

	return SDL_GL_CreateContext( window );
}

static bool GLimp_RecreateContextWhenChange( const glConfiguration &configuration )
{
	static glConfiguration currentConfiguration = {};
	static SDL_Window *currentWindow = nullptr; 

	if ( glContext == nullptr
		|| configuration != currentConfiguration
		|| window != currentWindow )
	{
		currentConfiguration = configuration;
		currentWindow = window;

		GLimp_DestroyContextIfExists();
		glContext = GLimp_CreateContext( currentConfiguration );

		const char* CurrentProfileName = GLimp_getProfileName( currentConfiguration.profile );

		if ( glContext != nullptr )
		{
			logger.Debug( "Valid context: %d-bit OpenGL %d.%d %s",
				currentConfiguration.colorBits,
				currentConfiguration.major,
				currentConfiguration.minor,
				CurrentProfileName );
		}
		else
		{
			logger.Debug( "Invalid context: %d-bit OpenGL %d.%d %s",
				currentConfiguration.colorBits,
				currentConfiguration.major,
				currentConfiguration.minor,
				CurrentProfileName );
		}
	}

	return glContext != nullptr;
}

/* GLimp_DestroyWindowIfExists checks if window exists before
destroying it so we can call GLimp_RecreateWindowWhenChange even
if no window is created yet.

It is assumed width, height and other things like that are unchanged,
given vid_restart is called when changing those and then destroying
the window before calling this. */
static bool GLimp_RecreateWindowWhenChange( const bool fullscreen, const bool bordered, const glConfiguration &configuration )
{
	static bool currentFullscreen = false;
	static bool currentBordered = false;
	static int currentColorBits = 0;

	if ( window == nullptr
		|| fullscreen != currentFullscreen
		|| bordered != currentBordered
		|| configuration.colorBits != currentColorBits )
	{
		currentFullscreen = fullscreen;
		currentBordered = bordered;
		currentColorBits = configuration.colorBits;

		GLimp_DestroyWindowIfExists();

		if ( !GLimp_CreateWindow( fullscreen, bordered ) )
		{
			return false;
		}
	}

	return true;
}

static rserr_t GLimp_SetModeAndResolution( const int mode )
{
	SDL_DisplayMode desktopMode;

	if ( SDL_GetDesktopDisplayMode( r_displayIndex->integer, &desktopMode ) == 0 )
	{
		displayAspect = ( float ) desktopMode.w / ( float ) desktopMode.h;
		logger.Notice("Display aspect: %.3f", displayAspect );
	}
	else
	{
		Com_Memset( &desktopMode, 0, sizeof( SDL_DisplayMode ) );

		logger.Notice("Cannot determine display aspect, assuming 1.333: %s", SDL_GetError() );
		logger.Notice("Display aspect: 1.333");
	}

	if ( mode == -2 )
	{
		// use desktop video resolution
		if ( desktopMode.h > 0 )
		{
			glConfig.vidWidth = desktopMode.w;
			glConfig.vidHeight = desktopMode.h;
		}
		else
		{
			glConfig.vidWidth = 640;
			glConfig.vidHeight = 480;
			logger.Notice("Cannot determine display resolution, assuming 640x480" );
		}
	}
	else if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, mode ) )
	{
		logger.Notice("Invalid mode %d", mode );
		return rserr_t::RSERR_INVALID_MODE;
	}

	logger.Notice("...setting mode %d: %d×%d", mode, glConfig.vidWidth, glConfig.vidHeight );

	return rserr_t::RSERR_OK;
}

static rserr_t GLimp_ValidateBestContext( const int GLEWmajor, glConfiguration &bestValidatedConfiguration )
{
	/* We iterate known 4.x, 3.x, 2.x and 1.x OpenGL versions
	from highest to lowest, starting with core profile then trying
	compatibility profile when core profile is supported for such
	version, trying with 24-bit display first, then 16-bit.

	Once we get a core profile working, we stop and attempt
	to use 3.2 core whatever the highest version we validated.
	The 3.2 is the oldest version with core profile, meaning
	every extension not in 3.2 code are expected to be loaded
	explicitely. This may make extension loading more predictable.

	For debugging and knowledge purpose we log the best supported
	version even if we're not gonna use it.

	User can request explicit version or profile with related cvars.

	We test down to the lowest known OpenGL version so we can provide
	useful and precise error message when an OpenGL version is too low.

	The idea of going from high to low is to make the engine load fast
	on actual hardware likely to support highest core profile version,
	then spend more time on more rarest configuration. The highest
	loading time affects hardware and related drivers that can't run
	then engine anyway.

	For known OpenGL version,
	see https://en.wikipedia.org/wiki/OpenGL#Version_history

	For information about core, compatibility and forward profiles,
	see https://www.khronos.org/opengl/wiki/OpenGL_Context */

	const int glSupportArray[][3] = {
		{ 4, 6, coreProfile },
		{ 4, 5, coreProfile },
		{ 4, 4, coreProfile },
		{ 4, 3, coreProfile },
		{ 4, 2, coreProfile },
		{ 4, 1, coreProfile },
		{ 4, 0, coreProfile },
		{ 3, 3, coreProfile },
		{ 3, 2, coreProfile },
		{ 3, 1, compatibilityProfile },
		{ 3, 0, compatibilityProfile },
		{ 2, 1, compatibilityProfile },
		{ 2, 0, compatibilityProfile },
		{ 1, 5, compatibilityProfile },
		{ 1, 4, compatibilityProfile },
		{ 1, 3, compatibilityProfile },
		{ 1, 2, compatibilityProfile },
		{ 1, 1, compatibilityProfile },
		{ 1, 0, compatibilityProfile },
		{ 0, 0, undefinedProfile },
	};

	Log::Debug( "Validating best context." );

	for ( int colorBits = 24; colorBits >= 16; colorBits -= 8 )
	{
		// Test core profiles, stop on first valid one.
		for ( int row = 0; glSupportArray[ row ][ 0 ] != 0; row++ )
		{
			if ( GLEWmajor < 2 )
			{
				// GLEW version < 2.0.0 doesn't support OpenGL core profiles.
				break;
			}

			glConfiguration testConfiguration = {};

			testConfiguration.major = glSupportArray[ row ][ 0 ];
			testConfiguration.minor = glSupportArray[ row ][ 1 ];
			testConfiguration.profile = glSupportArray[ row ][ 2 ];
			testConfiguration.colorBits = colorBits;

			if ( testConfiguration.profile == compatibilityProfile )
			{
				break;
			}

			// Do the testing with borderless window.
			if ( !GLimp_RecreateWindowWhenChange( false, false, testConfiguration ) )
			{
				return rserr_t::RSERR_INVALID_MODE;
			}

			if ( GLimp_RecreateContextWhenChange( testConfiguration ) )
			{
				bestValidatedConfiguration = testConfiguration;
				return rserr_t::RSERR_OK;
			}
		}

		// Test compatibility profiles if no core profile is valid.
		for ( int row = 0; glSupportArray[ row ][ 0 ] != 0; row++ )
		{
			glConfiguration testConfiguration = {};

			testConfiguration.major = glSupportArray[ row ][ 0 ];
			testConfiguration.minor = glSupportArray[ row ][ 1 ];
			testConfiguration.profile = compatibilityProfile;
			testConfiguration.colorBits = colorBits;

			// Do the testing with borderless window.
			if ( !GLimp_RecreateWindowWhenChange( false, false, testConfiguration ) )
			{
				return rserr_t::RSERR_INVALID_MODE;
			}

			if ( GLimp_RecreateContextWhenChange( testConfiguration ) )
			{
				bestValidatedConfiguration = testConfiguration;
				return rserr_t::RSERR_OK;
			}
		}
	}

	if ( bestValidatedConfiguration.major == 0 )
	{
		return rserr_t::RSERR_MISSING_GL;
	}

	return rserr_t::RSERR_OLD_GL;
}

static rserr_t GLimp_ApplyCustomOptions( const int GLEWmajor, const bool fullscreen, const bool bordered, const glConfiguration &bestConfiguration, glConfiguration &requestedConfiguration, bool &customOptions )
{
	glConfiguration customConfiguration = {};

	if ( bestConfiguration.profile == coreProfile && !Q_stricmp( r_glProfile->string, "compat" ) )
	{
		logger.Debug( "Compatibility profile is forced by r_glProfile" );

		customConfiguration.profile = compatibilityProfile;
		customOptions = true;
	}

	if ( bestConfiguration.profile == compatibilityProfile && !Q_stricmp( r_glProfile->string, "core" ) )
	{
		if ( GLEWmajor < 2 )
		{
			// GLEW version < 2.0.0 doesn't support OpenGL core profiles.
			logger.Debug( "Core profile is ignored from r_glProfile" );
		}
		else
		{
			logger.Debug( "Core profile is forced by r_glProfile" );

			customConfiguration.profile = coreProfile;
			customOptions = true;
		}
	}

	// Beware: unset cvar is equal to 0.

	customConfiguration.major = std::max( 0, r_glMajorVersion->integer );
	customConfiguration.minor = std::max( 0, r_glMinorVersion->integer );

	if ( customConfiguration.major == 0 )
	{
		customConfiguration.major = bestConfiguration.major;
		customConfiguration.minor = bestConfiguration.minor;
	}
	else if ( customConfiguration.major == 1 )
	{
		logger.Warn( "OpenGL %d.%d is not supported, trying %d.%d instead",
			customConfiguration.major,
			customConfiguration.minor,
			bestConfiguration.major,
			bestConfiguration.minor );

		customConfiguration.major = bestConfiguration.major;
		customConfiguration.minor = bestConfiguration.minor;
	}
	else
	{
		if ( customConfiguration.major == 3
			&& customConfiguration.minor < 2
			&& customConfiguration.profile == undefinedProfile )
		{
			customConfiguration.profile = compatibilityProfile;
		}
		else if ( customConfiguration.major == 2 )
		{
			if ( customConfiguration.profile == undefinedProfile )
			{
				customConfiguration.profile = compatibilityProfile;
			}

			if ( customConfiguration.minor == 0 )
			{
				logger.Warn( "OpenGL 2.0 is not supported, trying 2.1 instead" );

				customConfiguration.minor = 1;
			}
		}

		logger.Debug( "GL version %d.%d is forced by r_MajorVersion and r_MinorVersion",
			customConfiguration.major,
			customConfiguration.minor );

		customOptions = true;
	}

	if ( customConfiguration.profile == undefinedProfile )
	{
		customConfiguration.profile = bestConfiguration.profile;
	}

	customConfiguration.colorBits = std::max( 0, r_colorbits->integer );

	if ( customConfiguration.colorBits == 0 )
	{
		customConfiguration.colorBits = bestConfiguration.colorBits;
	}
	else
	{
		if ( customConfiguration.colorBits != bestConfiguration.colorBits )
		{
			logger.Debug( "Color framebuffer bitness %d is forced by r_colorbits",
				customConfiguration.colorBits );

			customOptions = true;
		}
	}

	if ( customOptions )
	{
		if ( !GLimp_RecreateWindowWhenChange( fullscreen, bordered, customConfiguration ) )
		{
			return rserr_t::RSERR_INVALID_MODE;
		}

		const char* customProfileName = GLimp_getProfileName( customConfiguration.profile );

		/* When calling GLimp_RecreateContextWhenChange() some drivers may
		provide a valid context that is unusable while GL_CheckErrors()
		catches nothing.

		We can catch some errors when calling GLimp_DetectAvailableModes(),
		see below. */

		if ( GLimp_RecreateContextWhenChange( customConfiguration ) )
		{
			logger.Notice( "Using custom context: %d-bit OpenGL %d.%d %s",
				customConfiguration.colorBits,
				customConfiguration.major,
				customConfiguration.minor,
				customProfileName );

			requestedConfiguration = customConfiguration;
		}
		else
		{
			logger.Warn( "Failed custom context: %d-bit OpenGL %d.%d %s",
				customConfiguration.colorBits,
				customConfiguration.major,
				customConfiguration.minor,
				customProfileName );

			logger.Warn( "SDL_GL_CreateContext failed: %s", SDL_GetError() );

			customOptions = false;
		}
	}

	return rserr_t::RSERR_OK;
}

static rserr_t GLimp_ApplyBestOptions( const bool fullscreen, const bool bordered, const glConfiguration &bestConfiguration, glConfiguration &requestedConfiguration )
{
	if ( !GLimp_RecreateWindowWhenChange( fullscreen, bordered, bestConfiguration ) )
	{
		return rserr_t::RSERR_INVALID_MODE;
	}

	bool success = false;

	if ( bestConfiguration.profile == coreProfile )
	{
		glConfiguration preferredConfiguration = {};

		preferredConfiguration.major = 3;
		preferredConfiguration.minor = 2;
		preferredConfiguration.profile = bestConfiguration.profile;
		preferredConfiguration.colorBits = bestConfiguration.colorBits;

		const char* preferredProfileName = GLimp_getProfileName( preferredConfiguration.profile );

		if ( GLimp_RecreateContextWhenChange( preferredConfiguration ) )
		{
			logger.Notice( "Using preferred context: %d-bit OpenGL %d.%d %s", 
				preferredConfiguration.colorBits,
				preferredConfiguration.major,
				preferredConfiguration.minor,
				preferredProfileName );

			success = true;
			requestedConfiguration = preferredConfiguration;
		}
		else
		{
			logger.Warn( "Failed preferred context: %d-bit OpenGL %d.%d %s", 
				bestConfiguration.colorBits,
				preferredConfiguration.major,
				preferredConfiguration.minor,
				preferredProfileName );
		}
	}

	if ( !success )
	{
		const char* bestProfileName = GLimp_getProfileName( bestConfiguration.profile );

		if ( GLimp_RecreateContextWhenChange( bestConfiguration ) )
		{
			logger.Notice( "Using best context: %d-bit OpenGL %d.%d %s",
				bestConfiguration.colorBits,
				bestConfiguration.major,
				bestConfiguration.minor,
				bestProfileName );

			success = true;
			requestedConfiguration = bestConfiguration;
		}
		else
		{
			logger.Warn( "Failed best context: %d-bit OpenGL %d.%d %s",
				bestConfiguration.colorBits,
				bestConfiguration.major,
				bestConfiguration.minor,
				bestProfileName );
		}
	}

	if ( !success )
	{
		logger.Warn( "SDL_GL_CreateContext failed: %s", SDL_GetError() );
		GLimp_DestroyWindowIfExists();
		return rserr_t::RSERR_INVALID_MODE;
	}

	return rserr_t::RSERR_OK;
}

static void GLimp_RegisterConfiguration( const glConfiguration& bestConfiguration, const glConfiguration &requestedConfiguration )
{
	glConfig2.glBestMajor = bestConfiguration.major;
	glConfig2.glBestMinor = bestConfiguration.minor;

	glConfig2.glRequestedMajor = requestedConfiguration.major;
	glConfig2.glRequestedMinor = requestedConfiguration.minor;

	SDL_GL_SetSwapInterval( r_swapInterval->integer );

	// alphaBits was used by legacy renderer, do we need it for anything?
	// int alphaBits = std::max( 0, r_alphabits->integer );

	// FIXME: It looks like MSAA was only implemented in legacy renderer.
	// int samples = std::max( 0, r_ext_multisample->integer );

	glConfig.colorBits = requestedConfiguration.colorBits;
	glConfig.depthBits =  std::max( 0, r_depthbits->integer );
	glConfig.stencilBits = std::max( 0, r_stencilbits->integer );

	{
		/* Make sure we don't silence any useful error that would
		already have happened. */
		GL_CheckErrors();

		// Check if we have a core profile.
		int profileBit;
		glGetIntegerv( GL_CONTEXT_PROFILE_MASK, &profileBit );

		/* OpenGL implementations not supporting core profile like the ones only
		implementing OpenGL version older than 3.2 may raise a GL_INVALID_ENUM
		error while implementations supporting core profile may not raise the
		error when forcing an OpenGL version older than 3.2, so we better want
		to catch and silence this expected error. */

		if ( glGetError() != GL_NO_ERROR )
		{
			glConfig2.glCoreProfile = false;
		}
		else
		{
			glConfig2.glCoreProfile = ( profileBit == GL_CONTEXT_CORE_PROFILE_BIT );
		}

		int providedProfile = glConfig2.glCoreProfile ? coreProfile : compatibilityProfile ;
		const char *providedProfileName = GLimp_getProfileName( providedProfile );

		if ( providedProfile != requestedConfiguration.profile )
		{
			const char *requestedProfileName = GLimp_getProfileName( requestedConfiguration.profile );

			logger.Warn( "Provided OpenGL %s profile is not the same as requested %s profile.",
				providedProfileName,
				requestedProfileName );
		}
		else
		{
			logger.Debug( "Provided OpenGL context uses %s profile.",
			providedProfileName );
		}
	}

	if ( requestedConfiguration.profile == coreProfile )
	{
		// Check if context is forward compatible.
		int contextFlags;
		glGetIntegerv( GL_CONTEXT_FLAGS, &contextFlags );

		glConfig2.glForwardCompatibleContext = contextFlags & GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT;

		if ( glConfig2.glForwardCompatibleContext )
		{
			Log::Debug( "Provided OpenGL core context is forward compatible." );
		}
		else
		{
			Log::Debug( "Provided OpenGL core context is not forward compatible." );
		}
	}
	else
	{
		glConfig2.glForwardCompatibleContext = false;
	}

	logger.Notice("Using %d Color bits, %d depth, %d stencil display.",
		glConfig.colorBits,
		glConfig.depthBits,
		glConfig.stencilBits );

	{
		int GLmajor, GLminor;
		sscanf( ( const char * ) glGetString( GL_VERSION ), "%d.%d", &GLmajor, &GLminor );

		glConfig2.glMajor = GLmajor;
		glConfig2.glMinor = GLminor;
	}

	/* FIXME: a duplicate of this is done in GLimp_Init, also storing it
	for gfxinfo command. */
	const char *glstring = ( char * ) glGetString( GL_RENDERER );
	logger.Notice("OpenGL Renderer: %s", glstring );
}

static void GLimp_DrawWindowContent()
{
	// Fill window with a dark grey (#141414) background.
	glClearColor( 0.08f, 0.08f, 0.08f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	GLimp_EndFrame();
}

static rserr_t GLimp_CheckOpenGLVersion( const glConfiguration &requestedConfiguration )
{
	if ( glConfig2.glMajor != requestedConfiguration.major
		|| glConfig2.glMinor != requestedConfiguration.minor )
	{
		logger.Warn( "Provided OpenGL %d.%d is not the same as requested %d.%d version",
			glConfig2.glMajor,
			glConfig2.glMinor,
			requestedConfiguration.major,
			requestedConfiguration.minor );
	}
	else
	{
		logger.Debug( "Provided OpenGL %d.%d version.",
			glConfig2.glMajor,
			glConfig2.glMinor );
	}

	if ( glConfig2.glMajor < 2 || ( glConfig2.glMajor == 2 && glConfig2.glMinor < 1 ) )
	{
		GLimp_DestroyWindowIfExists();

		// Missing shader support, there is no OpenGL 1.x renderer anymore.

		return rserr_t::RSERR_OLD_GL;
	}

	if ( glConfig2.glMajor < 3 || ( glConfig2.glMajor == 3 && glConfig2.glMinor < 2 ) )
	{
		// Shaders are supported, but not all OpenGL 3.x features
		logger.Notice("Using GL3 Renderer in OpenGL 2.x mode..." );
	}
	else
	{
		logger.Notice("Using GL3 Renderer in OpenGL 3.x mode..." );
		glConfig.driverType = glDriverType_t::GLDRV_OPENGL3;
	}

	return rserr_t::RSERR_OK;
}

static void GLimp_CheckGLEW( const glConfiguration &requestedConfiguration )
{
	GLenum glewResult = glewInit();

#ifdef GLEW_ERROR_NO_GLX_DISPLAY
	if ( glewResult != GLEW_OK && glewResult != GLEW_ERROR_NO_GLX_DISPLAY )
#else
	if ( glewResult != GLEW_OK )
#endif
	{
		// glewInit failed, something is seriously wrong

		GLimp_DestroyWindowIfExists();

		const char* requestedProfileName = GLimp_getProfileName( requestedConfiguration.profile );

		Sys::Error( "GLEW initialization failed: %s.\n\n"
			"Engine successfully created %d-bit OpenGL %d.%d %d context,\n"
			"This is a GLEW issue.",
			glewGetErrorString( glewResult ),
			requestedConfiguration.colorBits,
			requestedConfiguration.major,
			requestedConfiguration.minor,
			requestedProfileName );
	}
}

/*
===============
GLimp_SetMode
===============
*/
static rserr_t GLimp_SetMode( const int mode, const bool fullscreen, const bool bordered )
{
	logger.Notice("Initializing OpenGL display" );

	int GLEWmajor;

	{
		const GLubyte * glewVersion = glewGetString( GLEW_VERSION );

		logger.Notice("Using GLEW version %s", glewVersion );

		int GLEWminor, GLEWmicro;

		sscanf( ( const char * ) glewVersion, "%d.%d.%d",
			&GLEWmajor, &GLEWminor, &GLEWmicro );

		if ( GLEWmajor < 2 )
		{
			logger.Warn( "GLEW version < 2.0.0 doesn't support OpenGL core profiles." );
		}
	}

	{
		rserr_t err = GLimp_SetModeAndResolution( mode );

		if ( err != rserr_t::RSERR_OK )
		{
			return err;
		}
	}

	// HACK: We want to set the current value, not the latched value
	Cvar::ClearFlags("r_customwidth", CVAR_LATCH);
	Cvar::ClearFlags("r_customheight", CVAR_LATCH);
	Cvar_Set( "r_customwidth", va("%d", glConfig.vidWidth ) );
	Cvar_Set( "r_customheight", va("%d", glConfig.vidHeight ) );
	Cvar::AddFlags("r_customwidth", CVAR_LATCH);
	Cvar::AddFlags("r_customheight", CVAR_LATCH);

	// Attempt to detect best configuration.

	static glConfiguration bestValidatedConfiguration = {};

	if ( bestValidatedConfiguration.major != 0 )
	{
		const char* bestValidatedProfileName = GLimp_getProfileName( bestValidatedConfiguration.profile );

		Log::Debug( "Previously best validated context: %d-bit OpenGL %d.%d %s",
			bestValidatedConfiguration.colorBits,
			bestValidatedConfiguration.major,
			bestValidatedConfiguration.minor,
			bestValidatedProfileName );
	}
	else
	{
		rserr_t err = GLimp_ValidateBestContext( GLEWmajor, bestValidatedConfiguration );

		if ( err == rserr_t::RSERR_OLD_GL )
		{
			// Used by error message.
			glConfig2.glMajor = bestValidatedConfiguration.major;
			glConfig2.glMinor = bestValidatedConfiguration.minor;

			GLimp_DestroyWindowIfExists();
			return err;
		}
		else if ( err != rserr_t::RSERR_OK )
		{
			GLimp_DestroyWindowIfExists();
			return err;
		}
	}

	static glConfiguration bestConfiguration = bestValidatedConfiguration;

	const char* bestProfileName = GLimp_getProfileName( bestConfiguration.profile );

	logger.Notice( "Best context: %d-bit OpenGL %d.%d %s",
		bestConfiguration.colorBits,
		bestConfiguration.major,
		bestConfiguration.minor,
		bestProfileName );

	static glConfiguration requestedConfiguration = {};

	// Attempt to apply custom configuration.

	bool customOptions = false;

	{
		rserr_t err = GLimp_ApplyCustomOptions( GLEWmajor, fullscreen, bordered, bestConfiguration, requestedConfiguration, customOptions );

		if ( err != rserr_t::RSERR_OK )
		{
			return err;
		}
	}

	// If not custom configuration is set or if it faild,
	// attempt to apply 3.2 if core profile is supported,
	// otherwise attempt to apply best configuration.


	if ( !customOptions )
	{
		rserr_t err = GLimp_ApplyBestOptions( fullscreen, bordered, bestConfiguration, requestedConfiguration );

		if ( err != rserr_t::RSERR_OK )
		{
			return err;
		}
	}

	GLimp_DrawWindowContent();

	GLimp_RegisterConfiguration( bestConfiguration, requestedConfiguration );

	{
		rserr_t err = GLimp_CheckOpenGLVersion( requestedConfiguration );

		if ( err != rserr_t::RSERR_OK )
		{
			return err;
		}
	}

	/* GLimp_CheckGLEW() calls Sys::Error directly so it does not return
	in case of error. */

	GLimp_CheckGLEW( requestedConfiguration );

	/* When calling GLimp_RecreateContextWhenChange() some drivers may
	provide a valid context that is unusable while GL_CheckErrors()
	catches nothing.

	For example 3840×2160 is too large for the Radeon 9700 and
	the Mesa r300 driver may print this error when the requested
	resolution is higher than what is supported by hardware:

	> r300: Implementation error: Render targets are too big in r300_set_framebuffer_state, refusing to bind framebuffer state!

	The engine would later make a segfault when calling GL_SetDefaultState
	from tr_init.cpp if we don't do anything.

	Hopefully it looks like SDL_GetWindowDisplayMode can raise this error:

	> Couldn't find display mode match

	so we catch this SDL error when calling GLimp_DetectAvailableModes()
	and force a safe lower resolution before we start to draw to prevent
	an engine segfault when calling GL_SetDefaultState(). */

	if ( GLimp_DetectAvailableModes() )
	{
		return rserr_t::RSERR_OK;
	}

	// The engine will retry with a default mode.
	return rserr_t::RSERR_UNKNOWN;
}

/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static bool GLimp_StartDriverAndSetMode( int mode, bool fullscreen, bool bordered )
{
	int numDisplays;

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		const char *driverName;
		SDL_version v;
		SDL_GetVersion( &v );

		logger.Notice("SDL_Init( SDL_INIT_VIDEO )... " );
		logger.Notice("Using SDL version %u.%u.%u", v.major, v.minor, v.patch );

		/* SDL_INIT_NOPARACHUTE flag is removed.

		> SDL_INIT_NOPARACHUTE: compatibility; this flag is ignored
		> -- https://wiki.libsdl.org/SDL_Init

		It is recommended to test for negative value and not just -1.

		> Returns 0 on success or a negative error code on failure;
		> call SDL_GetError() for more information.
		> -- https://wiki.libsdl.org/SDL_Init

		the SDL_GetError page also gives a sample of code testing for < 0
		> if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
		> -- https://wiki.libsdl.org/SDL_GetError */

		if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
		{
			logger.Notice("SDL_Init( SDL_INIT_VIDEO ) failed: %s", SDL_GetError() );
			return false;
		}

		driverName = SDL_GetCurrentVideoDriver();

		if ( !driverName )
		{
			Sys::Error( "No video driver initialized\n" );
		}

		logger.Notice("SDL using driver \"%s\"", driverName );
		ri.Cvar_Set( "r_sdlDriver", driverName );
	}

	numDisplays = SDL_GetNumVideoDisplays();

	if ( numDisplays <= 0 )
	{
		Sys::Error( "SDL_GetNumVideoDisplays failed: %s\n", SDL_GetError() );
	}

	AssertCvarRange( r_displayIndex, 0, numDisplays - 1, true );

	if ( fullscreen && ri.Cvar_VariableIntegerValue( "in_nograb" ) )
	{
		logger.Notice("Fullscreen not allowed with in_nograb 1" );
		ri.Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = false;
		fullscreen = false;
	}

	rserr_t err = GLimp_SetMode(mode, fullscreen, bordered);

	switch ( err )
	{
		case rserr_t::RSERR_OK:
			return true;

		case rserr_t::RSERR_INVALID_FULLSCREEN:
			logger.Warn("GLimp: Fullscreen unavailable in this mode" );
			break;

		case rserr_t::RSERR_INVALID_MODE:
			logger.Warn("GLimp: Could not set mode %d", mode );
			break;

		case rserr_t::RSERR_MISSING_GL:
			Sys::Error(
				"OpenGL is not available.\n\n"
				"You need a graphic card with drivers supporting at least OpenGL 3.2\n"
				"or OpenGL 2.1 with ARB_half_float_vertex and ARB_framebuffer_object." );

			// Sys:Error calls OSExit() so the break and the return is unreachable.
			break;

		case rserr_t::RSERR_OLD_GL:
			Sys::Error(
				"OpenGL %d.%d is too old.\n\n"
				"You need a graphic card with drivers supporting at least OpenGL 3.2\n"
				"or OpenGL 2.1 with ARB_half_float_vertex and ARB_framebuffer_object.",
				glConfig2.glMajor, glConfig2.glMinor );

			// Sys:Error calls OSExit() so the break and the return is unreachable.
			break;

		case rserr_t::RSERR_UNKNOWN:
		default:
			logger.Warn("GLimp: Unknown error for mode %d", mode );
			break;
	}

	return false;
}

static GLenum debugTypes[] =
{
	0,
	GL_DEBUG_TYPE_ERROR_ARB,
	GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB,
	GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB,
	GL_DEBUG_TYPE_PORTABILITY_ARB,
	GL_DEBUG_TYPE_PERFORMANCE_ARB,
	GL_DEBUG_TYPE_OTHER_ARB
};

#ifdef _WIN32
#define DEBUG_CALLBACK_CALL APIENTRY
#else
#define DEBUG_CALLBACK_CALL
#endif
static void DEBUG_CALLBACK_CALL GLimp_DebugCallback( GLenum, GLenum type, GLuint,
                                       GLenum severity, GLsizei, const GLchar *message, const void* )
{
	const char *debugTypeName;
	const char *debugSeverity;

	if ( r_glDebugMode->integer <= Util::ordinal(glDebugModes_t::GLDEBUG_NONE))
	{
		return;
	}

	if ( r_glDebugMode->integer < Util::ordinal(glDebugModes_t::GLDEBUG_ALL))
	{
		if ( debugTypes[ r_glDebugMode->integer ] != type )
		{
			return;
		}
	}

	switch ( type )
	{
		case GL_DEBUG_TYPE_ERROR_ARB:
			debugTypeName = "DEBUG_TYPE_ERROR";
			break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
			debugTypeName = "DEBUG_TYPE_DEPRECATED_BEHAVIOR";
			break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
			debugTypeName = "DEBUG_TYPE_UNDEFINED_BEHAVIOR";
			break;
		case GL_DEBUG_TYPE_PORTABILITY_ARB:
			debugTypeName = "DEBUG_TYPE_PORTABILITY";
			break;
		case GL_DEBUG_TYPE_PERFORMANCE_ARB:
			debugTypeName = "DEBUG_TYPE_PERFORMANCE";
			break;
		case GL_DEBUG_TYPE_OTHER_ARB:
			debugTypeName = "DEBUG_TYPE_OTHER";
			break;
		default:
			debugTypeName = "DEBUG_TYPE_UNKNOWN";
			break;
	}

	switch ( severity )
	{
		case GL_DEBUG_SEVERITY_HIGH_ARB:
			debugSeverity = "high";
			break;
		case GL_DEBUG_SEVERITY_MEDIUM_ARB:
			debugSeverity = "med";
			break;
		case GL_DEBUG_SEVERITY_LOW_ARB:
			debugSeverity = "low";
			break;
		default:
			debugSeverity = "none";
			break;
	}

	logger.Warn("%s: severity: %s msg: %s", debugTypeName, debugSeverity, message );
}

/*
===============
GLimp_InitExtensions
===============
*/

/* ExtFlag_CORE means the extension is known to be an OpenGL 3 core extension.
The code considers the extension is available even if the extension is not listed
if the driver pretends to support OpenGL Core 3 and we know this extension is part
of OpenGL Core 3. */

enum {
	ExtFlag_NONE,
	ExtFlag_REQUIRED = BIT( 1 ),
	ExtFlag_CORE = BIT( 2 ),
};

static bool LoadExt( int flags, bool hasExt, const char* name, bool test = true )
{
	if ( hasExt || ( flags & ExtFlag_CORE && glConfig2.glCoreProfile) )
	{
		if ( test )
		{
			logger.WithoutSuppression().Notice( "...using GL_%s", name );

			if ( glConfig2.glEnabledExtensionsString.length() != 0 )
			{
				glConfig2.glEnabledExtensionsString += " ";
			}

			glConfig2.glEnabledExtensionsString += "GL_";
			glConfig2.glEnabledExtensionsString += name;

			return true;
		}
		else
		{
			// Required extension can't be made optional
			ASSERT( !( flags & ExtFlag_REQUIRED ) );

			logger.WithoutSuppression().Notice( "...ignoring GL_%s", name );
		}
	}
	else
	{
		if ( flags & ExtFlag_REQUIRED )
		{
			Sys::Error( "Required extension GL_%s is missing.", name );
		}
		else
		{
			logger.WithoutSuppression().Notice( "...GL_%s not found.", name );

			if ( glConfig2.glMissingExtensionsString.length() != 0 )
			{
				glConfig2.glMissingExtensionsString += " ";
			}

			glConfig2.glMissingExtensionsString += "GL_";
			glConfig2.glMissingExtensionsString += name;
		}
	}
	return false;
}

#define LOAD_EXTENSION(flags, ext) LoadExt(flags, GLEW_##ext, #ext)

#define LOAD_EXTENSION_WITH_TEST(flags, ext, test) LoadExt(flags, GLEW_##ext, #ext, test)

static void GLimp_InitExtensions()
{
	logger.Notice("Initializing OpenGL extensions" );

	glConfig2.glEnabledExtensionsString = std::string();
	glConfig2.glMissingExtensionsString = std::string();

	if ( LOAD_EXTENSION_WITH_TEST( ExtFlag_NONE, ARB_debug_output, r_glDebugProfile->value ) )
	{
		glDebugMessageCallbackARB( (GLDEBUGPROCARB)GLimp_DebugCallback, nullptr );
		glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );
	}

	// Shader limits
	glGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB, &glConfig2.maxVertexUniforms );
	glGetIntegerv( GL_MAX_VERTEX_ATTRIBS_ARB, &glConfig2.maxVertexAttribs );

	int reservedComponents = 36 * 10; // approximation how many uniforms we have besides the bone matrices
	glConfig2.maxVertexSkinningBones = Math::Clamp( ( glConfig2.maxVertexUniforms - reservedComponents ) / 16, 0, MAX_BONES );
	glConfig2.vboVertexSkinningAvailable = r_vboVertexSkinning->integer && ( ( glConfig2.maxVertexSkinningBones >= 12 ) ? true : false );

	// GLSL

	Q_strncpyz( glConfig2.shadingLanguageVersionString, ( char * ) glGetString( GL_SHADING_LANGUAGE_VERSION_ARB ),
				sizeof( glConfig2.shadingLanguageVersionString ) );
	int majorVersion, minorVersion;
	if ( sscanf( glConfig2.shadingLanguageVersionString, "%i.%i", &majorVersion, &minorVersion ) != 2 )
	{
		logger.Warn("unrecognized shading language version string format" );
	}
	glConfig2.shadingLanguageVersion = majorVersion * 100 + minorVersion;

	logger.Notice("...found shading language version %i", glConfig2.shadingLanguageVersion );

	// Texture formats and compression
	glGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig2.maxCubeMapTextureSize );

	// made required in OpenGL 3.0
	glConfig2.textureHalfFloatAvailable =  LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, ARB_half_float_pixel, r_ext_half_float_pixel->value );

	// made required in OpenGL 3.0
	glConfig2.textureFloatAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, ARB_texture_float, r_ext_texture_float->value );

	// made required in OpenGL 3.0
	glConfig2.gpuShader4Available = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, EXT_gpu_shader4, r_ext_gpu_shader4->value );

	// made required in OpenGL 3.0
	// GL_EXT_texture_integer can be used in shaders only if GL_EXT_gpu_shader4 is also available
	glConfig2.textureIntegerAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, EXT_texture_integer, r_ext_texture_integer->value )
	  && glConfig2.gpuShader4Available;

	// made required in OpenGL 3.0
	glConfig2.textureRGAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, ARB_texture_rg, r_ext_texture_rg->value );

	{
		/* GT218-based GPU with Nvidia 340.108 driver advertising
		ARB_texture_gather extension is know to fail to compile
		the depthtile1 GLSL shader.

		See https://github.com/DaemonEngine/Daemon/issues/368

		Unfortunately this workaround may also disable the feature for
		all GPUs using this driver even if we don't know if some of them
		are not affected by the bug while advertising this extension, but
		there is no known easy way to detect GT218-based cards. Not all cards
		using 340 driver supports this extension anyway, like the G92 one.

		We can assume cards not using the 340 driver are not GT218 ones and
		are not affected.

		Usually, those GT218 cards are not powerful enough for dynamic
		lighting so it is likely this feature would be disabled to
		get acceptable framerate on this hardware anyway, making the
		need for such extension and the related shader code useless. */
		bool foundNvidia340 = ( Q_stristr( glConfig.vendor_string, "NVIDIA Corporation" ) && Q_stristr( glConfig.version_string, "NVIDIA 340." ) );

		if ( foundNvidia340 )
		{
			// No need for WithoutSuppression for something which can only be printed once per renderer restart.
			logger.Notice("...found buggy Nvidia 340 driver");
		}

		// made required in OpenGL 4.0
		glConfig2.textureGatherAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_NONE, ARB_texture_gather, r_arb_texture_gather->value && !foundNvidia340 );
	}

	// made required in OpenGL 1.3
	glConfig.textureCompression = textureCompression_t::TC_NONE;
	if( LOAD_EXTENSION( ExtFlag_NONE, EXT_texture_compression_s3tc ) )
	{
		glConfig.textureCompression = textureCompression_t::TC_S3TC;
	}

	// made required in OpenGL 3.0
	glConfig2.textureCompressionRGTCAvailable = LOAD_EXTENSION( ExtFlag_CORE, ARB_texture_compression_rgtc );

	// Texture - others
	glConfig2.textureAnisotropyAvailable = false;
	if ( LOAD_EXTENSION_WITH_TEST( ExtFlag_NONE, EXT_texture_filter_anisotropic, r_ext_texture_filter_anisotropic->value ) )
	{
		glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig2.maxTextureAnisotropy );
		glConfig2.textureAnisotropyAvailable = true;
	}

	// VAO and VBO
	// made required in OpenGL 3.0

	LOAD_EXTENSION( ExtFlag_REQUIRED | ExtFlag_CORE, ARB_half_float_vertex );

	// made required in OpenGL 3.0
	LOAD_EXTENSION( ExtFlag_REQUIRED | ExtFlag_CORE, ARB_framebuffer_object );

	// FBO
	glGetIntegerv( GL_MAX_RENDERBUFFER_SIZE, &glConfig2.maxRenderbufferSize );
	glGetIntegerv( GL_MAX_COLOR_ATTACHMENTS, &glConfig2.maxColorAttachments );

	// made required in OpenGL 1.5
	glConfig2.occlusionQueryAvailable = false;
	glConfig2.occlusionQueryBits = 0;
	if ( r_ext_occlusion_query->integer != 0 )
	{
		glConfig2.occlusionQueryAvailable = true;
		glGetQueryiv( GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &glConfig2.occlusionQueryBits );
	}

	// made required in OpenGL 2.0
	glConfig2.drawBuffersAvailable = false;
	if ( r_ext_draw_buffers->integer != 0 )
	{
		glGetIntegerv( GL_MAX_DRAW_BUFFERS, &glConfig2.maxDrawBuffers );
		glConfig2.drawBuffersAvailable = true;
	}

	{
		int formats = 0;

		glGetIntegerv( GL_NUM_PROGRAM_BINARY_FORMATS, &formats );

		if ( formats == 0 )
		{
			// No need for WithoutSuppression for something which can only be printed once per renderer restart.
			logger.Notice("...no program binary formats");
		}

		glConfig2.getProgramBinaryAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_NONE, ARB_get_program_binary, formats > 0 );
	}

	glConfig2.bufferStorageAvailable = false;
	glConfig2.bufferStorageAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_NONE, ARB_buffer_storage, r_arb_buffer_storage->integer > 0 );

	// made required since OpenGL 3.1
	glConfig2.uniformBufferObjectAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, ARB_uniform_buffer_object, r_arb_uniform_buffer_object->value );

	// made required in OpenGL 3.0
	glConfig2.mapBufferRangeAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, ARB_map_buffer_range, r_arb_map_buffer_range->value );

	// made required in OpenGL 3.2
	glConfig2.syncAvailable = LOAD_EXTENSION_WITH_TEST( ExtFlag_CORE, ARB_sync, r_arb_sync->value );

	GL_CheckErrors();
}

static const int R_MODE_FALLBACK = 3; // 640 * 480

/* Support code for GLimp_Init */

static void reportDriverType( bool force )
{
	static const char *const drivers[] = {
		"integrated", "stand-alone", "OpenGL 3+", "Mesa"
	};
	if (glConfig.driverType > glDriverType_t::GLDRV_UNKNOWN && (unsigned) glConfig.driverType < ARRAY_LEN( drivers ) )
	{
		logger.Notice("%s graphics driver class '%s'",
		           force ? "User has forced" : "Detected",
		           drivers[Util::ordinal(glConfig.driverType)] );
	}
}

static void reportHardwareType( bool force )
{
	static const char *const hardware[] = {
		"generic", "ATI R300"
	};
	if (glConfig.hardwareType > glHardwareType_t::GLHW_UNKNOWN && (unsigned) glConfig.hardwareType < ARRAY_LEN( hardware ) )
	{
		logger.Notice("%s graphics hardware class '%s'",
		           force ? "User has forced" : "Detected",
		           hardware[Util::ordinal(glConfig.hardwareType)] );
	}
}

/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
bool GLimp_Init()
{
	glConfig.driverType = glDriverType_t::GLDRV_ICD;

	r_sdlDriver = ri.Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
	r_allowResize = ri.Cvar_Get( "r_allowResize", "0", CVAR_LATCH );
	r_centerWindow = ri.Cvar_Get( "r_centerWindow", "0", 0 );
	r_displayIndex = ri.Cvar_Get( "r_displayIndex", "0", 0 );
	ri.Cvar_Get( "r_availableModes", "", CVAR_ROM );

	ri.Cmd_AddCommand( "minimize", GLimp_Minimize );

	if ( ri.Cvar_VariableIntegerValue( "com_abnormalExit" ) )
	{
		ri.Cvar_Set( "r_mode", va( "%d", R_MODE_FALLBACK ) );
		ri.Cvar_Set( "r_fullscreen", "0" );
		ri.Cvar_Set( "r_centerWindow", "0" );
		ri.Cvar_Set( "r_noBorder", "0" );
		ri.Cvar_Set( "com_abnormalExit", "0" );
	}

	// Create the window and set up the context
	if ( GLimp_StartDriverAndSetMode( r_mode->integer, r_fullscreen->integer, !r_noBorder->value ) )
	{
		goto success;
	}

	// Finally, try the default screen resolution
	if ( r_mode->integer != R_MODE_FALLBACK )
	{
		logger.Notice("Setting r_mode %d failed, falling back on r_mode %d", r_mode->integer, R_MODE_FALLBACK );

		if ( GLimp_StartDriverAndSetMode( R_MODE_FALLBACK, false, true ) )
		{
			goto success;
		}
	}

	// Nothing worked, give up
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	return false;

success:
	// These values force the UI to disable driver selection
	glConfig.hardwareType = glHardwareType_t::GLHW_GENERIC;

	// get our config strings
	Q_strncpyz( glConfig.vendor_string, ( char * ) glGetString( GL_VENDOR ), sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, ( char * ) glGetString( GL_RENDERER ), sizeof( glConfig.renderer_string ) );

	if ( *glConfig.renderer_string && glConfig.renderer_string[ strlen( glConfig.renderer_string ) - 1 ] == '\n' )
	{
		glConfig.renderer_string[ strlen( glConfig.renderer_string ) - 1 ] = 0;
	}

	Q_strncpyz( glConfig.version_string, ( char * ) glGetString( GL_VERSION ), sizeof( glConfig.version_string ) );

	glConfig2.glExtensionsString = std::string();

	if ( glConfig.driverType == glDriverType_t::GLDRV_OPENGL3 )
	{
		GLint numExts, i;

		glGetIntegerv( GL_NUM_EXTENSIONS, &numExts );

		logger.Debug( "Found %d OpenGL extensions.", numExts );

		std::string glExtensionsString = std::string();

		for ( i = 0; i < numExts; ++i )
		{
			char* s = ( char * ) glGetStringi( GL_EXTENSIONS, i );

			/* Check for errors when fetching string.

			> If an error is generated, glGetString returns 0. 
			> -- https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glGetString.xhtml */
			if ( s == nullptr )
			{
				logger.Warn( "Error when fetching OpenGL extension list." );
			}
			else
			{
				std::string extensionName = s;

				if ( i != 0 )
				{
					glExtensionsString += " ";
				}

				glExtensionsString += extensionName;
			}
		}

		logger.Debug( "OpenGL extensions found: %s", glExtensionsString );

		glConfig2.glExtensionsString = glExtensionsString;
	}
	else
	{
		char* extensions_string = ( char * ) glGetString( GL_EXTENSIONS );

		if ( extensions_string == nullptr )
		{
			logger.Warn( "Error when fetching OpenGL extension list." );
		}
		else
		{
			std::string glExtensionsString = extensions_string;

			int numExts = std::count(glExtensionsString.begin(), glExtensionsString.end(), ' ');

			logger.Debug( "Found %d OpenGL extensions.", numExts );

			logger.Debug( "OpenGL extensions found: %s", glExtensionsString );

			glConfig2.glExtensionsString = glExtensionsString;
		}
	}

	if ( Q_stristr( glConfig.renderer_string, "amd " ) ||
	     Q_stristr( glConfig.renderer_string, "ati " ) )
	{
		if ( glConfig.driverType != glDriverType_t::GLDRV_OPENGL3 )
		{
			glConfig.hardwareType = glHardwareType_t::GLHW_R300;
		}
	}

	reportDriverType( false );
	reportHardwareType( false );

	{ // allow overriding where the user really does know better
		cvar_t          *forceGL;
		glDriverType_t   driverType   = glDriverType_t::GLDRV_UNKNOWN;
		glHardwareType_t hardwareType = glHardwareType_t::GLHW_UNKNOWN;

		forceGL = ri.Cvar_Get( "r_glForceDriver", "", CVAR_LATCH );

		if      ( !Q_stricmp( forceGL->string, "icd" ))
		{
			driverType = glDriverType_t::GLDRV_ICD;
		}
		else if ( !Q_stricmp( forceGL->string, "standalone" ))
		{
			driverType = glDriverType_t::GLDRV_STANDALONE;
		}
		else if ( !Q_stricmp( forceGL->string, "opengl3" ))
		{
			driverType = glDriverType_t::GLDRV_OPENGL3;
		}

		forceGL = ri.Cvar_Get( "r_glForceHardware", "", CVAR_LATCH );

		if      ( !Q_stricmp( forceGL->string, "generic" ))
		{
			hardwareType = glHardwareType_t::GLHW_GENERIC;
		}
		else if ( !Q_stricmp( forceGL->string, "r300" ))
		{
			hardwareType = glHardwareType_t::GLHW_R300;
		}

		if ( driverType != glDriverType_t::GLDRV_UNKNOWN )
		{
			glConfig.driverType = driverType;
			reportDriverType( true );
		}

		if ( hardwareType != glHardwareType_t::GLHW_UNKNOWN )
		{
			glConfig.hardwareType = hardwareType;
			reportHardwareType( true );
		}
	}

	// initialize extensions
	GLimp_InitExtensions();

	// This depends on SDL_INIT_VIDEO, hence having it here
	ri.IN_Init( window );

	return true;
}

/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame()
{
	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		SDL_GL_SwapWindow( window );
	}
}

/*
===============
GLimp_HandleCvars

Responsible for handling cvars that change the window or OpenGL state,
should only be called by the main thread.
===============
*/
void GLimp_HandleCvars()
{
	if ( r_swapInterval->modified )
	{
		/* Set the swap interval for the OpenGL context.

		* -1 : adaptive sync
		* 0 : immediate update
		* 1 : generic sync, updates synchronized with the vertical refresh
		* N : generic sync occurring on Nth vertical refresh
		* -N : adaptive sync occurring on Nth vertical refresh

		For example if screen has 60 Hz refresh rate:

		* -1 will update the screen 60 times per second,
		  using adaptive sync if supported,
		* 0 will update the screen as soon as it can,
		* 1 will update the screen 60 times per second,
		* 2 will update the screen 30 times per second.
		* 3 will update the screen 20 times per second,
		* 4 will update the screen 15 times per second,
		* -4 will update the screen 15 times per second,
		  using adaptive sync if supported,

		About adaptive sync:

		> Some systems allow specifying -1 for the interval,
		> to enable adaptive vsync.
		> Adaptive vsync works the same as vsync, but if you've
		> already missed the vertical retrace for a given frame,
		> it swaps buffers immediately, which might be less
		> jarring for the user during occasional framerate drops.
		> -- https://wiki.libsdl.org/SDL_GL_SetSwapInterval

		About the accepted values:

		> A swap interval greater than 0 means that the GPU may force
		> the CPU to wait due to previously issued buffer swaps.
		> -- https://www.khronos.org/opengl/wiki/Swap_Interval

		> If <interval> is negative, the minimum number of video frames
		> between buffer swaps is the absolute value of <interval>.
		> -- https://www.khronos.org/registry/OpenGL/extensions/EXT/GLX_EXT_swap_control_tear.txt

		The max value is said to be implementation-dependent:

		> The current swap interval and implementation-dependent max
		> swap interval for a particular drawable can be obtained by
		> calling glXQueryDrawable with the attribute […]
		> -- https://www.khronos.org/registry/OpenGL/extensions/EXT/GLX_EXT_swap_control_tear.txt

		About how to deal with errors:

		> If an application requests adaptive vsync and the system
		> does not support it, this function will fail and return -1.
		> In such a case, you should probably retry the call with 1
		> for the interval.
		> -- https://wiki.libsdl.org/SDL_GL_SetSwapInterval

		Given what's written in Swap Interval Khronos page, setting r_finish
		to 1 or 0 to call or not call glFinish may impact the behaviour.
		See https://www.khronos.org/opengl/wiki/Swap_Interval#GPU_vs_CPU_synchronization

		According to the SDL documentation, only arguments from -1 to 1
		are allowed to SDL_GL_SetSwapInterval. But investigation of SDL
		internals shows that larger intervals should work on Linux and
		Windows. See https://github.com/DaemonEngine/Daemon/pull/497
		Only 0 and 1 work on Mac.

		5 and -5 are arbitrarily set as ceiling and floor value
		to prevent mistakes making the game unresponsive. */

		AssertCvarRange( r_swapInterval, -5, 5, true );

		R_SyncRenderThread();

		int sign = r_swapInterval->integer < 0 ? -1 : 1;
		int interval = std::abs( r_swapInterval->integer );

		while ( SDL_GL_SetSwapInterval( sign * interval ) == -1 )
		{
			if ( sign == -1 )
			{
				logger.Warn("Adaptive sync is unsupported, fallback to generic sync: %s", SDL_GetError() );
				sign = 1;
			}
			else
			{
				if ( interval > 1 )
				{
					logger.Warn("Sync interval %d is unsupported, fallback to 1: %s", interval, SDL_GetError() );
					interval = 1;
				}
				else if ( interval == 1 )
				{
					logger.Warn("Sync is unsupported, disabling sync: %s", SDL_GetError() );
					interval = 0;
				}
				else if ( interval == 0 )
				{
					logger.Warn("Can't disable sync, something is wrong: %s", SDL_GetError() );
					break;
				}
			}
		}

		r_swapInterval->modified = false;
	}

	if ( r_fullscreen->modified )
	{
		int sdlToggled = false;
		bool needToToggle = true;
		bool fullscreen = !!( SDL_GetWindowFlags( window ) & SDL_WINDOW_FULLSCREEN );

		if ( r_fullscreen->integer && ri.Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			logger.Notice("Fullscreen not allowed with in_nograb 1" );
			ri.Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = false;
		}

		// Is the state we want different from the current state?
		needToToggle = !!r_fullscreen->integer != fullscreen;

		if ( needToToggle )
		{
			Uint32 flags = r_fullscreen->integer == 0 ? 0 : SDL_WINDOW_FULLSCREEN;
			sdlToggled = SDL_SetWindowFullscreen( window, flags );

			if ( sdlToggled < 0 )
			{
				Cmd::BufferCommandText("vid_restart");
			}

			ri.IN_Restart();
		}

		r_fullscreen->modified = false;
	}

	if ( r_noBorder->modified )
	{
		SDL_bool bordered = r_noBorder->integer == 0 ? SDL_TRUE : SDL_FALSE;
		SDL_SetWindowBordered( window, bordered );

		r_noBorder->modified = false;
	}

	// TODO: Update r_allowResize using SDL_SetWindowResizable when we have SDL 2.0.5
}

void GLimp_LogComment( const char *comment )
{
	static char buf[ 4096 ];

	if ( r_logFile->integer && GLEW_ARB_debug_output )
	{
		// copy string and ensure it has a trailing '\0'
		Q_strncpyz( buf, comment, sizeof( buf ) );

		glDebugMessageInsertARB( GL_DEBUG_SOURCE_APPLICATION_ARB,
					 GL_DEBUG_TYPE_OTHER_ARB,
					 0,
					 GL_DEBUG_SEVERITY_MEDIUM_ARB,
					 strlen( buf ), buf );
	}
}
