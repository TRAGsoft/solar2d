//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////


#include "Core/Rtt_Build.h"

#include "Rtt_LuaContext.h"

#include "Display/Rtt_Display.h"
#include "Display/Rtt_LuaLibDisplay.h"
#include "Display/Rtt_LuaLibGraphics.h"
#include "Display/Rtt_StageObject.h"
#include "Rtt_Archive.h"
#include "Rtt_Event.h"
#include "Rtt_LuaAux.h"
#include "Rtt_LuaFile.h"
#include "Rtt_LuaFrameworks.h"
#include "Rtt_LuaGCInhibitor.h"
#include "Rtt_LuaCoronaBaseLib.h"
#include "Rtt_LuaLibCrypto.h"
#if defined ( Rtt_USE_ALMIXER )
#include "Rtt_LuaLibOpenAL.h"
#endif
#if defined ( Rtt_USE_OPENSLES )
#include "Rtt_LuaLibOpenSLES.h"
#endif
#include "Rtt_LuaLibNative.h"
#if defined( Rtt_SQLITE )
#include "Rtt_LuaLibSQLite.h"
#endif
#include "Rtt_LuaLibSystem.h"
#include "Rtt_LuaUserdataProxy.h"
#include "Rtt_MPlatform.h"
#include "Rtt_PlatformData.h"
#include "Rtt_Runtime.h"
#include "Rtt_MRuntimeDelegate.h"
#include "Core/Rtt_String.h"

#ifdef Rtt_AUTHORING_SIMULATOR
	#include "Rtt_SimulatorControl.h"
#endif

#include <string.h>
#include <string>
#include <algorithm>
#include <signal.h>

#include "Rtt_Lua.h"

#include "CoronaLibrary.h"

// ----------------------------------------------------------------------------

#ifdef Rtt_NXS_ENV
	int luaopen_network(lua_State* L);
	extern "C" {
		int luaopen_lfs(lua_State* L);
		int luaopen_plugin_websockets(lua_State* L);
	}
#endif

#ifdef Rtt_LINUX_ENV
	extern "C" {
		int luaopen_network(lua_State* L);
		int luaopen_lfs(lua_State* L);
		int luaopen_socket_core(lua_State* L);
		int luaopen_mime_core(lua_State* L);
	}
#endif

namespace Rtt
{

#ifdef Rtt_NXS_ENV
	int luaload_nnNativeAlert(lua_State* L);
#endif

#ifdef Rtt_LINUX_ENV
	int luaload_luasocket_socket(lua_State* L);
	int luaload_luasocket_ftp(lua_State* L);
	int luaload_luasocket_headers(lua_State* L);
	int luaload_luasocket_http(lua_State* L);
	int luaload_luasocket_mbox(lua_State* L);
	int luaload_luasocket_smtp(lua_State* L);
	int luaload_luasocket_tp(lua_State* L);
	int luaload_luasocket_url(lua_State* L);
	int luaload_luasocket_mime(lua_State* L);
	int luaload_luasocket_ltn12(lua_State* L);
#endif

// ----------------------------------------------------------------------------

#ifdef Rtt_DEBUG
void
LuaContext::stackdump(lua_State* L)
{
	Rtt_LUA_STACK_GUARD( L );
	
	int top = lua_gettop(L);
    int i;
	
	printf("=== Lua Stack Dump: %d frames ===\n",top);
	
	for (i = 1; i <= top; i++)
	{  /* repeat for each level */
		int t = lua_type(L, i);
		printf("[%d]: ", i);
		switch (t)
		{
			case LUA_TSTRING:  /* strings */
				printf("string: '%s'\n", lua_tostring(L, i));
				break;
			case LUA_TBOOLEAN:  /* booleans */
				printf("boolean %s\n",lua_toboolean(L, i) ? "true" : "false");
				break;
			case LUA_TNUMBER:  /* numbers */
				printf("number: %g\n", lua_tonumber(L, i));
				break;
			case LUA_TTABLE:  /* tables */
				/* table is in the stack at index 't' */
				lua_pushnil(L);  /* first key */
				printf("table:\n");
				while (lua_next(L, i) != 0)
				{
					/* uses 'key' (at index -2) and 'value' (at index -1) */
					printf("    [%s] - %s\n",
						   lua_tostring(L, -2),
						   lua_typename(L, lua_type(L, -1)));
					/* removes 'value'; keeps 'key' for next iteration */
					lua_pop(L, 1);
				}
				break;
			default:  /* other values */
				printf("%s: %p\n", lua_typename(L, t), lua_topointer(L, i));
				break;
		}
	}
	printf("\n");  /* end the listing */
	
	lua_settop(L, top);
}
#endif // Rtt_DEBUG

// ----------------------------------------------------------------------------

void*
LuaContext::Alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	if ( 0 == nsize )
	{
		Rtt_CNTX_FREE( (Rtt_Allocator*)ud, ptr );
		return NULL;
	}
	else if ( 0 == osize )
	{
		return Rtt_CNTX_MALLOC( (Rtt_Allocator*)ud, nsize );
	}
	else if ( nsize <= osize )
	{
		return ptr;
	}
	else
	{
		return Rtt_CNTX_REALLOC( (Rtt_Allocator*)ud, ptr, nsize );
	}
}

int
LuaContext::Panic( ::lua_State* L )
{
	Rtt_TRACE( ( "PANIC: unprotected error in call to Lua API (%s)\n", lua_tostring(L, -1) ) );
#if defined( Rtt_DEBUG )
	LuaContext::stackdump(L);
#endif
	Rtt_ASSERT_NOT_REACHED( );

	return 0;
}

// ----------------------------------------------------------------------------

#if defined( Rtt_DEBUG )

static lua_State *sLuaContext = NULL;

void
LuaContext::lstop( lua_State* L, lua_Debug *ar )
{
	(void)ar;  /* unused arg. */
	lua_sethook(L, NULL, 0, 0);
	luaL_error(L, "interrupted!");
}

static void laction( int i )
{
	// if another SIGINT happens before lstop, terminate process (default action)
	signal( i, SIG_DFL );
	
	lua_sethook( sLuaContext, LuaContext::lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1 );
}

#endif // Rtt_DEBUG

static const char kCapturedErrorMetatable[] = "Solar2D.CapturedError";

static void PushStackFrames( lua_State *L, lua_State *thread, int firstLevel )
{
	std::string resourceRoot;
	if ( LuaContext::HasRuntime( L ) )
	{
		const MPlatform& platform = LuaContext::GetPlatform( L );
		String path( &platform.GetAllocator() );
		platform.PathForFile( NULL, MPlatform::kResourceDir, MPlatform::kDefaultPathFlags, path );
		if ( path.GetString() )
		{
			resourceRoot = path.GetString();
			std::replace( resourceRoot.begin(), resourceRoot.end(), '\\', '/' );
			if ( !resourceRoot.empty() && resourceRoot[resourceRoot.size() - 1] != '/' )
			{
				resourceRoot += '/';
			}
		}
	}

	lua_newtable( L );
	lua_Debug frame;
	for ( int index = 1; index <= 128 && lua_getstack( thread, firstLevel + index - 1, &frame ); index++ )
	{
		if ( !lua_getinfo( thread, "Snl", &frame ) ) { break; }
		lua_newtable( L );
		lua_pushstring( L, frame.source );
		lua_setfield( L, -2, "source" );
		lua_pushstring( L, frame.what );
		lua_setfield( L, -2, "what" );
		lua_pushstring( L, frame.name );
		lua_setfield( L, -2, "name" );
		if ( frame.currentline > 0 )
		{
			lua_pushinteger( L, frame.currentline );
			lua_setfield( L, -2, "currentline" );
		}
		if ( frame.source && frame.source[0] == '@' )
		{
			std::string filename( frame.source + 1 );
			std::replace( filename.begin(), filename.end(), '\\', '/' );
			if ( !resourceRoot.empty() && filename.compare( 0, resourceRoot.size(), resourceRoot ) == 0 )
			{
				filename.erase( 0, resourceRoot.size() );
			}
			lua_pushlstring( L, filename.data(), filename.size() );
			lua_setfield( L, -2, "filename" );
		}
		lua_rawseti( L, -2, index );

	}
}

static int CapturedErrorToString( lua_State *L )
{
	lua_getfield( L, 1, "errorMessage" );
	lua_getfield( L, 1, "stackTrace" );
	lua_concat( L, 2 );
	return 1;
}

static void PushCapturedError( lua_State *L, int errorIndex, lua_State *thread, int firstLevel )
{
	if ( lua_getmetatable( L, errorIndex ) )
	{
		luaL_getmetatable( L, kCapturedErrorMetatable );
		bool captured = lua_rawequal( L, -1, -2 ) != 0;
		lua_pop( L, 2 );
		if ( captured )
		{
			lua_pushvalue( L, errorIndex );
			return;
		}
	}

	lua_newtable( L );
	if ( lua_isstring( L, errorIndex ) )
	{
		lua_pushvalue( L, errorIndex );
		lua_tostring( L, -1 );
	}
	else
	{
		lua_pushfstring( L, "Lua error object (%s)", luaL_typename( L, errorIndex ) );
	}
	lua_setfield( L, -2, "errorMessage" );

	PushStackFrames( L, thread, firstLevel );
	lua_setfield( L, -2, "stackFrames" );
	lua_pushliteral( L, "\n" );
	lua_traceback( L, thread, firstLevel );
	lua_concat( L, 2 );
	lua_setfield( L, -2, "stackTrace" );
	if ( luaL_newmetatable( L, kCapturedErrorMetatable ) )
	{
		lua_pushcfunction( L, CapturedErrorToString );
		lua_setfield( L, -2, "__tostring" );
	}
	lua_setmetatable( L, -2 );
}

int
LuaContext::CaptureStackTrace( lua_State *L )
{
	int level = luaL_optint( L, 1, 1 );
	luaL_argcheck( L, level >= 0, 1, "level must be non-negative" );
	PushStackFrames( L, L, level );
	return 1;
}

int
LuaContext::CaptureXpcallError( lua_State *L )
{
	luaL_checkany( L, 1 );
	PushCapturedError( L, 1, L, 1 );
	return 1;
}

int
LuaContext::CaptureCoroutineError( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTHREAD );
	luaL_checkany( L, 2 );
	lua_State *thread = lua_tothread( L, 1 );
	PushCapturedError( L, 2, thread, 0 );
	return 1;
}

bool
LuaContext::callUnhandledErrorHandler( lua_State* L, const char *message, const char *stacktrace, int errorIndex )
{
	// Do not continue if the given Lua state does not belong to a Corona runtime.
	// If this is the case, then there is no runtime to dispatch an event to.
	if ( LuaContext::HasRuntime(L) == false )
	{
		return false;
	}

	Rtt_LUA_STACK_GUARD( L );
	
	int top = lua_gettop(L);
	int bail = true;
	
	CoronaLuaNewEvent( L, "unhandledError" );
	
	// Put the error message and stacktrace in the table that's handed to the handler
	lua_pushstring( L, message );
	lua_setfield( L, -2, "errorMessage" );
	lua_pushstring( L, stacktrace );
	lua_setfield( L, -2, "stackTrace" );

	lua_getfield( L, errorIndex, "stackFrames" );
	lua_setfield( L, -2, "stackFrames" );

	Lua::DispatchRuntimeEvent( L, 1 );
	
#if defined( Rtt_DEBUG )
	// LuaContext::stackdump(L);
#endif
	
	if (lua_isboolean( L, -1 ))
	{
		int result = lua_toboolean( L, -1 );
		
		// Rtt_TRACE_SIM( ("callUnhandledErrorHandler: got %d from stack", result ));
		
		if (result)
		{
			// They returned "true" ... carry on
			bail = false;
		}
	}
	
	lua_settop(L, top);
	
	return bail;
}

static const char kLuaRuntimeError[] = "Runtime error";
static const char kLuaSyntaxError[] = "Syntax error";
static const char kLuaMemoryError[] = "Out of Memory error";
static const char kLuaGenericError[] = "Generic error";
	
int
LuaContext::traceback( lua_State* L )
{
	return LuaContext::handleError( L, kLuaRuntimeError, true );
}

int
LuaContext::handleError( lua_State* L, const char *errorType, bool callErrorListener )
{
	const int originalTop = lua_gettop( L );
	PushCapturedError( L, lua_gettop( L ), L, 1 );
	const int errorIndex = lua_gettop( L );
	lua_getfield( L, errorIndex, "errorMessage" );
	const char *briefMessage = lua_tostring( L, -1 );
	lua_getfield( L, errorIndex, "stackTrace" );
	const char *stackTrace = lua_tostring( L, -1 );
	lua_pushfstring( L, "%s%s", briefMessage, stackTrace );
	const int resultIndex = lua_gettop( L );

	CORONA_LOG_ERROR( "%s\n%s%s", errorType, briefMessage, stackTrace );

	// Preventing recursive custom error handler call! Errors in error handle will not be handled by itself.
	// Application environment is used for this
	if(Self::HasRuntime(L))
	{
		Runtime* runtime = Self::GetRuntime(L);
		if (runtime->fErrorHandlerRecursionGuard == true)
		{
			CORONA_LOG_ERROR("Preventing recursive custom error handler call! Errors in error handle will not be handled by itself.\n");
#ifdef Rtt_AUTHORING_SIMULATOR
			SimulatorControl::HaltOnRuntimeError( *runtime );
#endif
			lua_replace( L, originalTop + 1 );
			lua_settop( L, originalTop + 1 );
			return 1;
		}
#ifdef Rtt_AUTHORING_SIMULATOR
		SimulatorControl::RecordRuntimeError(
			*runtime, L, errorType, briefMessage, stackTrace );
#endif
		runtime->fErrorHandlerRecursionGuard = true;
	}

	int bail = true;
	
	// We call the unhandledError listener (if defined) whether or not we're showing the runtime error popups
	if ( callErrorListener )
	{
		bail = LuaContext::callUnhandledErrorHandler( L, briefMessage, stackTrace, errorIndex );
	}

#ifdef Rtt_AUTHORING_SIMULATOR
	if ( bail && Self::HasRuntime( L ) )
	{
		SimulatorControl::HaltOnRuntimeError( *Self::GetRuntime( L ) );
	}
#endif
	
	// TODO: this ought to be in a shared header somewhere
	const char *javaStackTraceSignature = "\nJava Stack Trace:";
	bool isJavaError = (strstr(briefMessage, javaStackTraceSignature) != NULL);
    
    if (Self::HasRuntime( L ))
    {
        Runtime *runtime = Self::GetRuntime( L );
        // Heuristic test to determine if the Lua with the error has debug symbols
#ifndef Rtt_AUTHORING_SIMULATOR
        lua_Debug ar;
        bool luaDebugAvailable = lua_getstack(L, 1, &ar) == 1 && lua_getinfo(L, "l", &ar) && ar.currentline != 0;
#else
        bool luaDebugAvailable = true; // debug is always available in the Simulator
#endif
        
         // If the app set an explicit value for "showRuntimeErrors" then use it, otherwise show errors if code is built with debug symbols (always show syntax errors)
		bool showRuntimeError = luaDebugAvailable || (strcmp(errorType, "Syntax error") == 0);
        if (isJavaError || (bail && showRuntimeError))
        {
            lua_CFunction errfunc = Lua::GetErrorHandler( NULL );
            
            // Call the Lua error handler if it's defined (we define it on Android to display the crash dialog)
            if ( errfunc != NULL )
            {
                (*errfunc)( L );
            }
            else
            {
                LuaContext::GetRuntime( L )->Platform().RuntimeErrorNotification( errorType, briefMessage, stackTrace );
            }
        }
        
        // Apparently this is used for automated testing (set application.content.exitOnError in config.lua)
        if (runtime->IsProperty(Runtime::kUseExitOnErrorHandler))
        {
            if (lua_isstring(L, 1))  // safety check
            {
                Rtt_LogException( "%s", lua_tostring(L, 1));
            }
            Rtt_LogException( "Exiting Application due to trapped Lua script error.");
            
            exit(0);
        }
		runtime->fErrorHandlerRecursionGuard = false;
    }
	
	lua_settop( L, resultIndex );
	lua_replace( L, originalTop + 1 );
	lua_settop( L, originalTop + 1 );
	return 1;
}

void
LuaContext::l_message( const char *pname, const char *msg )
{
	if (pname) Rtt_TRACE_SIM( ( "%s: ", pname ) );
	Rtt_TRACE_SIM( ( "%s\n", msg ) );
}

static int report( lua_State* L, int status )
{
	if (status && !lua_isnil(L, -1)) {
		const char *msg = lua_tostring(L, -1);
		if (msg == NULL) msg = "(error object is not a string)";

		const char *errName = kLuaGenericError;
		switch( status )
		{
			case LUA_ERRRUN:
				errName = kLuaRuntimeError;
				break;
			case LUA_ERRSYNTAX:
				errName = kLuaSyntaxError;
				break;
			case LUA_ERRMEM:
				errName = kLuaMemoryError;
				break;
		}

		LuaContext::handleError( L, errName, (status == LUA_ERRRUN) );

		lua_pop(L, 1);
	}
	return status;
}


// ----------------------------------------------------------------------------

int
LuaContext::pcall_exit_on_error( lua_State* L )
{
	int exit_code = 0;
	traceback(L);
	
	if (lua_isstring(L, 1))  /* safety check */
	{
		fprintf(stderr, "%s", lua_tostring(L, 1));
	}
	fprintf(stderr, "Exiting Application due to trapped Lua script error.");
	
	exit(exit_code);
	return 1; /* Make compiler happy */
}

lua_CFunction
LuaContext::GetDefaultErrorHandler()
{
	lua_CFunction result = NULL;

	result = & traceback;

	return result;
}

// ----------------------------------------------------------------------------

void
LuaContext::InitializeLuaPath( lua_State* L, const MPlatform& platform )
{
	// Note 1: On Win32, we also push paths to Lua's package path so that we can support multiple runtime instances.
	// Note 2: We should actually do this on *all* plaforms so that we can support multiple Corona instances as well.
#if (defined( Rtt_WIN_ENV ) && ( _MSC_VER >= 1800 ))
#	define Rtt_LUA_CONTEXT_SET_PACKAGE_PATH 
#endif

	Rtt_ASSERT( 0 == lua_gettop( L ) );

	int numPushed = 0;

#if defined( Rtt_LUA_CONTEXT_SET_PACKAGE_PATH )
	// Push the Lua package table to the top of the stack, if currently available.
	// The intent is to add the paths below to Lua's package path.
	lua_getglobal(L, "package");
	int luaPackageTableIndex = lua_gettop(L);
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		return;
	}
#endif

	// Default search paths
	// For platform-specific changes, DO NOT MODIFY this.  Instead, set the
	// environment variable BEFORE this --- note we call setenv with the 
	// overwrite param set to false.
	String absoluteBase( & platform.GetAllocator() );
	platform.PathForFile( NULL, MPlatform::kSystemResourceDir, MPlatform::kDefaultPathFlags, absoluteBase );

	String pluginsBase( & platform.GetAllocator() );
	platform.PathForFile( NULL, MPlatform::kPluginsDir, MPlatform::kDefaultPathFlags, pluginsBase );
	const char *pluginsBaseStr = pluginsBase.GetString();

#if defined( Rtt_MAC_ENV ) && ! defined( Rtt_AUTHORING_SIMULATOR )
	// kSystemResourceDir and kResourceDir are the same for CoronaSDK for OS X apps so don't add both
	String sysResourceDir( & platform.GetAllocator() );
	platform.PathForFile( NULL, MPlatform::kSystemResourceDir, MPlatform::kDefaultPathFlags, sysResourceDir );
	lua_pushfstring( L, "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_SCRIPT_FILE_EXTENSION LUA_PATHSEP,
					absoluteBase.GetString() );
#else
		// Simulator can look in both system and user-selected resource directory
		// (e.g. the one they choose in the Open dialog). It only loads .lua script files.
		// Looks in the user resource dir first.
		String absoluteBaseUser( & platform.GetAllocator() );
		platform.PathForFile( NULL, MPlatform::kResourceDir, MPlatform::kDefaultPathFlags, absoluteBaseUser );
		lua_pushfstring( L, "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_SCRIPT_FILE_EXTENSION LUA_PATHSEP
							"%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_SCRIPT_FILE_EXTENSION LUA_PATHSEP, 
							absoluteBaseUser.GetString(), absoluteBase.GetString() );
#endif
	++numPushed;

	// Prepend pluginsBaseDir
	if ( pluginsBaseStr )
	{
		lua_pushfstring( L, "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_SCRIPT_FILE_EXTENSION LUA_PATHSEP "%s",
						pluginsBaseStr, lua_tostring( L, -1 ) );
		++numPushed;

#if defined( Rtt_MAC_ENV ) && ! defined( Rtt_AUTHORING_SIMULATOR )
		// For CoronaSDK for OS X apps, also add ".lu" to the plugin search
		lua_pushfstring( L, "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_OBJECT_FILE_EXTENSION LUA_PATHSEP "%s",
						pluginsBaseStr, lua_tostring( L, -1 ) );
		++numPushed;
#endif
	}

#if defined( Rtt_LUA_CONTEXT_SET_PACKAGE_PATH )
	lua_getfield( L, luaPackageTableIndex, "path" );
	lua_concat( L, 2 );
	lua_setfield(L, luaPackageTableIndex, "path");
	numPushed--;		// Setting the above field pops off the last string.
#elif defined( Rtt_WIN_ENV )
	_putenv_s( LUA_PATH, lua_tostring( L, -1 ) );
#else
	Rtt_TRACE(( "LUA_PATH = %s\n", lua_tostring( L, -1 ) ));
	setenv( LUA_PATH, lua_tostring( L, -1 ), 1 );
#endif

	// Set "cpath" for plugins
	//
	//   -- Example for 'dll'. Similar for .dylib or .so
	//   package.cpath =
	//      "?.dll" .. ";" .. system.pathForFile( system.SystemResourceDirectory ) .. "/?.dll"
	//
	// NOTE: Assumes C modules reside in system resource directory
#if defined(Rtt_NXS_ENV)
	lua_pushfstring(L,
		"%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_C_MODULE_FILE_EXTENSION LUA_PATHSEP,
		absoluteBase.GetString());
	++numPushed;
#else
	lua_pushfstring( L,
		"." LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_C_MODULE_FILE_EXTENSION
		LUA_PATHSEP "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_C_MODULE_FILE_EXTENSION LUA_PATHSEP,
			absoluteBase.GetString() );
	++numPushed;
#endif

#if defined( Rtt_MAC_ENV ) && ! defined( Rtt_AUTHORING_SIMULATOR )
	const char *coronaCardsFrameworksDir = "../../Frameworks/CoronaCards.framework/Versions/A/Frameworks";
	lua_pushfstring( L, "%s" LUA_DIRSEP "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_C_MODULE_FILE_EXTENSION LUA_PATHSEP "%s",
					sysResourceDir.GetString(), coronaCardsFrameworksDir, lua_tostring( L, -1 ) );
	++numPushed;
#endif
#if defined( Rtt_MAC_ENV )
    std::string res = absoluteBase.GetString();
    res = res.substr(0, res.find_last_of("/"));
	lua_pushfstring( L, "%s" LUA_DIRSEP "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_C_MODULE_FILE_EXTENSION LUA_PATHSEP "%s",
					res.c_str(), "Frameworks", lua_tostring( L, -1 ) );
	++numPushed;
#endif

	// Prepend pluginsBaseDir
	if ( pluginsBaseStr )
	{
		lua_pushfstring( L, "%s" LUA_DIRSEP LUA_PATH_MARK "." Rtt_LUA_C_MODULE_FILE_EXTENSION LUA_PATHSEP
							"%s", pluginsBaseStr, lua_tostring( L, -1 ) );
		++numPushed;
	}

#if defined( Rtt_LUA_CONTEXT_SET_PACKAGE_PATH )
	lua_getfield( L, luaPackageTableIndex, "cpath" );
	lua_concat( L, 2 );
	lua_setfield( L, luaPackageTableIndex, "cpath" );
	numPushed--;		// Setting the above field pops off the last string.
#elif defined( Rtt_WIN_ENV )
	_putenv_s( LUA_CPATH, lua_tostring( L, -1 ) );
#else
	Rtt_TRACE(( "LUA_CPATH = %s\n", lua_tostring( L, -1 ) ));
	setenv( LUA_CPATH, lua_tostring( L, -1 ), 1 );
#endif

	// Pop the above strings off the Lua stack.
	if ( numPushed > 0 )
	{
		lua_pop( L, numPushed );
	}

#if defined( Rtt_LUA_CONTEXT_SET_PACKAGE_PATH )
	// Pop the "package" table off the Lua stack.
	lua_pop( L, 1 );
#endif

	Rtt_ASSERT( 0 == lua_gettop( L ) );
}	

void
LuaContext::InitializeLibraries( lua_State* L, Runtime *runtime )
{
	LuaGCInhibitor inhibitor( L );
	Self::InitializeLuaCore( L );
	Self::InitializeRttCore( L, runtime );
}

/**
 * Register libraries. There are two kinds of libraries registered here:
 *		- Built in Lua libraries
 *		- Optional libraries. These are only loaded if someone requires them, so this is preferred
 *			for pieces of code that don't always have to be loaded.
 */
void
LuaContext::InitializeLuaCore( lua_State* L )
{
	luaL_openlibs(L);  /* open libraries */

	/* initialize these immediately */
	const luaL_Reg luaInitializeLibsImmediately[] =
	{
		{ "coronabaselib", luaopen_coronabaselib },

		{NULL, NULL}
	};

	for (const luaL_Reg *lib = luaInitializeLibsImmediately; lib->func; lib++) {
		lua_pushcfunction(L, lib->func);
		lua_pushstring(L, lib->name);
		lua_call(L, 1, 0);
	}
	
	// NOTE:
	// -------------------------------------------------------------------------
	// Do NOT add modules in here.
	// You probably should use RuntimeDelegate instead.
	// -------------------------------------------------------------------------

	// Static Modules
	const luaL_Reg luaStaticModules[] =
	{
		{ "lpeg", luaopen_lpeg },
		{ "re", Lua::Open< luaload_re > },
		{ "crypto", LuaLibCrypto::Open },
#if defined( Rtt_SQLITE )
		{ "sqlite3", LuaLibSQLite::Open },
#endif
		{ "easing", Lua::Open< luaload_easing> },
		{ "dkjson", Lua::Open< luaload_dkjson > },
		{ "json", Lua::Open< luaload_json > },
#if defined(Rtt_LINUX_ENV)
		{ "network", luaopen_network },
		{ "lfs", luaopen_lfs },
		{ "socket.core", luaopen_socket_core },
		{ "socket", Lua::Open< luaload_luasocket_socket >  },
		{ "socket.ftp", Lua::Open< luaload_luasocket_ftp > },
		{ "socket.headers", Lua::Open< luaload_luasocket_headers > },
		{ "socket.http", Lua::Open< luaload_luasocket_http > },
		{ "socket.mbox", Lua::Open< luaload_luasocket_mbox > },
		{ "socket.smtp", Lua::Open< luaload_luasocket_smtp > },
		{ "socket.tp", Lua::Open< luaload_luasocket_tp > },
		{ "socket.url", Lua::Open< luaload_luasocket_url > },
		{ "mime.core", luaopen_mime_core },
		{ "mime", Lua::Open< luaload_luasocket_mime > },
		{ "ltn12", Lua::Open< luaload_luasocket_ltn12 > },

#endif

#if defined(Rtt_NXS_ENV)
		{ "network", luaopen_network },
		{ "lfs", luaopen_lfs },
		{ "websockets", luaopen_plugin_websockets },
		{ "nnNativeAlert", Lua::Open< luaload_nnNativeAlert > },
	
#endif

		{NULL, NULL}
	};

	CoronaLibraryProviderDefine( L, "native.popup" );

	// Preload modules to eliminate dependency
	lua_getfield( L, LUA_GLOBALSINDEX, "package" );
	lua_getfield( L, -1, "preload" );
	for ( const luaL_Reg *lib = luaStaticModules; lib->func; lib++ )
	{
		lua_pushcfunction( L, lib->func );
		lua_setfield( L, -2, lib->name );
	}

	lua_pop( L, 2 );
}

/**
 * Register Rtt libraries. These are always loaded.
 */
void
LuaContext::InitializeRttCore( lua_State* L, Runtime *runtime )
{
	// Init Proxy
	LuaProxy::Initialize( L );
	LuaProxyConstant::Initialize( L );

	// Init Userdata Proxy
	LuaUserdataProxy::Initialize( L );

	// Init core classes
	CoronaLibraryInitialize( L );

	// Init built-in libs
	LuaLibSystem::Initialize( L );
	LuaLibDisplay::Initialize( L, runtime->GetDisplay() );
#if defined ( Rtt_USE_ALMIXER )
	LuaLibOpenAL::Initialize( L );
#endif
#if defined ( Rtt_USE_OPENSLES )
	LuaLibOpenSLES::Initialize( L );
#endif
	LuaLibNative::Initialize( L );
	LuaLibGraphics::Initialize( L, runtime->GetDisplay() );

	// Init add'l GC metatables
	PlatformData::Initialize( L );

	Rtt_ASSERT( 0 == lua_gettop( L ) );
}

void
LuaContext::RegisterModuleLoader( lua_State *L, const char *name, lua_CFunction loader, int nupvalues )
{
	Lua::RegisterModuleLoader( L, name, loader, nupvalues );
}

void
LuaContext::RegisterModuleLoaders( lua_State *L, const luaL_Reg moduleLoaders[], int nupvalues )
{
	Lua::RegisterModuleLoaders( L, moduleLoaders, nupvalues );
}

int
LuaContext::DoCall( lua_State* L, int narg, int nresults )
{
	int base = lua_gettop(L) - narg;

	int errfunc = 0; // index of errfunc. Default to 0 (i.e. none)
	lua_CFunction callback = GetDefaultErrorHandler();
	if ( callback )
	{
		lua_pushcfunction(L, callback);  /* push errfunc */
		lua_insert(L, base);  /* put it under chunk and args */
		errfunc = base;
	}

#if defined( Rtt_DEBUG ) && !defined(Rtt_NXS_ENV)
	signal(SIGINT, laction);
#endif

	// The actual call
	int status = lua_pcall(L, narg, nresults, errfunc);

#if defined( Rtt_DEBUG ) && !defined(Rtt_NXS_ENV)
	signal(SIGINT, SIG_DFL);
#endif

    if ( 0 != errfunc )
    {
        lua_remove(L, errfunc);  /* remove errfunc */
    }
	/* force a complete garbage collection in case of errors */
	if (status != 0)
	{
		// If there is no error function, report the message returned by Lua.
		if ( 0 == errfunc )
		{
			Rtt_LogException("Lua Runtime Error: lua_pcall failed with status: %d, error message is: %s\n", status, lua_tostring( L, -1) );
		}
		
		lua_gc(L, LUA_GCCOLLECT, 0);

#ifndef Rtt_AUTHORING_SIMULATOR
		// Trigger the unhandledError mechanism
		//report( L, status );
#endif // ! Rtt_AUTHORING_SIMULATOR
	}

	return status;
}

#ifdef Rtt_AUTHORING_SIMULATOR
int
LuaContext::DoCPCall( lua_State* L, lua_CFunction func, void* ud )
{
	int status = lua_cpcall(L, func, ud );

	return report( L, status );
}
#endif // Rtt_AUTHORING_SIMULATOR

int
LuaContext::DoBuffer( lua_State *L, lua_CFunction loader, lua_CFunction pushargs )
{
	Rtt_ASSERT( loader );

	int status = loader( L );
	int narg = ( pushargs ? pushargs( L ) : 0 );

	// Load and invoke Lua bytecode
	if ( Rtt_VERIFY( 0 == status ) )
	{
		status = DoCall( L, narg, 0 ); Rtt_ASSERT( 0 == status );
		report( L, status );
	}

	return status;
}

int
LuaContext::DoFile( lua_State *L, const char* file, int narg, bool clear )
{
	int status = luaL_loadfile( L, file );
	if ( 0 == status )
	{
		int base = lua_gettop( L ) - narg;
		lua_insert( L, base ); // move chunk underneath args

		status = DoCall( L, narg, ( clear ? 0 : LUA_MULTRET ) );
	}

	// Runtime errors have already been reported so we should not report them again
	if (status != LUA_ERRRUN)
	{
		return report( L, status );
	}
	else
	{
		return status;
	}
}

// ----------------------------------------------------------------------------	

class LuaContextUserdata
{
	public:
		LuaContextUserdata( Rtt_Allocator* pAllocator, const MPlatform& platform, Runtime *runtime );

	public:
		void Initialize( LuaContext *owner );

	public:
		Rtt_Allocator* GetAllocator() const { return fAllocator; }
		const MPlatform& GetPlatform() const { return fPlatform; }
		Runtime* GetRuntime() const { return fRuntime; }
		LuaContext* GetOwner() const { return fOwner; }

	// Weak references. Does NOT own these.
	private:
		Rtt_Allocator *fAllocator;
		const MPlatform& fPlatform;
		Runtime *fRuntime;
		LuaContext *fOwner;
};

LuaContextUserdata::LuaContextUserdata( Rtt_Allocator* pAllocator, const MPlatform& platform, Runtime *runtime )
:	fAllocator( pAllocator ),
	fPlatform( platform ),
	fRuntime( runtime ),
	fOwner( NULL )
{
}

void
LuaContextUserdata::Initialize( LuaContext *owner )
{
	if ( Rtt_VERIFY( NULL == fOwner ) )
	{
		fOwner = owner;
	}
}

// ----------------------------------------------------------------------------	

LuaContext*
LuaContext::New( Rtt_Allocator* pAllocator, const MPlatform& platform, Runtime *runtime )
{
	LuaContext* result = NULL;

	LuaContextUserdata *ud = Rtt_NEW( pAllocator, LuaContextUserdata( pAllocator, platform, runtime ) );

	::lua_State* L = lua_newstate( LuaContext::Alloc, ud );
	Lua::Initialize( L ); // Cache main Lua state for plugins

	if ( ud && Rtt_VERIFY( L ) )
	{
		result = Rtt_NEW( pAllocator, LuaContext( L ) ); Rtt_ASSERT( result );
		ud->Initialize( result );
	}
	else
	{
		// Error
		Rtt_DELETE( ud );
	}

	Lua::SetCriticalSection( L, runtime );

	return result;
}

void
LuaContext::Delete( LuaContext *context )
{
	if ( context )
	{
		void *ud = NULL;
		(void)lua_getallocf( context->L(), & ud ); Rtt_ASSERT( ud );
	
		Rtt_DELETE( context );

		// Destroy this *after* we destroy the Lua state.  
		// Calls to LuaContext::GetRuntime(), GetAllocator(), etc. depend on this pointer
		Rtt_DELETE( static_cast< LuaContextUserdata* >( ud ) );
	}
}

Rtt_Allocator*
LuaContext::GetAllocator( lua_State* L )
{
	void *ud = NULL;
	(void)lua_getallocf( L, & ud ); Rtt_ASSERT( ud );
	return static_cast< LuaContextUserdata* >( ud )->GetAllocator();
}	

const MPlatform&
LuaContext::GetPlatform( lua_State* L )
{
	void *ud = NULL;
	(void)lua_getallocf( L, & ud ); Rtt_ASSERT( ud );
	return static_cast< LuaContextUserdata* >( ud )->GetPlatform();
}

Runtime*
LuaContext::GetRuntime( lua_State* L )
{
    void *ud = NULL;
    (void)lua_getallocf( L, & ud ); Rtt_ASSERT( ud );
    return static_cast< LuaContextUserdata* >( ud )->GetRuntime();
}

bool
LuaContext::HasRuntime( lua_State* L )
{
    void *ud = NULL;
    (void)lua_getallocf( L, & ud );
    return (ud != NULL) && (static_cast< LuaContextUserdata* >( ud )->GetRuntime() != NULL);
}
    
LuaContext*
LuaContext::GetContext( lua_State *L )
{
	void *ud = NULL;
	(void)lua_getallocf( L, & ud ); Rtt_ASSERT( ud );
	return static_cast< LuaContextUserdata* >( ud )->GetOwner();
}

bool
LuaContext::IsBinaryLua( const char* filename )
{
	bool result = false;

	if ( Rtt_VERIFY( filename ) )
	{
		FILE* f = fopen( filename, "r" );
		if ( f )
		{
			int c = getc( f );

			// Unix exec. file?
			if (c == '#')
			{
				while ( (c = getc( f )) != EOF && c != '\n' ) ; // skip first line
				if ( c == '\n' ) { c = getc( f ); }
			}

			result = ( c == LUA_SIGNATURE[0] );
			fclose( f );
		}
	}

	return result;
}

// ----------------------------------------------------------------------------

LuaContext::LuaContext( ::lua_State* L )
:	fL( L ),
	fHandle( LuaContext::GetAllocator( L ), * L ),
	fModules( 0 )
{
}

// init.lua is pre-compiled into bytecodes and then placed in a byte array
// constant in init.cpp. The following function defined in init.cpp loads
// the bytecodes via luaL_loadbuffer. The .cpp file is dynamically generated.
int luaload_init(lua_State* L);

// Initialize Lua and Rtt Runtime
void
LuaContext::Initialize( const MPlatform& platform, Runtime* runtime )
{
	lua_State* L = fL;

	lua_atpanic( L, & LuaContext::Panic );

	Self::InitializeLuaPath( L, platform );

#ifdef Rtt_DEBUG
	sLuaContext = L;
#endif

	if ( runtime )
	{
		Self::InitializeLibraries( L, runtime );

        // We should wait until the basic set of Lua libraries are registered.
        // Opportunity for the platform to register platform-specific Lua/C loaders
		const MRuntimeDelegate *delegate = runtime->GetDelegate();
		if ( delegate )
		{
			delegate->DidInitLuaLibraries( * runtime );
		}
        
		// Load and invoke init.lua bytecodes
		DoBuffer( luaload_init );
		/*
		if ( Rtt_VERIFY( 0 == luaload_init( L ) ) )
		{
			int status = DoCall( L, 0, 0 ); Rtt_ASSERT( 0 == status );
			report( L, status );
		}
		*/

		UpdateStage( * runtime->GetDisplay().GetStage() );
#ifdef Rtt_AUTHORING_SIMULATOR
		SimulatorControl::InitializeDebugger( *runtime, L );
#endif
	}
	else
	{
		LuaGCInhibitor inhibitor( L );
		Self::InitializeLuaCore( L );
	}
}

LuaContext::~LuaContext()
{
	Rtt_ASSERT( fL );
	lua_close( fL );
}

static int
DisabledParserFunction( lua_State *L )
{
	const char *name = lua_tostring( L, lua_upvalueindex( 1 ) );
	if ( ! Rtt_VERIFY( name ) ) { name = "function"; }

	Rtt_LogException( "Error: %s() is not available because the parser is not loaded", name );
	return 0;
}

static void
ReplaceGlobalFunction( lua_State *L, const char *functionName, lua_CFunction f )
{
	lua_pushstring( L, functionName );
	lua_pushcclosure( L, f, 1 );
	lua_setglobal( L, functionName );
}

void
LuaContext::DisableParser()
{
	lua_State *L = fL;

	ReplaceGlobalFunction( L, "dofile", DisabledParserFunction );
	ReplaceGlobalFunction( L, "load", DisabledParserFunction );
	ReplaceGlobalFunction( L, "loadfile", DisabledParserFunction );
}

static int
RestrictedEnterpriseFunction( lua_State *L )
{
	lua_pushvalue( L, lua_upvalueindex( 1 ) );

	Rtt_ASSERT_NOT_IMPLEMENTED();

	return 0;
}

void
LuaContext::RestrictFunction( const char *libName, const char *functionName )
{
	lua_State *L = fL;

	CoronaLibraryPushProperty( L, libName, functionName );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, 1 );
		lua_pushcclosure( L, RestrictedEnterpriseFunction, 1 );
		CoronaLuaPushModule( L, libName );
		lua_setfield( L, -2, functionName );
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );
}

void
LuaContext::Collect()
{
	Rtt_TRACE( ( "Lua is currently using %d KB", lua_gc( fL, LUA_GCCOUNT, 0 ) ) );
	lua_gc( fL, LUA_GCCOLLECT, 0 );
}

int
LuaContext::DoBuffer( lua_CFunction loader, lua_CFunction pushargs )
{
	return Self::DoBuffer( fL, loader, pushargs );
}

int
LuaContext::DoFile( const char* file, int narg, bool clear )
{
	return Self::DoFile( fL, file, narg, clear );
}

void
LuaContext::UpdateStage( StageObject& stage ) const
{
	::lua_State* L = fL;

	// Set value for _G.Runtime._stage
	// Do this *after* calling Rtt_LuaInit. Otherwise Runtime won't exist yet
	Lua::PushRuntime( L );

	stage.InitProxy( L );
	stage.GetProxy()->PushTable( L );

	const char kStageKey[] = "_stage";
	lua_setfield( L, -2, kStageKey );

	lua_pop( L, 1 );
}
    
// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
