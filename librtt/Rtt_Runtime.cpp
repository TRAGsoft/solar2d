//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Solar2D game engine.
// With contributions from Dianchu Technology
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_Runtime.h"
#include "Rtt_SimulatorControl.h"

#include "Core/Rtt_Allocator.h"
#include "Core/Rtt_String.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_SpritePlayer.h"
#include "Display/Rtt_StageObject.h"
#include "Rtt_Archive.h"
#include "Display/Rtt_BufferBitmap.h"
#include "Rtt_Event.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaFile.h"
#include "Rtt_LuaProxy.h"
#include "Rtt_LuaProxyVTable.h"
#include "Rtt_MPlatform.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_MRuntimeDelegate.h"
#include "Rtt_PlatformExitCallback.h"
#include "Rtt_PlatformTimer.h"
#include "Rtt_Scheduler.h"
#include "Rtt_LuaFrameworks.h"
#include "Rtt_HTTPClient.h"

#ifdef Rtt_USE_ALMIXER
	#include "Rtt_PlatformOpenALPlayer.h"
#endif
#ifdef Rtt_USE_OPENSLES
#include "Rtt_PlatformOpenSLESPlayer.h"
#endif

// ----------------------------------------------------------------------------

namespace Rtt
{
#if defined( Rtt_AUTHORING_SIMULATOR )
extern "C" int luaopen_coronabaselib(lua_State * L);

void Runtime::FinalizeWorkingThreadWithEvent(Runtime* runtime, lua_State* L)
{
	std::thread* curWorkingThread = runtime->m_fAsyncThread.load();
	if (curWorkingThread) {
		curWorkingThread->join();
		std::string* strResult = runtime->m_fAsyncResultStr.load();
		Lua::Ref listener = runtime->m_fAsyncListener.load();

		if (L) {
			Lua::NewEvent(L, "async");
			if (strResult->empty()) {
				lua_pushnil(L);
			} else {
				lua_pushstring(L, strResult->c_str());
			}
			lua_setfield(L, -2, "result");
			Lua::DispatchEvent(L, listener, 0);
			Lua::DeleteRef(L, listener);
		}

		delete curWorkingThread;
		delete strResult;

		runtime->m_fAsyncListener.store(nullptr);
		runtime->m_fAsyncResultStr.store(nullptr);
		runtime->m_fAsyncThread.store(nullptr);
	}
}

int Runtime::ShellPluginCollector_Async(lua_State* L)
{
	Runtime *runtime = (Runtime*)lua_touserdata(L, lua_upvalueindex(1));

	std::string args(lua_tostring(L, 1));
	Lua::Ref listener = nullptr;
	if (lua_type(L, 2) == LUA_TFUNCTION) {
		listener = Lua::NewRef(L, 2);
	}

	FinalizeWorkingThreadWithEvent(runtime, L);
	
	std::thread* thread = new std::thread([](std::string args, Lua::Ref listener, Runtime* runtime) {
		lua_State* L = Rtt::Lua::New(true);
		lua_pushcfunction(L, luaopen_coronabaselib);
		lua_pushstring(L, "coronabaselib");
		lua_call(L, 1, 0);

		HTTPClient::registerFetcherModuleLoaders(L);
		std::string result;
		lua_getglobal(L, "require");
		lua_pushstring(L, "CoronaBuilderPluginCollector");
		int res = lua_pcall(L, 1, 1, NULL);
		lua_getfield(L, -1, "collect");
		lua_pushstring(L, args.c_str());
		res = lua_pcall(L, 1, 1, NULL);
		if (lua_type(L, -1) == LUA_TSTRING) {
			result = lua_tostring(L, -1);
		}
		Rtt::Lua::Delete(L);
		runtime->m_fAsyncListener.store(listener);
		runtime->m_fAsyncResultStr.store(new std::string(result));
	}, args, listener, runtime);

	if (listener) {
		lua_pushnil(L);
		runtime->m_fAsyncListener.store(listener);
		runtime->m_fAsyncThread.store(thread);
	}
	else {
		thread->join();
		std::string* result = runtime->m_fAsyncResultStr.load();
		if (result->empty()) {
			lua_pushnil(L);
		}
		else {
			lua_pushstring(L, result->c_str());
		}
		FinalizeWorkingThreadWithEvent(runtime, NULL);
	}
	return 1;
}

#endif
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------

Runtime::Runtime( const MPlatform& platform, MCallback *viewCallback )
:	fAllocator( platform.GetAllocator() ),
	fPlatform( platform ),
	fStartTime( Rtt_GetAbsoluteTime() ),
	fStartTimeCorrection( 0 ),
	fSuspendTime( 0 ),
#ifdef Rtt_AUTHORING_SIMULATOR
	fSimulatorControlTime( 0 ),
	fSimulatorControlLastTickTime( 0 ),
	fSimulatorControlPaused( false ),
	fSimulatorControlTimeConsumed( false ),
#endif
	fResourcesHead( Rtt_NEW( & fAllocator, CachedResource( * this, NULL ) ) ),
	fTimer( platform.CreateTimerWithCallback( viewCallback ? * viewCallback : * this ) ),
	fDisplay( Rtt_NEW( & fAllocator, Display( * this ) ) ),
	fVMContext( LuaContext::New( Allocator(), platform, this ) ),
	fScheduler( Rtt_NEW( & fAllocator, Scheduler( * this ) ) ),
	fArchive( NULL ),
	fBackend("glBackend"),
	fBackendState(nullptr),
#ifdef Rtt_USE_ALMIXER
	fOpenALPlayer(NULL),
#endif
#ifdef Rtt_WIN_ENV
	fFPS(30),
#else
	fFPS(60),
#endif
	fIsSuspended(-1), // uninitialized
	fProperties(0),
	fSuspendOverrideProperties(kSuspendAll),
	fFrame(0),
	fLaunchArgsRef(LUA_NOREF),
	fSimulatorPlatformName(NULL),
	fDownloadablePluginsRef(LUA_NOREF),
	fDownloadablePluginsCount(0),
	fDelegate(NULL),
#ifdef Rtt_AUTHORING_SIMULATOR
	m_fAsyncListener(nullptr),
	m_fAsyncResultStr(nullptr),
	m_fAsyncThread(nullptr),
#endif
	fErrorHandlerRecursionGuard( false )
{
	Rtt_TRACE_SIM( ( "\n%s\n", Rtt_STRING_COPYRIGHT ) );
	Rtt_TRACE_SIM( ( "\tVersion: %s\n", Rtt_STRING_VERSION ) );
	Rtt_TRACE_SIM( ( "\tBuild: %s\n", Rtt_STRING_BUILD ) );

	fResourcesHead->Retain();

	if ( ! Rtt_VERIFY( fVMContext )
		 || ! Rtt_VERIFY( fTimer ) )
	{
		platform.RaiseError( MPlatform::kOutOfMemoryError, NULL );
	}

	// TODO: Move this to Mac/Win simulator-side of code
	#ifdef Rtt_AUTHORING_SIMULATOR
		// Simulator does not use resource.car file
		SetProperty( kIsApplicationNotArchived, true );
		SetProperty( kIsLuaParserAvailable, true );
	#endif
}

Runtime::~Runtime()
{
	// Notify the owner that this runtime object is about to be destroyed.
	if (fDelegate)
	{
		fDelegate->WillDestroy( *this );
	}

	OnSystemEvent( SystemEvent::kOnAppExit );

#if defined(Rtt_AUTHORING_SIMULATOR)
	SimulatorControl::Shutdown( *this );
#endif

#ifdef Rtt_USE_ALMIXER
	if ( fOpenALPlayer )
	{
		PlatformOpenALPlayer::SharedInstance()->RuntimeWillTerminate( * this );
		PlatformOpenALPlayer::ReleaseInstance();
		fOpenALPlayer = NULL;
	}	
#endif

	Rtt_DELETE( fArchive );
	Rtt_DELETE( fScheduler );
	fTimer->Stop();
	Rtt_DELETE( fTimer );
	
	// Notify textures that everything is about to be destroyed. Clean up all dependencies.
	// This is required because Textures and Objects may be dependent on Lua state, or have to notify plugins
	fDisplay->Teardown();

	LuaContext::Delete( fVMContext );

	// Marking the Lua context as NULL invalidates the resource cache.
	// We do this before we delete the display list to prevent resources in the
	// display list from unnecessarily removing themselves from a bogus cache!
	fVMContext = NULL;

	// Lua VM no longer exists, so Corona app is technically no longer executing.
	// This also stops TextureFactory::GetTextureMemoryUsed() from going negative,
	// since it will count images loaded by shell.lua as being removed.
	SetProperty( kIsApplicationExecuting, false );

	// Must delete this *after* Lua is destroyed, since certain Lua finalizers
	// rely on fDisplay being valid
	Rtt_DELETE( fDisplay );

	fResourcesHead->Release();
#if defined(Rtt_AUTHORING_SIMULATOR)
	FinalizeWorkingThreadWithEvent(this, nullptr);
#endif
}

// ----------------------------------------------------------------------------

class LoadMainGuard
{
	Rtt_CLASS_NO_DYNAMIC_ALLOCATION

	public:
		LoadMainGuard( const Runtime& runtime );
		~LoadMainGuard();

	private:
		const Runtime& fRuntime;
		const MRuntimeDelegate *fDelegate;
};

LoadMainGuard::LoadMainGuard( const Runtime& runtime )
:	fRuntime( runtime ),
	fDelegate( runtime.GetDelegate() )
{
	if ( fDelegate )
	{
		LuaStackGuard guard( runtime.VMContext().L(), "WillLoadMain()" );

		fDelegate->WillLoadMain( runtime );
	}
}

LoadMainGuard::~LoadMainGuard()
{
	if ( fDelegate )
	{
		LuaStackGuard guard( fRuntime.VMContext().L(), "DidLoadMain()" );

		fDelegate->DidLoadMain( fRuntime );
	}
}

// ----------------------------------------------------------------------------
// LoadMainTask is used by the authoring simulator only. Its main purpose is to
// wrap up code in shell.lua.
class LoadMainTask : public Task
{
	public:
		LoadMainTask();

	public:
		virtual void operator()( Scheduler& sender );
};

LoadMainTask::LoadMainTask()
{
}

void
LoadMainTask::operator()( Scheduler& sender )
{
	Runtime& runtime = sender.GetOwner();

	// Rtt_ASSERT( ! runtime.IsProperty( Runtime::kIsApplicationExecuting ) );
	runtime.SetProperty( Runtime::kIsApplicationExecuting, true );

	LoadMainGuard guard( runtime );

	int narg = runtime.PushLaunchArgs( false );

	if ( runtime.IsProperty( Runtime::kIsApplicationNotArchived ) )
	{
		// Load from individual Lua file
		const char kMain[] = Rtt_LUA_SCRIPT_FILE( "main" );
		String filePath( runtime.GetAllocator() );
		runtime.Platform().PathForFile( kMain, MPlatform::kResourceDir, MPlatform::kDefaultPathFlags, filePath );
		if ( Rtt_VERIFY( filePath.GetString() ) )
		{
			runtime.VMContext().DoFile( filePath.GetString(), narg );
		}
	}
	else
	{
		// Load from resource.car
		const char kMain[] = Rtt_LUA_OBJECT_FILE( "main" );
		(void) Rtt_VERIFY( 0 == runtime.GetArchive()->DoResource( runtime.VMContext().L(), kMain, narg ) );
	}

	runtime.OnSystemEvent( SystemEvent::kOnAppStart );
}


static int
onShellComplete( lua_State* L )
{
	Runtime* runtime = LuaContext::GetRuntime( L );
	Scheduler& scheduler = runtime->GetScheduler();

	LoadMainTask* e = Rtt_NEW( runtime->Allocator(), LoadMainTask() );
	scheduler.Append( e );

	return 0;
}

#if defined( Rtt_AUTHORING_SIMULATOR )
static int
exitCallback( lua_State* L )
{
	Runtime* runtime = LuaContext::GetRuntime( L );
	int code = luaL_optint( L, 1, 0 );	
	PlatformExitCallback* callback = runtime->GetExitCallback();
	if ( callback )
	{
		(*callback)(code);
	}
	return 0;
}

static int
pushShellArgs( lua_State* L )
{
	Runtime* runtime = LuaContext::GetRuntime( L );
	Rtt::String value( runtime->GetAllocator() );

	lua_createtable( L, 0, 8 ); // params
	{
		// Pass function to schedule LoadMainTask
		lua_pushcfunction( L, onShellComplete );
		lua_setfield( L, -2, "onShellComplete" ); // params.onShellComplete

		if ( runtime->GetExitCallback() )
		{
			lua_pushcfunction( L, exitCallback );
			lua_setfield( L, -2, "exitCallback" ); // params.exitCallback
		}

		runtime->PushDownloadablePlugins( L );
		lua_setfield( L, -2, "plugins" );
		
#if defined( Rtt_AUTHORING_SIMULATOR )
		lua_pushlightuserdata(L, runtime);
		lua_pushcclosure(L, &Runtime::ShellPluginCollector_Async, 1);
		lua_setfield( L, -2, "shellPluginCollector");
#endif

		const char *name = runtime->GetSimulatorPlaformName();
		if ( name )
		{
			lua_pushstring( L, name );
			lua_setfield( L, -2, "platform" );
		}
	}

	return 1; // params
}

#else // ! defined( Rtt_AUTHORING_SIMULATOR )

static int
pushShellArgs( lua_State* L )
{
	int result = 1;
	
	Runtime* runtime = LuaContext::GetRuntime( L );
	Rtt::String value( runtime->GetAllocator() );

	lua_createtable( L, 0, 2 ); // params
	{
		// Pass function to schedule LoadMainTask
		lua_pushcfunction( L, onShellComplete );
		lua_setfield( L, -2, "onShellComplete" ); // params.onShellComplete
	}

	return result;
}

#endif // defined( Rtt_AUTHORING_SIMULATOR )

// shell.lua is pre-compiled into bytecodes and then placed in a byte array
// constant in shell.cpp. The following function defined in shell.cpp loads
// the bytecodes via luaL_loadbuffer. The .cpp file is dynamically generated.
int luaload_shell(lua_State* L);




// ----------------------------------------------------------------------------

void
Runtime::InitializeArchive( const char *filePath )
{
	Rtt_ASSERT( ! fArchive );

	fArchive = Rtt_NEW( & fAllocator, Archive( fAllocator, filePath ) );

	// Add loader
	lua_State *L = fVMContext->L();
	Lua::InsertPackageLoader( L, & Archive::ResourceLoader, 1 );
}

static const char kApplication[] = "application";

bool
Runtime::PushConfig( lua_State *L )
{
	bool result = false;

	int status = LUA_ERRFILE; // assume file does not exist

	Rtt_ASSERT( 0 == lua_gettop( L ) );

	if ( IsProperty( kIsApplicationNotArchived ) )
	{
		// Load from individual Lua file
		const char kConfig[] = Rtt_LUA_SCRIPT_FILE( "config" );

		String filePath( GetAllocator() );
		fPlatform.PathForFile( kConfig, MPlatform::kResourceDir, MPlatform::kTestFileExists, filePath );
		const char *path = filePath.GetString();
		if ( path )
		{
			status = VMContext().DoFile( path, 0, true );
		}
		else
		{
			// other cases assume a non-zero status means there's an error msg on the stack
			// so push a "fake" error msg on the stack so we are consistent with those cases
			lua_pushnil( L );
		}
	}
	else
	{
		// Load from resource.car
		const char kConfig[] = Rtt_LUA_OBJECT_FILE( "config" );
		status = GetArchive()->DoResource( L, kConfig, 0 );
	}

	// Check config.lua was loaded successfully
	if ( 0 == status )
	{
		if ( fDelegate )
		{
			fDelegate->InitializeConfig( * this, L );
		}

		lua_getglobal( L, kApplication ); // application

		if ( lua_istable( L, -1 ) )
		{			
			lua_getfield( L, -1, "content" ); // application.content
			result = lua_istable( L, -1 );

			if ( result )
			{
				lua_remove( L, -2 ); // application
			}
			else
			{
				lua_pop( L, 2 ); // pop application and application.content
			}
		}
		else
		{
			lua_pop( L, 1 ); // pop application
		}
	}
	else
	{
		lua_pop( L, lua_gettop( L ) ); // pop and ignore error msg (we don't have a config.lua file)
	}

	Rtt_ASSERT( ( ! result && 0 == lua_gettop( L ) )
				|| ( result && 1 == lua_gettop( L ) ) );

	return result;
}

void
Runtime::ReadConfig( lua_State *L )
{
	// Rtt_ASSERT( ! fDisplay );
	Rtt_ASSERT( 1 == lua_gettop( L ) );	
	Rtt_ASSERT( lua_istable( L, -1 ) );

	lua_getfield(L, -1, "fps");
#if defined( Rtt_WIN_ENV ) || defined( Rtt_MAC_ENV )
	int fps = (int)lua_tointeger(L, -1);
	// Any positive integer fps value is accepted on desktop platforms with
	// display-aware timers.
	// The runtime caps the effective tick rate to the monitor refresh rate in BeginRunLoop().
	if (fps > 0)
	{
		Rtt_ASSERT(!IsProperty(kIsApplicationLoaded));
		fFPS = (U32)fps;
	}
#else
	// Other platforms do not support dynamic FPS yet; keep them fixed at 60 FPS.
#endif
	lua_pop(L, 1);

	// Apparently this is used for automated testing (set application.content.exitOnError in config.lua)
	lua_getfield( L, -1, "exitOnError" );
	if ( lua_toboolean( L, -1 ) )
	{
		SetProperty( kUseExitOnErrorHandler, true );
	}
	lua_pop( L, 1 );
	
#ifdef Rtt_USE_ALMIXER
	lua_getfield( L, -1, "audioPlayFrequency" );
	int frequency = (int) lua_tointeger( L, -1 );
	PlatformOpenALPlayer::SetFrequency( frequency );
	lua_pop( L, 1 );

	lua_getfield( L, -1, "maxSources" );
	int sources = (int) lua_tointeger( L, -1 );
	PlatformOpenALPlayer::SetMaxSources( sources );
	lua_pop( L, 1 );	
#endif
	

	Rtt_ASSERT( 1 == lua_gettop( L ) );	
}

void
Runtime::PopAndClearConfig( lua_State *L )
{
	Rtt_ASSERT( 1 == lua_gettop( L ) );
	Rtt_ASSERT( lua_istable( L, -1 ) );

	// pop "application.content"
	lua_pop( L, 1 );

	// set global table "application" to nil
	lua_pushnil( L );
	lua_setglobal( L, kApplication );

	Rtt_ASSERT( 0 == lua_gettop( L ) );
}

void
Runtime::AddDownloadablePlugin(
	lua_State *L, const char *pluginName, const char *pluginVersion, const char *publisherId,
	int downloadablePluginsIndex, bool isSupportedOnThisPlatform, const char *pluginEntryJSON)
{
	Rtt_LUA_STACK_GUARD( L );

	lua_createtable( L, 0, 2 );
	{
		lua_pushstring( L, pluginName );
		lua_setfield( L, -2, "pluginName" );

		lua_pushstring( L, pluginVersion );
		lua_setfield( L, -2, "version" );

		lua_pushstring( L, publisherId );
		lua_setfield( L, -2, "publisherId" );

		lua_pushboolean( L, isSupportedOnThisPlatform ? 1 : 0 );
		lua_setfield( L, -2, "isSupportedOnThisPlatform" );
		
		lua_pushstring(L, pluginEntryJSON);
		lua_setfield(L, -2, "json");
	}
	++fDownloadablePluginsCount;
	lua_rawseti( L, downloadablePluginsIndex, fDownloadablePluginsCount );
}

// Determine list of plugins that the project is using based on build.settings
void
Runtime::FindDownloadablePlugins( const char *simPlatformName )
{
	fSimulatorPlatformName = simPlatformName;

	// Create temporary Lua state to prevent build.settings from having
	// any side effects from the Runtime's L state. Note the separate
	// 'runtimeL' variable for Runtime's L below.
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	
	Lua::RegisterModuleLoader( L, "lpeg", luaopen_lpeg );
	Lua::RegisterModuleLoader( L, "dkjson", Lua::Open< luaload_dkjson > );
	Lua::RegisterModuleLoader( L, "json", Lua::Open< luaload_json > );
	lua_getglobal(L, "require");
	lua_pushstring(L, "json");
	lua_pcall(L, 1, 1, 0);
	lua_getfield(L, -1, "encode");
	lua_setglobal(L, "jsonEncode");
	lua_pop(L, 1); // pop json

	const char kBuildSettings[] = "build.settings";
	
	String filePath( & fPlatform.GetAllocator() );
	fPlatform.PathForFile( kBuildSettings, MPlatform::kResourceDir, MPlatform::kTestFileExists, filePath );

	const char *p = filePath.GetString();
	if ( p
		 && 0 == luaL_loadfile( L, p )
		 && 0 == lua_pcall( L, 0, 0, 0 ) )
	{
		lua_getglobal( L, "settings" );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield(L, -1, "plugins");
			if (lua_istable(L, -1))
			{
				lua_State *runtimeL = fVMContext->L();
				Rtt_LUA_STACK_GUARD(runtimeL);

				// Create table to store downloadable plugins
				lua_createtable(runtimeL, 0, 0);
				lua_pushvalue(runtimeL, -1); // luaL_ref pops so push extra reference
				fDownloadablePluginsRef = luaL_ref(runtimeL, LUA_REGISTRYINDEX);
				int downloadablePluginsIndex = lua_gettop(runtimeL);

				int pluginsIndex = lua_gettop(L);
				lua_pushnil(L); // push first key
				while (lua_next(L, pluginsIndex) != 0)
				{
					bool availableOnPlatform = true;

					if (lua_type(L, -2) != LUA_TSTRING)
					{
						Rtt_LogException("ERROR: Skipping invalid plugin entry in %s file", kBuildSettings);
						lua_pop(L, 1); // pop value; keep 'key' for next iteration
						continue;
					}

					// key at -2, value at -1
					const char *pluginName = lua_tostring(L, -2);

					// these names are compatible with those used in things like "excludeFiles"
					// (they should probably be defined in a central place but are not)
#ifdef Rtt_MAC_ENV
					const char *primaryOSKey = "osx";
#elif Rtt_WIN_DESKTOP_ENV
					const char *primaryOSKey = "win32";
#elif Rtt_LINUX_ENV
					const char *primaryOSKey = "linux";
#else
					// this code shouldn't run on any other platform
					const char *primaryOSKey = "not-used";
#endif

					// ensure the stack contains a table at top
					if (lua_istable(L, -1))
					{
						lua_getglobal(L, "jsonEncode");
						lua_pushvalue(L, -2);
						lua_pcall(L, 1, 1, 0);
						const char *pluginJson = lua_tostring(L, -1);
						String pluginJsonStr;
						pluginJsonStr.Set(pluginJson);
						lua_pop(L, 1);
						
						// Honor the settings for "supportedPlatforms", if any
						lua_getfield(L, -1, "supportedPlatforms");
						{
							if (lua_istable(L, -1))
							{
								// If "supportedPlatforms" exists, the default for platforms changes to false
								// (if it doesn't exist at all, platforms are assumed to be supported)
								availableOnPlatform = false;

								lua_getfield(L, -1, primaryOSKey);
								if (lua_isboolean(L, -1))
								{
									availableOnPlatform = (bool)lua_toboolean(L, -1);
								}
								lua_pop(L, 1); // pop platform
							}
						}
						lua_pop(L, 1); // pop supportedPlatforms

						// Add the plugin to our download list if supported.
						// Note: Ignore the "supportedPlatforms" in the simulator, because we always want to
						//       download the plugin's simulator stub if available.
#ifndef Rtt_AUTHORING_SIMULATOR
						if (availableOnPlatform)
#endif
						{
							// Transfer information to Runtime's list of downloadable plugins.
							// Note, we use runtimeL instead of L here!
							lua_getfield(L, -1, "version");
							const char *pluginVersion = lua_tostring(L, -1); // push version
							lua_pop(L, 1); // pop version

							lua_getfield(L, -1, "publisherId");
							const char *publisherId = lua_tostring(L, -1); // push publisherId
							lua_pop(L, 1); // pop publisherId

							AddDownloadablePlugin(
								runtimeL, pluginName, pluginVersion, publisherId, downloadablePluginsIndex, availableOnPlatform, pluginJsonStr.GetString());
						}
					}
					else
					{
						Rtt_LogException("ERROR: Plugin entry '%s' must be a table in %s file", pluginName, kBuildSettings);
					}

					lua_pop(L, 1); // pop value; keep 'key' for next iteration
				}
				lua_pop(runtimeL, 1); // pop downloadablePlugins table
			}
			lua_pop(L, 1); // pop plugins
		}
	}

	lua_close( L );
}

void
Runtime::PushDownloadablePlugins( lua_State *L )
{
	lua_rawgeti( L, LUA_REGISTRYINDEX, fDownloadablePluginsRef );
}

bool
Runtime::RequiresDownloadablePlugins() const
{
	return ( fDownloadablePluginsCount > 0 );
}

int
Runtime::PushLaunchArgs( bool createIfNotExists )
{
	lua_State *L = fVMContext->L(); Rtt_ASSERT( L );

	if ( LUA_NOREF == fLaunchArgsRef )
	{
		if ( createIfNotExists )
		{
			lua_createtable( L, 0, 0 );

			lua_pushvalue( L, -1 ); // luaL_ref pops so push extra reference
			fLaunchArgsRef = luaL_ref( L, LUA_REGISTRYINDEX );
		}
		else
		{
			lua_pushnil( L );
		}
	}
	else
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, fLaunchArgsRef );
	}

	return 1;
}

void
Runtime::ClearLaunchArgs()
{
	lua_State *L = fVMContext->L(); Rtt_ASSERT( L );

	if ( LUA_NOREF != fLaunchArgsRef )
	{
		luaL_unref( L, LUA_REGISTRYINDEX, fLaunchArgsRef );
		fLaunchArgsRef = LUA_NOREF;
	}
}

Runtime::LoadApplicationReturnCodes
Runtime::LoadApplication( U32 launchOptions )
{
	LoadApplicationReturnCodes result = Runtime::kGeneralFail;

	const char kArchive[] = Rtt_CAR_FILE( "resource" );
	const char *basename = kArchive;

	// Use kSystemResourceDir b/c resource.car should always live inside the .app bundle
	String filePath( GetAllocator() );
#if defined( Rtt_ANDROID_ENV )
    fPlatform.PathForFile( basename, MPlatform::kResourceDir, MPlatform::kDefaultPathFlags, filePath );
#else
	fPlatform.PathForFile( basename, MPlatform::kSystemResourceDir, MPlatform::kDefaultPathFlags, filePath );
#endif

	{
		// Init VM
		fVMContext->Initialize( fPlatform, this );
		if ( ! IsProperty( kIsLuaParserAvailable ) )
		{
			fVMContext->DisableParser();
		}

		if ( ! IsProperty( kIsApplicationNotArchived ) )
		{
			if ( filePath.IsEmpty() )
			{
				goto exit_gracefully;
			}
			InitializeArchive( filePath.GetString() );
		}
		
		// ---------------------------------------------------------------

		lua_State *L = VMContext().L();
		bool hasConfig = PushConfig( L );
		if ( hasConfig )
		{
			if ( fDelegate )
			{
				fDelegate->WillLoadConfig( * this, L );
			}
			ReadConfig( L ); // assumes fDisplay is NULL
		}
		else
		{
			// Always create a table to simplify logic later
			lua_createtable( L, 0, 2 );
		}

		// We need a runtime guard to lock in the OpenGL context.
		// The runtime guard is re-entrant safe. It doesn't look like we invoke multiple runtime guards (though I didn't verify),
		// but it should be safe to do.
		RuntimeGuard guard( * this );

		int configIndex = lua_gettop( L );
		fDisplay->Initialize( L, configIndex, fBackend, fBackendState );

		if ( fDelegate )
		{
			fDelegate->DidLoadConfig( * this, L );
		}

		PopAndClearConfig( L );
		
		// ---------------------------------------------------------------
		
		// Now that the config.lua has been read, startup any libraries that depend on those values
#if defined ( Rtt_USE_ALMIXER )
		fOpenALPlayer = PlatformOpenALPlayer::RetainInstance();
		fOpenALPlayer->AttachNotifier( Rtt_NEW( GetAllocator(), PlatformNotifier( VMContext().LuaState() ) ) );
#endif
		// If kLaunchDeviceShell is set, run shell.lua which will, in turn, run main.lua (via onShellComplete())
		if ( launchOptions & kLaunchDeviceShell )
		{
			// Load and invoke shell.lua bytecodes
			bool retVal = Rtt_VERIFY( 0 == fVMContext->DoBuffer( luaload_shell, pushShellArgs ) );
			result = retVal ? Runtime::kSuccess : Runtime::kGeneralFail;
		}
		else
		{
			// If kLaunchDeviceShell is not set, just schedule main.lua
			LoadMainTask* e = Rtt_NEW( Allocator(), LoadMainTask( ) );
			GetScheduler().Append( e );
			
			result = Runtime::kSuccess;
		}
	}

	if ( result == Runtime::kSuccess )
	{
		SetProperty( kIsApplicationLoaded, true );
	}

exit_gracefully:
	return result;
}

#ifdef Rtt_WIN_ENV
void
Runtime::OnMonitorChanged(double newRefreshRate)
{
	if (newRefreshRate <= 0.0 || !IsProperty(kIsApplicationLoaded))
	{
		return;
	}

	double kFps = static_cast<double>(fFPS);
	if (kFps > newRefreshRate)
	{
		kFps = newRefreshRate;
	}

	fTimer->SetInterval(1000.0 / kFps);

	MonitorChangedEvent e(newRefreshRate, static_cast<U32>(kFps));
	DispatchEvent(e);
}
#endif

void
Runtime::BeginRunLoop()
{
	double refreshRate = fTimer->GetRefreshRate();

	// Calculate the effective fps - cap to monitor refresh rate if exceeded.
	// fFPS is never modified here so display.fps keeps reflecting the configured runtime FPS.
	double kFps = static_cast<double>(fFPS);
	if (refreshRate > 0.0 && kFps > refreshRate)
	{
		Rtt_LogException("WARNING: config.lua fps (%d) exceeds display refresh rate (%.4fHz). Capping to %.4ffps.\n",
			(int)kFps, refreshRate, refreshRate);
		kFps = refreshRate;
	}

	const double kInterval = 1000.0 / kFps;

	fDisplay->Start();

	// PlatformTimer
	fTimer->SetInterval( kInterval );

	// Initial call to main.lua can cause runtime to be suspended,
	// Otherwise, it's < 0 (i.e. uninitialized)
	if ( fIsSuspended < 0 )
	{
		fTimer->Start();
		fIsSuspended = 0;
	}

	// Update screen until timer fires to yield better launch time.
	// This may cause problems if the OpenGL context is not ready.

	//Platforms like Mac need to defer the initial update
 	if ( ! IsProperty( kDeferUpdate ) )
	{
		(*this)();
	}
}

void
Runtime::OnSystemEvent( SystemEvent::Type t )
{
	// Do not dispatch a system event if the Corona application has not been loaded yet.
	// This can happen if the destructor was called before LoadApplication() or if the application failed to load.
	if (IsProperty(kIsApplicationLoaded) == false)
	{
		return;
	}

	SystemEvent e( t );
	DispatchEvent( e );
}	
	
void
Runtime::CoronaInvokeSystemSuspendEvent()
{
	// If Corona (core) has already been suspended, don't trigger a callback.
	// This only looks at the Corona Core suspend flag.
	// This might need to be adjusted to deal with the possibility that only some subsystems may be suspended at a time.
	if ( 0 == fIsSuspended )
	{
		// Invoke the user system event
		OnSystemEvent( SystemEvent::kOnAppSuspend );
	}
}

void
Runtime::CoronaInvokeSystemResumeEvent()
{
	// Assumption: Corona has already been resumed at this point. This means the flag check can't look for being suspended.
	Rtt_ASSERT( fIsSuspended <= 0 );
	if ( 0 == fIsSuspended )
	{
		// Invoke the user system event
		OnSystemEvent( SystemEvent::kOnAppResume );
	}
}
	
void
Runtime::CoronaAllSuspend()
{
	CoronaCoreSuspend();
#ifdef Rtt_USE_ALMIXER
	PlatformOpenALPlayer* player = PlatformOpenALPlayer::SharedInstance();
	if( NULL != player)
	{
		player->SuspendPlayer();
	}
#endif
#ifdef Rtt_USE_OPENSLES
	getOpenSLES_player()->SuspendPlayer();
#endif

	// Suspend platform specific items
	fPlatform.Suspend();
}
	
void
Runtime::CoronaCoreSuspend()
{
	if ( 0 == fIsSuspended )
	{
		fTimer->Stop();
	}
	if ( fIsSuspended <= 0 )
	{
		fIsSuspended = 1;
		fSuspendTime = Rtt_GetAbsoluteTime();
	}
}

void
Runtime::Suspend(bool sendApplicationEvents /* = true */)
{
	// Determine if the runtime is currently running.
	// Note: -1 means not started. 0 means running. 1 means suspended.
	bool wasRunning = (0 == fIsSuspended);

	// Notify the runtime's owner that the runtime is about to be suspended.
	if ( wasRunning && fDelegate )
	{
		fDelegate->WillSuspend( *this );
	}

	// Only invoke the system suspend event if required (the Simulator sets this to false when suspending before a build)
	if (sendApplicationEvents)
	{
		// We need to make sure this event is called before any of the subsystems have been suspended.
		// We may need to track additional information about which specific subsystems were suspended and pass that as additional data to the event.
		CoronaInvokeSystemSuspendEvent();
	}

	// Easy case: 
	if ( kSuspendAll == fSuspendOverrideProperties )
	{
		CoronaAllSuspend();
	}
	else
	{
		bool coronaCoreShouldSuspend = true;

#ifdef Rtt_USE_ALMIXER
		if ( !IsSuspendOverrideProperty( kBackgroundAudio ) )
		{
			PlatformOpenALPlayer::SharedInstance()->SuspendPlayer();
		}
#endif


		// TODO: Right now this breaks the lua audio callback system too.
		// So selectively suspend so we can do further testing/experiments for now.
		if ( coronaCoreShouldSuspend )
		{
			CoronaCoreSuspend();
		}
	}

	// Notify the runtime's owner that the runtime has just been suspended.
	if ( wasRunning && fDelegate && (fIsSuspended > 0) )
	{
		fDelegate->DidSuspend( *this );
	}
}

void
Runtime::CoronaCoreResume()
{
	Rtt_ASSERT( 0 != fIsSuspended );
	if ( 1 == fIsSuspended )
	{

		Rtt_AbsoluteTime currentTime = Rtt_GetAbsoluteTime();
		Rtt_AbsoluteTime suspendTime = fSuspendTime; Rtt_ASSERT( suspendTime > 0 );
		if ( currentTime > suspendTime )
		{
			Rtt_AbsoluteTime suspendedDuration = currentTime - suspendTime;
			fStartTimeCorrection += suspendedDuration;
#if defined(Rtt_AUTHORING_SIMULATOR)
			if ( fSimulatorControlPaused )
			{
				fSimulatorControlTime += suspendedDuration;
				fSimulatorControlLastTickTime = currentTime;
			}
#endif
		}
		fSuspendTime = 0;

		fTimer->Start();

		fIsSuspended = 0;
	}
}

void
Runtime::CoronaResumeAll()
{
	// Determine if the runtime is currently suspended.
	// Note: -1 means not started. 0 means running. 1 means suspended.
	bool wasSuspended = (fIsSuspended > 0);

	// Notify the runtime's owner that the runtime is about to be resumed.
	if ( wasSuspended && fDelegate )
	{
		fDelegate->WillResume( *this );
	}

	// Resume the runtime, if currently suspended.
	CoronaCoreResume();

#ifdef Rtt_USE_ALMIXER
	// As an optimization, we may have just shutdown the audio system if it wasn't needed. Resume it to be safe.
	if( PlatformOpenALPlayer::SharedInstance()->IsPlayerSuspended() )
	{
		PlatformOpenALPlayer::SharedInstance()->ResumePlayer();
	}
	// If iOS entered an interruption state and backgrounding is enabled (think phone call), it could be possible that
	// resuming the app avoids the internal AudioSession callback to end the interruption.
	// So we should try to recover from an interruption event.
	// This should be done after the suspended check because the current implementation has suspend call interruption
	// so we want to avoid ending an interruption without resuming the player.
	else if( PlatformOpenALPlayer::SharedInstance()->IsInInterruption() )
	{
		PlatformOpenALPlayer::SharedInstance()->EndInterruption();
	}
#endif
#ifdef Rtt_USE_OPENSLES
	getOpenSLES_player()->ResumePlayer();
#endif

	// Resume platform specific items
	fPlatform.Resume();

	// Notify the runtime's owner that the runtime has just been resumed.
	if ( wasSuspended && fDelegate && !fIsSuspended )
	{
		fDelegate->DidResume( *this );
	}
}
	
void
Runtime::Resume(bool sendApplicationEvents /* = true */)
{
	CoronaResumeAll();
	// reset for the next time
	fSuspendOverrideProperties = kSuspendAll;

	// Only invoke the system resume event if required (the Simulator sets this to false when resuming after a build)
	if (sendApplicationEvents)
	{
		// We need to make sure this event is called only after all the subsystems have been resumed.
		// We may need to track additional information about which specific subsystems were resumed and pass that as additional data to the event.
		CoronaInvokeSystemResumeEvent();
	}
}

void
Runtime::DispatchEvent( const MEvent& e )
{
	RuntimeGuard guard( * this );

#if defined(Rtt_AUTHORING_SIMULATOR)
	if ( ! SimulatorControl::CanDispatchApplicationEvent( *this ) )
	{
		return;
	}
#endif

	e.Dispatch( fVMContext->L(), * this );
}

double
Runtime::GetElapsedMS() const
{
	Rtt_AbsoluteTime elapsed = GetElapsedTime();
	return ((double)Rtt_AbsoluteToMicroseconds( elapsed )) / 1000.;
}

Rtt_AbsoluteTime
Runtime::GetElapsedTime() const
{
	// During a suspend, use fSuspendTime as current time; otherwise, fetch absolute time
	Rtt_AbsoluteTime currentTime = ( 1 != fIsSuspended
		? ( fTimer ? fTimer->GetCurrentTime() : Rtt_GetAbsoluteTime() )
		: fSuspendTime );
#if defined(Rtt_AUTHORING_SIMULATOR)
	if ( fSimulatorControlPaused )
	{
		currentTime = fSimulatorControlTime;
	}
#endif
	Rtt_AbsoluteTime elapsed = currentTime - fStartTime;

	// Subtract out correction, but protect against subtracting past 0
	if ( elapsed > fStartTimeCorrection )
	{
		elapsed -= fStartTimeCorrection;
	}

	return elapsed;
}

#if defined(Rtt_AUTHORING_SIMULATOR)

void
Runtime::BeginSimulatorControlPause()
{
	if ( fSimulatorControlPaused )
	{
		return;
	}
	Rtt_AbsoluteTime currentTime = Rtt_GetAbsoluteTime();
	fSimulatorControlTime = IsSuspended() ? fSuspendTime : currentTime;
	fSimulatorControlLastTickTime = currentTime;
	fSimulatorControlPaused = true;
	fSimulatorControlTimeConsumed = false;
}

void
Runtime::WaitSimulatorControlPausedFrame()
{
	if ( fSimulatorControlPaused )
	{
		fSimulatorControlLastTickTime = Rtt_GetAbsoluteTime();
	}
}

void
Runtime::AdvanceSimulatorControlPausedFrame()
{
	if ( ! fSimulatorControlPaused )
	{
		return;
	}
	Rtt_AbsoluteTime currentTime = Rtt_GetAbsoluteTime();
	if ( fSimulatorControlTimeConsumed &&
		currentTime > fSimulatorControlLastTickTime )
	{
		fSimulatorControlTime +=
			currentTime - fSimulatorControlLastTickTime;
	}
	fSimulatorControlLastTickTime = currentTime;
	fSimulatorControlTimeConsumed = true;
}

void
Runtime::EndSimulatorControlPause( bool advanceToNextFrame )
{
	if ( ! fSimulatorControlPaused )
	{
		return;
	}
	Rtt_AbsoluteTime currentTime = Rtt_GetAbsoluteTime();
	if ( advanceToNextFrame && fSimulatorControlTimeConsumed &&
		currentTime > fSimulatorControlLastTickTime )
	{
		fSimulatorControlTime +=
			currentTime - fSimulatorControlLastTickTime;
	}
	Rtt_AbsoluteTime runtimeTime = IsSuspended() ? fSuspendTime : currentTime;
	if ( runtimeTime > fSimulatorControlTime )
	{
		fStartTimeCorrection += runtimeTime - fSimulatorControlTime;
	}
	fSimulatorControlTime = 0;
	fSimulatorControlLastTickTime = 0;
	fSimulatorControlPaused = false;
	fSimulatorControlTimeConsumed = false;
}

#endif

void
Runtime::Collect()
{
	LuaContext& vm = VMContext();
	
	// Lua
	vm.Collect();

	// Display objects
	GroupObject::CollectUnreachables( vm.L(), GetDisplay().GetScene(), * GetDisplay().Orphanage() );
}

void
Runtime::SetProperty( U16 mask, bool value )
{
	const U16 p = fProperties;
	fProperties = ( value ? p | mask : p & ~mask );
}

void
Runtime::SetSuspendOverrideProperty( U32 mask, bool value )
{
	const U32 p = fSuspendOverrideProperties;
	fSuspendOverrideProperties = ( value ? p | mask : p & ~mask );
}

PlatformExitCallback*
Runtime::GetExitCallback()
{
	MPlatform* platform = const_cast<MPlatform*>(&fPlatform);
	return platform->GetExitCallback();
}

void 
Runtime::UnloadResources()
{
#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "> ReleaseTextures" ) );
#endif

	fDisplay->UnloadResources();

	// Iterate through all resources
	lua_State *L = PushResourceRegistry(); Rtt_ASSERT( L );
	int t = lua_gettop( L ); // Need stable index for table

	lua_pushnil( L );
	while ( lua_next( L, t ) )
	{
		CachedResource* resource = (CachedResource*)lua_touserdata( L, -1 ); Rtt_ASSERT( ! resource || lua_type( L, -1 ) == LUA_TLIGHTUSERDATA );
		lua_pop( L, 1 );
	}

#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "< ReleaseTextures" ) );
#endif
}

void 
Runtime::ReloadResources()
{
#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "> ReloadTextures" ) );
#endif

	fDisplay->ReloadResources();

	// Iterate through all resources
	lua_State *L = PushResourceRegistry(); Rtt_ASSERT( L );
	int t = lua_gettop( L ); // Need stable index for table

	lua_pushnil( L );
	while ( lua_next( L, t ) )
	{
		CachedResource* resource = (CachedResource*)lua_touserdata( L, -1 ); Rtt_ASSERT( ! resource || lua_type( L, -1 ) == LUA_TLIGHTUSERDATA );
		lua_pop( L, 1 );
	}

	// Force rebuild of entire display
	fDisplay->GetStage()->Invalidate( DisplayObject::kRenderDirty );
	
#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "< ReloadTextures" ) );
#endif
}

// TODO: Remove this wrapper
void
Runtime::RestartRenderer()
{
	fDisplay->Restart();
}

CachedResource*
Runtime::LookupResource( const char key[] )
{
// Null key is valid
//	Rtt_ASSERT( key );
	if ( !key )
		return NULL;

	lua_State *L = PushResourceRegistry(); Rtt_ASSERT( L );
	lua_getfield( L, -1, key );
	CachedResource* result = (CachedResource*)lua_touserdata( L, -1 ); Rtt_ASSERT( ! result || lua_type( L, -1 ) == LUA_TLIGHTUSERDATA );
	lua_pop( L, 2 );

	return result;
}

void
Runtime::SetResource( CachedResource* resource, const char key[] )
{
	lua_State *L = PushResourceRegistry();

#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "> Runtime::SetResource" ) );
#endif

	// Library is only valid when the Lua state exists. No-op for invalid lib.
	if ( L )
	{
		Rtt_ASSERT( key );

		if ( resource )
		{
			// Add
			lua_pushlightuserdata( L, resource );

#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "+ Runtime::Set" ) );
#endif
			CachedResource* r = resource;
			fResourcesHead->Prepend( r ); // Put at end of list (MRU)
		}
		else
		{
			CachedResource* r = LookupResource( key );
			if ( r )
			{
#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "- Runtime::Set" ) );
#endif
				r->Remove();
			}
		
			// Remove
			lua_pushnil( L );
		}

		lua_setfield( L, -2, key );
		lua_pop( L, 1 );
	}
#if defined( Rtt_ANDROID_ENV ) && defined( Rtt_DEBUG )
	Rtt_TRACE( ( "< Runtime::SetResource" ) );
#endif
}

void
Runtime::MarkRecentlyUsed( CachedResource& resource )
{
	// Move to end of list (MRU).
	fResourcesHead->Prepend( & resource );
}

Rtt_Allocator*
Runtime::GetAllocator() const
{
	return Allocator();
}

lua_State*
Runtime::PushResourceRegistry()
{
	lua_State *L = fVMContext ? fVMContext->L() : NULL;

	if ( L )
	{
		lua_pushlightuserdata( L, this );
		lua_gettable( L, LUA_REGISTRYINDEX );

		// Create resource registry
		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 ); // pop nil

			lua_newtable( L ); // t

			lua_pushlightuserdata( L, this );
			lua_pushvalue( L, -2 );		
			lua_settable( L, LUA_REGISTRYINDEX ); // LUA_REGISTRY[this] = t
		}
	}

	return L;
}

#ifdef Rtt_WIN_ENV

void
Runtime::Step()
{
	// Advance the simulation by one fixed logic tick.
	// Runs the scheduler, dispatches enterFrame to Lua, updates physics and
	// display object state. Does NOT render; render-only refreshes are driven
	// by WM_PAINT when renderSync is enabled.
	RuntimeGuard guard( * this );

	if ( ! Rtt_VERIFY( fDisplay ) )
	{
		return;
	}

#if defined(Rtt_AUTHORING_SIMULATOR)
	SimulatorControl::Process( *this );
	if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
	{
		return;
	}
	if ( ! SimulatorControl::ShouldRunRuntimeFrame( *this ) )
	{
		return;
	}
#endif

	const bool wasSuspended = IsSuspended();
	fScheduler->Run();
#if defined(Rtt_AUTHORING_SIMULATOR)
	if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
	{
		return;
	}
#endif
	const bool isSuspended = IsSuspended();
	if( wasSuspended != isSuspended && isSuspended )
	{
		// This condition is written inverse for better understanding
		// Sometimes scheduled tasks can suspend Runtime
		// In that case (suspension state is changed and it is suspended), skip Display update
		return;
	}

#if defined(Rtt_AUTHORING_SIMULATOR)
	if (m_fAsyncResultStr.load()) {
		FinalizeWorkingThreadWithEvent(this, fVMContext->L());
	}
	if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
	{
		return;
	}
#endif

	fDisplay->Update();
#if defined(Rtt_AUTHORING_SIMULATOR)
	if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
	{
		return;
	}
#endif
	++fFrame;	
}

void
Runtime::operator()()
{
	// Legacy shim � keeps the Simulator and non-DWM path working unchanged.
	// Calls Step() followed by Render() in a single synchronous tick,
	// matching the original pre-decoupling behavior exactly.
	Step();

	if (!IsProperty(kRenderAsync))
	{
		Render();
	}
}

#else // ! Rtt_WIN_ENV

void
Runtime::operator()()
{
	// Original implementation preserved exactly for all non-Windows platforms.
	// Do not modify this block without testing on the target platform.
	RuntimeGuard guard(*this);

	if (!Rtt_VERIFY(fDisplay))
	{
		return;
	}

#if defined(Rtt_AUTHORING_SIMULATOR)
	SimulatorControl::Process( *this );
	if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
	{
		return;
	}
	if ( ! SimulatorControl::ShouldRunRuntimeFrame( *this ) )
	{
		return;
	}
#endif

	const bool wasSuspended = IsSuspended();
	fScheduler->Run();
#if defined(Rtt_AUTHORING_SIMULATOR)
	if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
	{
		return;
	}
#endif
	const bool isSuspended = IsSuspended();
	if (wasSuspended != isSuspended && isSuspended)
	{
		// This condition is written inverse for better understanding
		// Sometimes (Splash Screen is shown) scheduled tasks can suspend Runtime
		// In that case (suspension state is changed and it is suspended), skip Display update
	}
	else
	{
#if defined(Rtt_AUTHORING_SIMULATOR)
		if (m_fAsyncResultStr.load()) {
			FinalizeWorkingThreadWithEvent(this, fVMContext->L());
		}
		if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
		{
			return;
		}
#endif
		fDisplay->Update();

#if defined(Rtt_AUTHORING_SIMULATOR)
		if ( SimulatorControl::IsRuntimeErrorHalted( *this ) )
		{
			return;
		}
#endif
		++fFrame;
	}

	if (!IsProperty(kRenderAsync))
	{
		fDisplay->Render();
	}
}

#endif // Rtt_WIN_ENV

void
Runtime::Render()
{
	RuntimeGuard guard( * this );

	if ( ! Rtt_VERIFY( fDisplay ) )
	{
		return;
	}

	fDisplay->Render();
}

void
Runtime::Begin() const
{
	fPlatform.BeginRuntime( * this );
}

void
Runtime::End() const
{
	fPlatform.EndRuntime( * this );
}

// ----------------------------------------------------------------------------

RuntimeGuard::RuntimeGuard( Runtime& runtime )
:	fRuntime( runtime ),
	fPlatform( runtime.Platform() ),
	fLogSink( LuaContext::QueueEngineLog, &runtime.VMContext() )
{
	fPlatform.BeginRuntime( runtime );
}

RuntimeGuard::~RuntimeGuard()
{
	// Collect orphaned display objects
	fRuntime.GetDisplay().Collect( fRuntime.VMContext().L() );

	fPlatform.EndRuntime( fRuntime );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
