//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Data.h"
#include "Core/Rtt_String.h"

#include "Rtt_SimulatorControl.h"

#ifdef Rtt_AUTHORING_SIMULATOR

#include "Display/Rtt_BitmapPaint.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_DisplayObject.h"
#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_PlatformBitmap.h"
#include "Display/Rtt_StageObject.h"
#include "Display/Rtt_TextureFactory.h"
#include "Input/Rtt_PlatformInputAxis.h"
#include "Input/Rtt_PlatformInputDevice.h"
#include "Input/Rtt_PlatformInputDeviceManager.h"
#include "Input/Rtt_ReadOnlyInputAxisCollection.h"
#include "Input/Rtt_ReadOnlyInputDeviceCollection.h"
#include "Rtt_Event.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaProxy.h"
#include "Rtt_MPlatform.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_MSimulatorHost.h"
#include "Rtt_Runtime.h"
#include "Renderer/Rtt_Renderer.h"

#include <algorithm>
#include <ctype.h>
#include <deque>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits.h>
#include <map>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <utility>
#include <vector>

#ifdef Rtt_WIN_ENV
	#include <io.h>
	#include <process.h>
	#include <windows.h>
#else
	#include <dirent.h>
	#include <pthread.h>
	#include <signal.h>
	#include <sys/time.h>
	#include <unistd.h>
#endif

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static int
AbsoluteLuaIndex( lua_State *L, int index )
{
	return index > 0 || index <= LUA_REGISTRYINDEX ? index : lua_gettop( L ) + index + 1;
}

static bool
IsFiniteNumber( lua_Number value )
{
	return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

static SimulatorControllerInputDevice*
GetSimulatorController(
	Runtime& runtime, const MSimulatorHost::Input& input,
	bool create )
{
	PlatformInputDeviceManager& manager =
		runtime.Platform().GetDevice().GetInputDeviceManager();
	const ReadOnlyInputDeviceCollection& devices = manager.GetDevices();
	for ( int index = 0; index < devices.GetCount(); index++ )
	{
		PlatformInputDevice *device = devices.GetByIndex( index );
		const char *driverName = device ? device->GetDriverName() : NULL;
		if ( driverName && ! strcmp( driverName, "simulator" ) &&
			device->GetDescriptor().GetDeviceType().Equals(
				InputDeviceType::kGamepad ) )
		{
			SimulatorControllerInputDevice *simulatorController =
				static_cast< SimulatorControllerInputDevice* >( device );
			if ( simulatorController->HasIdentifier(
				input.controllerId.c_str() ) )
			{
				return simulatorController;
			}
		}
	}
	return create ? manager.AddSimulatorController(
		input.controllerId.c_str(),
		input.hasControllerProfile ?
			input.controllerProfile.c_str() : "xbox",
		input.hasControllerPlayerNumber ?
			input.controllerPlayerNumber : 0 ) : NULL;
}

bool
SimulatorControl::DispatchControllerInput(
	Runtime& runtime, const MSimulatorHost::Input& input )
{
	if ( MSimulatorHost::Input::kControllerInput != input.type )
	{
		return false;
	}

	const bool shouldCreate =
		MSimulatorHost::Input::kDisconnectController != input.controllerAction;
	SimulatorControllerInputDevice *device =
		GetSimulatorController( runtime, input, shouldCreate );
	if ( ! device )
	{
		return ! shouldCreate;
	}

	const bool wasConnected = device->GetConnectionState().IsConnected();
	if ( MSimulatorHost::Input::kDisconnectController == input.controllerAction )
	{
		if ( wasConnected )
		{
			device->SetConnected( false );
			InputDeviceStatusEvent event( device, true, false );
			runtime.DispatchEvent( event );
		}
		return true;
	}

	if ( MSimulatorHost::Input::kConnectController == input.controllerAction )
	{
		const bool wasReconfigured = device->Configure(
			input.hasControllerProfile ?
				input.controllerProfile.c_str() : NULL,
			input.hasControllerPlayerNumber ?
				input.controllerPlayerNumber : 0 );
		if ( ! wasConnected || wasReconfigured )
		{
			device->SetConnected( true );
			InputDeviceStatusEvent event(
				device, ! wasConnected, wasReconfigured );
			runtime.DispatchEvent( event );
		}
		return true;
	}
	if ( ! wasConnected )
	{
		return false;
	}
	if ( MSimulatorHost::Input::kButtonController == input.controllerAction )
	{
		KeyEvent::Phase phase;
		if ( MSimulatorHost::Input::kDownPhase == input.phase )
		{
			phase = KeyEvent::kDown;
		}
		else if ( MSimulatorHost::Input::kUpPhase == input.phase )
		{
			phase = KeyEvent::kUp;
		}
		else
		{
			return false;
		}
		KeyEvent event(
			device, phase, input.keyName.c_str(), 0,
			false, false, false, false );
		runtime.DispatchEvent( event );
		return true;
	}
	if ( MSimulatorHost::Input::kAxisController == input.controllerAction )
	{
		const ReadOnlyInputAxisCollection& axes = device->GetAxes();
		for ( int index = 0; index < axes.GetCount(); index++ )
		{
			PlatformInputAxis *axis = axes.GetByIndex( index );
			if ( axis && 0 == strcmp(
				axis->GetType().GetStringId(), input.axisName.c_str() ) )
			{
				AxisEvent event(
					device, axis,
					Rtt_FloatToReal( (float)input.axisValue ) );
				runtime.DispatchEvent( event );
				return true;
			}
		}
	}
	return false;
}

static const size_t kSimulatorControlMaximumRequestSize = 1024 * 1024;
static const size_t kSimulatorControlMaximumExecFileSize = 16 * 1024 * 1024;
static const size_t kSimulatorControlMaximumStringSize = 4096;
static const size_t kSimulatorControlMaximumResponseSize = 256 * 1024;
// Full Lua results are transferred intact, or rejected explicitly.
static const size_t kSimulatorControlMaximumJsonResultSize = 16 * 1024 * 1024;
static const size_t kSimulatorControlMaximumDiagnosticMessageSize = 16 * 1024;
static const size_t kSimulatorControlMaximumDiagnosticStackTraceSize = 128 * 1024;
static const size_t kSimulatorControlMaximumDiagnosticContextSize = 96 * 1024;
static const size_t kSimulatorControlMaximumDiagnosticValueStringSize = 256;
static const size_t kSimulatorControlMaximumLogBytes = 128 * 1024;
static const size_t kSimulatorControlMaximumLogEntries = 500;
static const int kSimulatorControlMaximumEntries = 100;
static const int kSimulatorControlMaximumDiagnosticFrames = 12;
static const int kSimulatorControlMaximumDiagnosticLocals = 12;
static const int kSimulatorControlMaximumDiagnosticUpvalues = 8;
static const int kSimulatorControlMaximumDiagnosticTableEntries = 3;
static const int kSimulatorControlMaximumDebuggerFrames = 64;
static const int kSimulatorControlMaximumDebuggerValues = 100;
static const unsigned long kSimulatorControlMaximumStepFrames = 1000;
static const size_t kSimulatorControlMaximumFrameTimingSamples = 120;
static char kSimulatorControlRegistryKey;

static unsigned long long GetSimulatorControlMilliseconds();
static unsigned long long GetSimulatorControlTimestampMilliseconds();
static bool MakeSimulatorControlAbsolutePath(
	const std::string& path, std::string& result );
static int RegisterSimulatorControlHandle( lua_State *L, int valueIndex );
static void SimulatorControlLuaHook( lua_State *L, lua_Debug *information );
struct SimulatorControlRuntimeState;
static void UpdateSimulatorControlLuaHook(
	Runtime& runtime, SimulatorControlRuntimeState& state );

class SimulatorControlLogMutex
{
	public:
		SimulatorControlLogMutex()
		{
#ifdef Rtt_WIN_ENV
			InitializeCriticalSection( & fMutex );
#else
			pthread_mutex_init( & fMutex, NULL );
#endif
		}

		~SimulatorControlLogMutex()
		{
#ifdef Rtt_WIN_ENV
			DeleteCriticalSection( & fMutex );
#else
			pthread_mutex_destroy( & fMutex );
#endif
		}

		void Lock()
		{
#ifdef Rtt_WIN_ENV
			EnterCriticalSection( & fMutex );
#else
			pthread_mutex_lock( & fMutex );
#endif
		}

		void Unlock()
		{
#ifdef Rtt_WIN_ENV
			LeaveCriticalSection( & fMutex );
#else
			pthread_mutex_unlock( & fMutex );
#endif
		}

	private:
#ifdef Rtt_WIN_ENV
		CRITICAL_SECTION fMutex;
#else
		pthread_mutex_t fMutex;
#endif
};

class SimulatorControlLogGuard
{
	public:
		SimulatorControlLogGuard( SimulatorControlLogMutex& mutex )
		:	fMutex( mutex )
		{
			fMutex.Lock();
		}

		~SimulatorControlLogGuard()
		{
			fMutex.Unlock();
		}

	private:
		SimulatorControlLogMutex& fMutex;
};

struct SimulatorControlLogEntry
{
	unsigned long sequence;
	unsigned long long timestampMilliseconds;
	std::string message;
};

struct SimulatorControlLogState
{
	SimulatorControlLogState()
	:	sequence( 0 ),
		byteCount( 0 ),
		droppedEntries( false )
	{
	}

	unsigned long sequence;
	size_t byteCount;
	bool droppedEntries;
	std::deque< SimulatorControlLogEntry > entries;
};

static SimulatorControlLogMutex&
GetSimulatorControlLogMutex()
{
	static SimulatorControlLogMutex *mutex =
		new SimulatorControlLogMutex();
	return *mutex;
}

static SimulatorControlLogState&
GetSimulatorControlLogState()
{
	static SimulatorControlLogState *state =
		new SimulatorControlLogState();
	return *state;
}

static void
ResetSimulatorControlLogs()
{
	SimulatorControlLogGuard guard( GetSimulatorControlLogMutex() );
	SimulatorControlLogState& state = GetSimulatorControlLogState();
	state.sequence = 0;
	state.byteCount = 0;
	state.droppedEntries = false;
	state.entries.clear();
}

static void
RecordSimulatorControlLog(
	const char *message, size_t length, void *context )
{
	(void)context;
	if ( ! message || 0 == length )
	{
		return;
	}
	while ( length > 0 &&
		( '\n' == message[length - 1] || '\r' == message[length - 1] ) )
	{
		length--;
	}
	if ( 0 == length )
	{
		return;
	}

	SimulatorControlLogGuard guard( GetSimulatorControlLogMutex() );
	SimulatorControlLogState& state = GetSimulatorControlLogState();
	SimulatorControlLogEntry entry;
	entry.sequence = ++state.sequence;
	entry.timestampMilliseconds = GetSimulatorControlTimestampMilliseconds();
	entry.message.assign( message, length );
	state.byteCount += entry.message.length();
	state.entries.push_back( entry );

	while ( state.entries.size() > kSimulatorControlMaximumLogEntries ||
		state.byteCount > kSimulatorControlMaximumLogBytes )
	{
		state.byteCount -= state.entries.front().message.length();
		state.entries.pop_front();
		state.droppedEntries = true;
	}
}

static std::string&
GetSimulatorControlDirectory()
{
	static std::string directory;
	return directory;
}

static std::string&
GetSimulatorControlOwnerPath()
{
	static std::string path;
	return path;
}

struct SimulatorControlRuntimeState
{
	SimulatorControlRuntimeState()
	:	generation( 0 ),
		diagnosticSequence( 0 ),
		diagnosticFrame( 0 ),
		debuggerPauseSequence( 0 ),
		debuggerBreakpointId( 0 ),
		debuggerLine( 0 ),
		debuggerStepDepth( 0 ),
		stepFramesRemaining( 0 ),
		controlledInputDispatchDepth( 0 ),
		lastFrameStartElapsedMilliseconds( 0.0 ),
		debuggerThread( NULL ),
		debuggerStepThread( NULL ),
		hasDiagnostic( false ),
		diagnosticTruncated( false ),
		runtimeErrorHalted( false ),
		controlPaused( false ),
		debuggerPaused( false ),
		debuggerOwnsRuntimeClock( false ),
		debuggerHookInstalled( false ),
		debuggerEvaluating( false ),
		stepStartPending( false ),
		hasLastFrameStart( false ),
		rendererStatisticsRequested( false ),
		diagnosticsWritten( false ),
		sessionWritten( false )
	{
	}

	unsigned long generation;
	unsigned long diagnosticSequence;
	unsigned long diagnosticFrame;
	unsigned long debuggerPauseSequence;
	unsigned long debuggerBreakpointId;
	int debuggerLine;
	int debuggerStepDepth;
	unsigned long stepFramesRemaining;
	int controlledInputDispatchDepth;
	double lastFrameStartElapsedMilliseconds;
	lua_State *debuggerThread;
	lua_State *debuggerStepThread;
	std::string directory;
	std::string sessionId;
	std::string resourceDirectory;
	std::string debuggerPauseReason;
	std::string debuggerSource;
	std::string debuggerProjectSource;
	std::string debuggerFunction;
	std::string debuggerStepMode;
	std::string debuggerConditionError;
	std::map< std::string, std::string > debuggerSourcePaths;
	std::string diagnosticType;
	std::string diagnosticMessage;
	std::string diagnosticStackTrace;
	std::string diagnosticContext;
	std::deque< double > frameIntervalsMilliseconds;
	bool hasDiagnostic;
	bool diagnosticTruncated;
	bool runtimeErrorHalted;
	bool controlPaused;
	bool debuggerPaused;
	bool debuggerOwnsRuntimeClock;
	bool debuggerHookInstalled;
	bool debuggerEvaluating;
	bool stepStartPending;
	bool hasLastFrameStart;
	bool rendererStatisticsRequested;
	bool diagnosticsWritten;
	bool sessionWritten;
};

struct SimulatorControlBreakpoint
{
	SimulatorControlBreakpoint()
	:	id( 0 ),
		line( 0 ),
		hitCount( 0 ),
		hits( 0 )
	{
	}

	unsigned long id;
	int line;
	unsigned long hitCount;
	unsigned long hits;
	std::string path;
	std::string comparisonPath;
	std::string condition;
};

struct SimulatorControlDebuggerConfiguration
{
	SimulatorControlDebuggerConfiguration()
	:	nextBreakpointId( 0 )
	{
	}

	unsigned long nextBreakpointId;
	std::vector< SimulatorControlBreakpoint > breakpoints;
};

static SimulatorControlDebuggerConfiguration&
GetSimulatorControlDebuggerConfiguration()
{
	static SimulatorControlDebuggerConfiguration configuration;
	return configuration;
}

struct SimulatorControlSession
{
	SimulatorControlSession()
	:	generation( 0 ),
		processId( 0 )
	{
	}

	unsigned long generation;
	unsigned long processId;
	std::string sessionId;
};

typedef std::map< const Runtime*, SimulatorControlRuntimeState >
	SimulatorControlRuntimeStateMap;

static SimulatorControlRuntimeStateMap&
GetSimulatorControlRuntimeStates()
{
	static SimulatorControlRuntimeStateMap states;
	return states;
}

static unsigned long
NextSimulatorControlGeneration()
{
	static unsigned long generation = 0;
	return ++generation;
}

static long
GetSimulatorControlProcessId()
{
#ifdef Rtt_WIN_ENV
	return (long)_getpid();
#else
	return (long)getpid();
#endif
}

static std::string
SimulatorControlPath( const std::string& directory, const char *filename )
{
	if ( directory.empty() )
	{
		return std::string();
	}

	char lastCharacter = directory[directory.length() - 1];
	if ( '/' == lastCharacter || '\\' == lastCharacter )
	{
		return directory + filename;
	}

#ifdef Rtt_WIN_ENV
	return directory + "\\" + filename;
#else
	return directory + "/" + filename;
#endif
}

#ifdef Rtt_WIN_ENV

static bool
ConvertSimulatorControlUtf8ToWide(
	const std::string& value, std::wstring& result )
{
	if ( value.empty() )
	{
		result.clear();
		return true;
	}
	int length = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS,
		value.data(), (int)value.length(), NULL, 0 );
	if ( length <= 0 )
	{
		return false;
	}
	result.resize( (size_t)length );
	return length == MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS,
		value.data(), (int)value.length(), & result[0], length );
}

static bool
ConvertSimulatorControlWideToUtf8(
	const std::wstring& value, std::string& result )
{
	if ( value.empty() )
	{
		result.clear();
		return true;
	}
	int length = WideCharToMultiByte(
		CP_UTF8, WC_ERR_INVALID_CHARS,
		value.data(), (int)value.length(), NULL, 0, NULL, NULL );
	if ( length <= 0 )
	{
		return false;
	}
	result.resize( (size_t)length );
	return length == WideCharToMultiByte(
		CP_UTF8, WC_ERR_INVALID_CHARS,
		value.data(), (int)value.length(), & result[0], length, NULL, NULL );
}

static void
SetSimulatorControlErrnoFromWindowsError( DWORD error )
{
	switch ( error )
	{
		case ERROR_FILE_EXISTS:
		case ERROR_ALREADY_EXISTS:
			errno = EEXIST;
			break;
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			errno = ENOENT;
			break;
		case ERROR_ACCESS_DENIED:
			errno = EACCES;
			break;
		default:
			errno = EIO;
			break;
	}
}

#endif

static int
DeleteSimulatorControlFile( const std::string& path )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		errno = EINVAL;
		return -1;
	}
	if ( DeleteFileW( widePath.c_str() ) )
	{
		return 0;
	}
	SetSimulatorControlErrnoFromWindowsError( GetLastError() );
	return -1;
#else
	return remove( path.c_str() );
#endif
}

static int
MoveSimulatorControlFile(
	const std::string& sourcePath, const std::string& destinationPath,
	bool replaceExisting )
{
#ifdef Rtt_WIN_ENV
	std::wstring wideSourcePath;
	std::wstring wideDestinationPath;
	if ( ! ConvertSimulatorControlUtf8ToWide(
			sourcePath, wideSourcePath ) ||
		! ConvertSimulatorControlUtf8ToWide(
			destinationPath, wideDestinationPath ) )
	{
		errno = EINVAL;
		return -1;
	}
	DWORD flags = replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0;
	if ( MoveFileExW(
		wideSourcePath.c_str(), wideDestinationPath.c_str(), flags ) )
	{
		return 0;
	}
	SetSimulatorControlErrnoFromWindowsError( GetLastError() );
	return -1;
#else
	if ( ! replaceExisting )
	{
		struct stat information;
		if ( 0 == stat( destinationPath.c_str(), & information ) )
		{
			errno = EEXIST;
			return -1;
		}
	}
	return rename( sourcePath.c_str(), destinationPath.c_str() );
#endif
}

static bool
ReadSimulatorControlFile(
	const std::string& path, std::string& result,
	size_t maximumSize = kSimulatorControlMaximumRequestSize )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		return false;
	}
	HANDLE fileHandle = CreateFileW(
		widePath.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( INVALID_HANDLE_VALUE == fileHandle )
	{
		return false;
	}

	LARGE_INTEGER length;
	if ( ! GetFileSizeEx( fileHandle, & length ) ||
		length.QuadPart < 0 ||
		(unsigned long long)length.QuadPart >
			maximumSize )
	{
		CloseHandle( fileHandle );
		return false;
	}

	result.resize( (size_t)length.QuadPart );
	DWORD readLength = 0;
	bool succeeded = result.empty() ||
		( ReadFile(
			fileHandle, & result[0], (DWORD)result.length(),
			& readLength, NULL ) &&
			readLength == result.length() );
	CloseHandle( fileHandle );
	if ( ! succeeded )
	{
		result.clear();
	}
	return succeeded;
#else
	std::ifstream stream( path.c_str(), std::ios::in | std::ios::binary );
	if ( ! stream )
	{
		return false;
	}

	stream.seekg( 0, std::ios::end );
	std::streamoff length = stream.tellg();
	if ( length < 0 || (size_t)length > maximumSize )
	{
		return false;
	}
	stream.seekg( 0, std::ios::beg );

	result.assign( (std::istreambuf_iterator< char >( stream )), std::istreambuf_iterator< char >() );
	return stream.good() || stream.eof();
#endif
}

static bool
WriteSimulatorControlFileAtomically( const std::string& path, const std::string& contents )
{
	std::string temporaryPath = path + ".tmp";
#ifdef Rtt_WIN_ENV
	std::wstring wideTemporaryPath;
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide(
			temporaryPath, wideTemporaryPath ) ||
		! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		errno = EINVAL;
		return false;
	}
	HANDLE fileHandle = CreateFileW(
		wideTemporaryPath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( INVALID_HANDLE_VALUE == fileHandle )
	{
		SetSimulatorControlErrnoFromWindowsError( GetLastError() );
		return false;
	}
	DWORD writtenLength = 0;
	BOOL didWrite = contents.empty() || WriteFile(
		fileHandle, contents.data(), (DWORD)contents.length(), & writtenLength, NULL );
	bool succeeded = didWrite && writtenLength == contents.length();
	DWORD writeError = didWrite ? ERROR_WRITE_FAULT : GetLastError();
	CloseHandle( fileHandle );
	if ( ! succeeded )
	{
		DeleteFileW( wideTemporaryPath.c_str() );
		SetSimulatorControlErrnoFromWindowsError( writeError );
		return false;
	}
	if ( ! MoveFileExW( wideTemporaryPath.c_str(), widePath.c_str(), MOVEFILE_REPLACE_EXISTING ) )
	{
		DWORD moveError = GetLastError();
		DeleteFileW( wideTemporaryPath.c_str() );
		SetSimulatorControlErrnoFromWindowsError( moveError );
		return false;
	}
	return true;
#else
	std::ofstream stream( temporaryPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc );
	if ( ! stream )
	{
		return false;
	}
	stream.write( contents.data(), (std::streamsize)contents.length() );
	stream.close();
	if ( ! stream )
	{
		int writeError = errno ? errno : EIO;
		DeleteSimulatorControlFile( temporaryPath );
		errno = writeError;
		return false;
	}
	if ( 0 != MoveSimulatorControlFile( temporaryPath, path, true ) )
	{
		int moveError = errno;
		DeleteSimulatorControlFile( temporaryPath );
		errno = moveError;
		return false;
	}
	return true;
#endif
}

static unsigned long long
GetSimulatorControlMilliseconds()
{
#ifdef Rtt_WIN_ENV
	return (unsigned long long)GetTickCount64();
#else
	struct timeval value;
	gettimeofday( & value, NULL );
	return ( (unsigned long long)value.tv_sec * 1000ULL ) +
		( (unsigned long long)value.tv_usec / 1000ULL );
#endif
}

static unsigned long long
GetSimulatorControlTimestampMilliseconds()
{
#ifdef Rtt_WIN_ENV
	FILETIME fileTime;
	ULARGE_INTEGER value;
	GetSystemTimeAsFileTime( &fileTime );
	value.LowPart = fileTime.dwLowDateTime;
	value.HighPart = fileTime.dwHighDateTime;
	static const unsigned long long kWindowsToUnixEpoch =
		116444736000000000ULL;
	return value.QuadPart >= kWindowsToUnixEpoch ?
		( value.QuadPart - kWindowsToUnixEpoch ) / 10000ULL : 0;
#else
	struct timeval value;
	gettimeofday( & value, NULL );
	return ( (unsigned long long)value.tv_sec * 1000ULL ) +
		( (unsigned long long)value.tv_usec / 1000ULL );
#endif
}

static void
SleepSimulatorControlMilliseconds( unsigned long milliseconds )
{
#ifdef Rtt_WIN_ENV
	Sleep( (DWORD)milliseconds );
#else
	usleep( (useconds_t)( milliseconds * 1000UL ) );
#endif
}

static bool
IsSimulatorControlProcessAlive( unsigned long processId )
{
	if ( 0 == processId )
	{
		return false;
	}
	if ( processId == (unsigned long)GetSimulatorControlProcessId() )
	{
		return true;
	}

#ifdef Rtt_WIN_ENV
	HANDLE processHandle = OpenProcess(
		SYNCHRONIZE, FALSE, (DWORD)processId );
	if ( processHandle )
	{
		DWORD waitResult = WaitForSingleObject( processHandle, 0 );
		CloseHandle( processHandle );
		return WAIT_TIMEOUT == waitResult;
	}

	// If Windows refuses access, assume the process is alive. It is safer to
	// leave a lock or session in place than to steal one from a live process.
	return ERROR_ACCESS_DENIED == GetLastError();
#else
	if ( 0 == kill( (pid_t)processId, 0 ) )
	{
		return true;
	}
	return EPERM == errno;
#endif
}

static std::string
CreateSimulatorControlSessionId( unsigned long generation )
{
	static unsigned long counter = 0;
	char value[128];
	snprintf(
		value, sizeof( value ), "%08lx-%08lx-%016llx-%08lx",
		(unsigned long)GetSimulatorControlProcessId(),
		(unsigned long)time( NULL ),
		GetSimulatorControlMilliseconds(),
		generation + ++counter );
	return std::string( value );
}

static SimulatorControlRuntimeState*
GetOrCreateSimulatorControlRuntimeState( Runtime& runtime )
{
	const std::string& directory = GetSimulatorControlDirectory();
	if ( directory.empty() )
	{
		return NULL;
	}

	SimulatorControlRuntimeStateMap& states =
		GetSimulatorControlRuntimeStates();
	SimulatorControlRuntimeStateMap::iterator iterator =
		states.find( & runtime );
	if ( states.end() == iterator )
	{
		SimulatorControlRuntimeState state;
		state.directory = directory;
		state.generation = NextSimulatorControlGeneration();
		state.sessionId =
			CreateSimulatorControlSessionId( state.generation );
		iterator = states.insert(
			std::make_pair( (const Runtime*)& runtime, state ) ).first;
	}
	return & iterator->second;
}

static SimulatorControlRuntimeState*
GetSimulatorControlRuntimeState( const Runtime *runtime )
{
	SimulatorControlRuntimeStateMap& states =
		GetSimulatorControlRuntimeStates();
	SimulatorControlRuntimeStateMap::iterator iterator =
		states.find( runtime );
	return states.end() == iterator ? NULL : & iterator->second;
}

static SimulatorControlRuntimeState*
GetSimulatorControlRuntimeState( Runtime& runtime )
{
	return GetSimulatorControlRuntimeState( & runtime );
}

static std::string
NormalizeSimulatorControlDebuggerPath( const char *path )
{
	std::string result( path ? path : "" );
	if ( ! result.empty() && '@' == result[0] )
	{
		result.erase( 0, 1 );
	}
	for ( size_t index = 0; index < result.length(); index++ )
	{
		if ( '\\' == result[index] )
		{
			result[index] = '/';
		}
#ifdef Rtt_WIN_ENV
		result[index] = (char)tolower( (unsigned char)result[index] );
#endif
	}
	while ( result.length() > 1 && '/' == result[result.length() - 1] )
	{
		result.erase( result.length() - 1 );
	}
	return result;
}

static std::string
CanonicalizeSimulatorControlDebuggerSource(
	SimulatorControlRuntimeState& state, const std::string& source )
{
	std::map< std::string, std::string >::const_iterator iterator =
		state.debuggerSourcePaths.find( source );
	if ( state.debuggerSourcePaths.end() != iterator )
	{
		return iterator->second;
	}

	std::string absoluteSource;
	std::string result = source;
	if ( MakeSimulatorControlAbsolutePath( source, absoluteSource ) )
	{
		result = NormalizeSimulatorControlDebuggerPath(
			absoluteSource.c_str() );
	}
	state.debuggerSourcePaths[source] = result;
	return result;
}

static std::string
GetSimulatorControlProjectSource(
	const SimulatorControlRuntimeState& state, const std::string& source )
{
	if ( state.resourceDirectory.empty() )
	{
		return std::string();
	}
	if ( source == state.resourceDirectory )
	{
		return std::string();
	}
	if ( source.length() > state.resourceDirectory.length() &&
		0 == source.compare(
			0, state.resourceDirectory.length(), state.resourceDirectory ) &&
		'/' == source[state.resourceDirectory.length()] )
	{
		return source.substr( state.resourceDirectory.length() + 1 );
	}
	return std::string();
}

static int
GetSimulatorControlLuaStackDepth( lua_State *L )
{
	int depth = 0;
	lua_Debug information;
	while ( lua_getstack( L, depth, & information ) )
	{
		depth++;
	}
	return depth;
}

static SimulatorControlBreakpoint*
FindSimulatorControlBreakpoint(
	const SimulatorControlRuntimeState& state,
	const std::string& source, int line )
{
	std::string projectSource =
		GetSimulatorControlProjectSource( state, source );
	SimulatorControlDebuggerConfiguration& configuration =
		GetSimulatorControlDebuggerConfiguration();
	for ( std::vector< SimulatorControlBreakpoint >::iterator iterator =
			configuration.breakpoints.begin();
		configuration.breakpoints.end() != iterator; iterator++ )
	{
		if ( iterator->line == line &&
			( iterator->comparisonPath == source ||
				( ! projectSource.empty() &&
					iterator->comparisonPath == projectSource ) ) )
		{
			return & *iterator;
		}
	}
	return NULL;
}

static void
AssignSimulatorControlDiagnosticText(
	std::string& result, const char *value,
	size_t maximumLength, bool& truncated )
{
	const char *safeValue = value ? value : "";
	size_t length = strlen( safeValue );
	if ( length > maximumLength )
	{
		length = maximumLength;
		truncated = true;
	}
	result.assign( safeValue, length );
}

static void
RecordSimulatorControlDiagnostic(
	Runtime& runtime, SimulatorControlRuntimeState& state,
	const char *errorType, const char *message, const char *stackTrace )
{
	state.diagnosticSequence++;
	state.diagnosticFrame = (unsigned long)runtime.GetFrame();
	state.diagnosticTruncated = false;
	AssignSimulatorControlDiagnosticText(
		state.diagnosticType, errorType ? errorType : "Runtime error",
		256, state.diagnosticTruncated );
	AssignSimulatorControlDiagnosticText(
		state.diagnosticMessage, message,
		kSimulatorControlMaximumDiagnosticMessageSize,
		state.diagnosticTruncated );
	AssignSimulatorControlDiagnosticText(
		state.diagnosticStackTrace, stackTrace,
		kSimulatorControlMaximumDiagnosticStackTraceSize,
		state.diagnosticTruncated );
	state.diagnosticContext.clear();
	state.hasDiagnostic = true;
	state.diagnosticsWritten = false;
}

static void
RecordSimulatorControlExecutionDiagnostic(
	Runtime& runtime, SimulatorControlRuntimeState& state,
	const std::string& error )
{
	size_t stackTraceOffset = error.find( "\nstack traceback:" );
	if ( std::string::npos == stackTraceOffset )
	{
		RecordSimulatorControlDiagnostic(
			runtime, state, "Simulator control error",
			error.c_str(), "" );
		return;
	}

	std::string message = error.substr( 0, stackTraceOffset );
	std::string stackTrace = error.substr( stackTraceOffset );
	RecordSimulatorControlDiagnostic(
		runtime, state, "Simulator control error",
		message.c_str(), stackTrace.c_str() );
}

static bool
SimulatorControlFileExists( const std::string& path )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		return false;
	}
	return INVALID_FILE_ATTRIBUTES !=
		GetFileAttributesW( widePath.c_str() );
#else
	struct stat information;
	return 0 == stat( path.c_str(), & information );
#endif
}

static bool
SimulatorControlDirectoryExists( const std::string& path )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		return false;
	}
	DWORD attributes = GetFileAttributesW( widePath.c_str() );
	return INVALID_FILE_ATTRIBUTES != attributes &&
		0 != ( attributes & FILE_ATTRIBUTE_DIRECTORY );
#else
	struct stat information;
	return 0 == stat( path.c_str(), & information ) &&
		S_ISDIR( information.st_mode );
#endif
}

static bool
IsSimulatorControlFileStale( const std::string& path, unsigned long seconds )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		return false;
	}
	struct _stat64 information;
	if ( 0 != _wstat64( widePath.c_str(), & information ) )
#else
	struct stat information;
	if ( 0 != stat( path.c_str(), & information ) )
#endif
	{
		return false;
	}
	return time( NULL ) - information.st_mtime > (time_t)seconds;
}

static bool
CreateSimulatorControlExclusivePidFile( const std::string& path )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		errno = EINVAL;
		return false;
	}
	HANDLE fileHandle = CreateFileW(
		widePath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( INVALID_HANDLE_VALUE == fileHandle )
	{
		SetSimulatorControlErrnoFromWindowsError( GetLastError() );
		return false;
	}
#else
	int descriptor = open( path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600 );
	if ( descriptor < 0 )
	{
		return false;
	}
#endif

	char contents[64];
	int length = snprintf(
		contents, sizeof( contents ), "%ld\n%lu\n",
		GetSimulatorControlProcessId(), (unsigned long)time( NULL ) );
#ifdef Rtt_WIN_ENV
	DWORD writtenLength = 0;
	BOOL didWrite = WriteFile(
		fileHandle, contents, (DWORD)length, & writtenLength, NULL );
	bool succeeded = didWrite && writtenLength == (DWORD)length;
	DWORD writeError = didWrite ? ERROR_WRITE_FAULT : GetLastError();
	CloseHandle( fileHandle );
	if ( ! succeeded )
	{
		SetSimulatorControlErrnoFromWindowsError( writeError );
	}
	int written = succeeded ? length : -1;
#else
	ssize_t written = write( descriptor, contents, (size_t)length );
	close( descriptor );
#endif
	if ( written != length )
	{
		int writeError = errno ? errno : EIO;
		DeleteSimulatorControlFile( path );
		errno = writeError;
		return false;
	}
	return true;
}

static bool
ReadSimulatorControlFileOwner(
	const std::string& path, unsigned long& processId )
{
	std::string contents;
	if ( ! ReadSimulatorControlFile( path, contents ) )
	{
		return false;
	}

	char *end = NULL;
	errno = 0;
	processId = strtoul( contents.c_str(), & end, 10 );
	return 0 != processId && ERANGE != errno && end &&
		end != contents.c_str() && ( '\n' == end[0] || '\r' == end[0] );
}

static void
ReleaseSimulatorControlDirectory()
{
	std::string& ownerPath = GetSimulatorControlOwnerPath();
	unsigned long ownerProcessId = 0;
	if ( ! ownerPath.empty() &&
		ReadSimulatorControlFileOwner( ownerPath, ownerProcessId ) &&
		ownerProcessId == (unsigned long)GetSimulatorControlProcessId() )
	{
		DeleteSimulatorControlFile( ownerPath );
	}
	ownerPath.clear();
}

static bool
MoveSimulatorControlFileAside( const std::string& path )
{
	static unsigned long counter = 0;
	char suffix[96];
	snprintf(
		suffix, sizeof( suffix ), ".stale.%ld.%llu.%lu",
		GetSimulatorControlProcessId(),
		GetSimulatorControlMilliseconds(), ++counter );
	std::string stalePath = path + suffix;
	if ( 0 != MoveSimulatorControlFile(
		path, stalePath, false ) )
	{
		return false;
	}
	DeleteSimulatorControlFile( stalePath );
	return true;
}

static bool
AcquireSimulatorControlLock(
	const std::string& directory, unsigned long timeoutMilliseconds,
	std::string& lockPath, std::string& error, const char **errorCode = NULL )
{
	lockPath = SimulatorControlPath( directory, ".simulator-control.lock" );
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	while ( GetSimulatorControlMilliseconds() < deadline )
	{
		if ( CreateSimulatorControlExclusivePidFile( lockPath ) )
		{
			return true;
		}
		if ( EEXIST != errno )
		{
			int fileError = errno;
			error = "unable to create the Simulator control lock at " + lockPath + ": " + strerror( fileError );
			if ( errorCode )
			{
				*errorCode = ( EACCES == fileError || EPERM == fileError ) ? "permission-denied" : "control-io-error";
			}
			return false;
		}

		unsigned long ownerProcessId = 0;
		if ( ReadSimulatorControlFileOwner( lockPath, ownerProcessId ) )
		{
			if ( ! IsSimulatorControlProcessAlive( ownerProcessId ) )
			{
				if ( MoveSimulatorControlFileAside( lockPath ) )
				{
					continue;
				}
			}
		}
		else if ( IsSimulatorControlFileStale( lockPath, 30 ) )
		{
			// Preserve compatibility with malformed or pre-owner lock files,
			// but never steal a valid lock from a process that is still alive.
			if ( MoveSimulatorControlFileAside( lockPath ) )
			{
				continue;
			}
		}
		SleepSimulatorControlMilliseconds( 20 );
	}
	error = "another Simulator control request held the lock for too long";
	if ( errorCode )
	{
		*errorCode = "control-busy";
	}
	return false;
}

static bool
MakeSimulatorControlAbsolutePath(
	const std::string& path, std::string& result )
{
#ifdef Rtt_WIN_ENV
	std::wstring widePath;
	if ( ! ConvertSimulatorControlUtf8ToWide( path, widePath ) )
	{
		return false;
	}
	DWORD requiredLength =
		GetFullPathNameW( widePath.c_str(), 0, NULL, NULL );
	if ( 0 == requiredLength )
	{
		return false;
	}
	std::wstring absolutePath( requiredLength, L'\0' );
	DWORD writtenLength = GetFullPathNameW(
		widePath.c_str(), requiredLength, & absolutePath[0], NULL );
	if ( 0 == writtenLength || writtenLength >= requiredLength )
	{
		return false;
	}
	absolutePath.resize( writtenLength );
	return ConvertSimulatorControlWideToUtf8( absolutePath, result );
#else
	char *absolutePath = realpath( path.c_str(), NULL );
	if ( ! absolutePath )
	{
		return false;
	}
	result.assign( absolutePath );
	free( absolutePath );
	return true;
#endif
}

static bool
MakeSimulatorControlAbsoluteOutputPath(
	const std::string& path, std::string& result )
{
	if ( path.empty() )
	{
		return false;
	}
#ifdef Rtt_WIN_ENV
	return MakeSimulatorControlAbsolutePath( path, result );
#else
	if ( '/' == path[0] )
	{
		result = path;
		return true;
	}
	char workingDirectory[PATH_MAX];
	if ( ! getcwd( workingDirectory, sizeof( workingDirectory ) ) )
	{
		return false;
	}
	result.assign( workingDirectory );
	result.push_back( '/' );
	result.append( path );
	return true;
#endif
}

static void
AppendSimulatorControlJsonString( std::string& result, const char *value, size_t length )
{
	static const char kHexadecimalDigits[] = "0123456789abcdef";
	result.push_back( '"' );
	for ( size_t index = 0; index < length; index++ )
	{
		unsigned char character = (unsigned char)value[index];
		switch ( character )
		{
			case '"': result.append( "\\\"" ); break;
			case '\\': result.append( "\\\\" ); break;
			case '\b': result.append( "\\b" ); break;
			case '\f': result.append( "\\f" ); break;
			case '\n': result.append( "\\n" ); break;
			case '\r': result.append( "\\r" ); break;
			case '\t': result.append( "\\t" ); break;
			default:
				if ( character < 0x20 )
				{
					result.append( "\\u00" );
					result.push_back( kHexadecimalDigits[( character >> 4 ) & 0x0f] );
					result.push_back( kHexadecimalDigits[character & 0x0f] );
				}
				else
				{
					result.push_back( (char)character );
				}
				break;
		}
	}
	result.push_back( '"' );
}

static void
AppendSimulatorControlJsonString( std::string& result, const std::string& value )
{
	AppendSimulatorControlJsonString( result, value.data(), value.length() );
}

static std::string
SimulatorControlErrorResponse( const std::string& message, const char *code = "invalid-request" )
{
	std::string result( "{\"ok\":false,\"error\":{\"message\":" );
	AppendSimulatorControlJsonString( result, message );
	result.append( ",\"code\":" );
	AppendSimulatorControlJsonString( result, code );
	result.append( "}}" );
	return result;
}

static std::string
SimulatorControlSuccessResponse( const std::string& jsonResult, bool fullJson = false )
{
	return std::string( "{\"ok\":true,\"result\":" ) + jsonResult +
		( fullJson ? ",\"encoding\":\"json\"}" : "}" );
}

static std::string
BuildSimulatorControlStatus(
	Runtime& runtime, const SimulatorControlRuntimeState& state )
{
	char result[640];
	const char *executionState = state.runtimeErrorHalted ?
		"error-halted" : ( state.debuggerPaused ?
			"debug-paused" : ( state.controlPaused ?
				"control-paused" : ( runtime.IsSuspended() ? "suspended" : "running" ) ) );
	snprintf(
		result, sizeof( result ),
		"{\"protocol\":2,\"sessionId\":\"%s\",\"generation\":%lu,"
		"\"pid\":%ld,\"frame\":%lu,"
		"\"applicationLoaded\":%s,\"applicationExecuting\":%s,"
		"\"executionState\":\"%s\",\"runtimeErrorHalted\":%s,"
		"\"controlPaused\":%s,\"stepFramesRemaining\":%lu,"
		"\"debuggerPaused\":%s,\"debuggerPauseSequence\":%lu,\"suspended\":%s}",
		state.sessionId.c_str(),
		state.generation,
		GetSimulatorControlProcessId(),
		(unsigned long)runtime.GetFrame(),
		runtime.IsProperty( Runtime::kIsApplicationLoaded ) ? "true" : "false",
		runtime.IsProperty( Runtime::kIsApplicationExecuting ) ? "true" : "false",
		executionState,
		state.runtimeErrorHalted ? "true" : "false",
		state.controlPaused ? "true" : "false",
		state.stepFramesRemaining,
		state.debuggerPaused ? "true" : "false",
		state.debuggerPauseSequence,
		runtime.IsSuspended() ? "true" : "false" );
	return std::string( result );
}

static void
AppendSimulatorControlJsonNumber( std::string& result, double value )
{
	char number[64];
	snprintf( number, sizeof( number ), "%.3f", value );
	result.append( number );
}

static std::string
BuildSimulatorControlRuntimeMetrics(
	Runtime& runtime, const SimulatorControlRuntimeState& state )
{
	const std::deque< double >& samples = state.frameIntervalsMilliseconds;
	double latestInterval = 0.0;
	double averageInterval = 0.0;
	double minimumInterval = 0.0;
	double maximumInterval = 0.0;
	double percentile95Interval = 0.0;
	if ( ! samples.empty() )
	{
		latestInterval = samples.back();
		minimumInterval = samples.front();
		maximumInterval = samples.front();
		std::vector< double > sortedSamples( samples.begin(), samples.end() );
		for ( size_t index = 0; index < samples.size(); index++ )
		{
			const double sample = samples[index];
			averageInterval += sample;
			minimumInterval = sample < minimumInterval ? sample : minimumInterval;
			maximumInterval = sample > maximumInterval ? sample : maximumInterval;
		}
		averageInterval /= (double)samples.size();
		std::sort( sortedSamples.begin(), sortedSamples.end() );
		size_t percentileIndex =
			( sortedSamples.size() * 95 + 99 ) / 100 - 1;
		percentile95Interval = sortedSamples[percentileIndex];
	}

	lua_State *L = runtime.VMContext().L();
	unsigned long long luaBytes = 0;
	if ( L )
	{
		const int kilobytes = lua_gc( L, LUA_GCCOUNT, 0 );
		const int remainingBytes = lua_gc( L, LUA_GCCOUNTB, 0 );
		if ( kilobytes >= 0 && remainingBytes >= 0 )
		{
			luaBytes = (unsigned long long)kilobytes * 1024ULL +
				(unsigned long long)remainingBytes;
		}
	}

	const S32 textureBytes =
		runtime.GetDisplay().GetTextureFactory().GetTextureMemoryUsed();
	Renderer& renderer = runtime.GetDisplay().GetRenderer();
	const bool statisticsEnabled = renderer.GetStatisticsEnabled();

	std::string result( "{\"sessionId\":" );
	AppendSimulatorControlJsonString( result, state.sessionId );
	char metadata[320];
	snprintf(
		metadata, sizeof( metadata ),
		",\"generation\":%lu,\"timestampMs\":%llu,\"frame\":%lu,"
		"\"runtimeElapsedMs\":",
		state.generation, GetSimulatorControlTimestampMilliseconds(),
		(unsigned long)runtime.GetFrame() );
	result.append( metadata );
	AppendSimulatorControlJsonNumber( result, runtime.GetElapsedMS() );

	char timing[192];
	snprintf(
		timing, sizeof( timing ),
		",\"frameTiming\":{\"configuredFps\":%lu,"
		"\"configuredFrameIntervalMs\":",
		(unsigned long)runtime.GetFPS() );
	result.append( timing );
	if ( runtime.GetFPS() > 0 )
	{
		AppendSimulatorControlJsonNumber(
			result, 1000.0 / (double)runtime.GetFPS() );
	}
	else
	{
		result.append( "null" );
	}
	snprintf(
		timing, sizeof( timing ),
		",\"sampleCount\":%lu,\"sampleCapacity\":%lu,"
		"\"latestFrameIntervalMs\":",
		(unsigned long)samples.size(),
		(unsigned long)kSimulatorControlMaximumFrameTimingSamples );
	result.append( timing );
	if ( samples.empty() )
	{
		result.append(
			"null,\"averageFrameIntervalMs\":null,"
			"\"minimumFrameIntervalMs\":null,"
			"\"maximumFrameIntervalMs\":null,"
			"\"p95FrameIntervalMs\":null,\"observedFps\":null}" );
	}
	else
	{
		AppendSimulatorControlJsonNumber( result, latestInterval );
		result.append( ",\"averageFrameIntervalMs\":" );
		AppendSimulatorControlJsonNumber( result, averageInterval );
		result.append( ",\"minimumFrameIntervalMs\":" );
		AppendSimulatorControlJsonNumber( result, minimumInterval );
		result.append( ",\"maximumFrameIntervalMs\":" );
		AppendSimulatorControlJsonNumber( result, maximumInterval );
		result.append( ",\"p95FrameIntervalMs\":" );
		AppendSimulatorControlJsonNumber( result, percentile95Interval );
		result.append( ",\"observedFps\":" );
		if ( averageInterval > 0.0 )
		{
			AppendSimulatorControlJsonNumber( result, 1000.0 / averageInterval );
		}
		else
		{
			result.append( "null" );
		}
		result.push_back( '}' );
	}

	char memory[160];
	snprintf(
		memory, sizeof( memory ),
		",\"memory\":{\"luaBytes\":%llu,\"textureBytes\":%ld},"
		"\"rendering\":{\"statisticsEnabled\":%s,\"previousFrame\":",
		luaBytes, (long)( textureBytes < 0 ? 0 : textureBytes ),
		statisticsEnabled ? "true" : "false" );
	result.append( memory );
	if ( ! statisticsEnabled )
	{
		result.append( "null}}" );
		return result;
	}

	const Renderer::Statistics& statistics = renderer.GetFrameStatistics();
	result.append( "{\"resourceCreateMs\":" );
	AppendSimulatorControlJsonNumber(
		result, (double)statistics.fResourceCreateTime );
	result.append( ",\"resourceUpdateMs\":" );
	AppendSimulatorControlJsonNumber(
		result, (double)statistics.fResourceUpdateTime );
	result.append( ",\"resourceDestroyMs\":" );
	AppendSimulatorControlJsonNumber(
		result, (double)statistics.fResourceDestroyTime );
	result.append( ",\"preparationMs\":" );
	AppendSimulatorControlJsonNumber(
		result, (double)statistics.fPreparationTime );
	result.append( ",\"cpuRenderMs\":" );
	AppendSimulatorControlJsonNumber(
		result, (double)statistics.fRenderTimeCPU );
	result.append( ",\"gpuRenderMs\":" );
	AppendSimulatorControlJsonNumber(
		result, (double)statistics.fRenderTimeGPU );
	char rendererCounts[384];
	snprintf(
		rendererCounts, sizeof( rendererCounts ),
		",\"drawCalls\":%lu,\"triangles\":%lu,\"lines\":%lu,"
		"\"geometryBinds\":%lu,\"programBinds\":%lu,"
		"\"textureBinds\":%lu,\"uniformBinds\":%lu}}}",
		(unsigned long)statistics.fDrawCallCount,
		(unsigned long)statistics.fTriangleCount,
		(unsigned long)statistics.fLineCount,
		(unsigned long)statistics.fGeometryBindCount,
		(unsigned long)statistics.fProgramBindCount,
		(unsigned long)statistics.fTextureBindCount,
		(unsigned long)statistics.fUniformBindCount );
	result.append( rendererCounts );
	return result;
}

static std::string
BuildSimulatorControlDiagnostics(
	const SimulatorControlRuntimeState& state )
{
	std::string result( "{\"sessionId\":" );
	AppendSimulatorControlJsonString( result, state.sessionId );
	char identity[128];
	snprintf(
		identity, sizeof( identity ),
		",\"generation\":%lu,\"runtimeErrorHalted\":%s,"
		"\"latestRuntimeError\":",
		state.generation, state.runtimeErrorHalted ? "true" : "false" );
	result.append( identity );
	if ( ! state.hasDiagnostic )
	{
		result.append( "null}" );
		return result;
	}

	char metadata[128];
	snprintf(
		metadata, sizeof( metadata ),
		"{\"sequence\":%lu,\"frame\":%lu,\"type\":",
		state.diagnosticSequence, state.diagnosticFrame );
	result.append( metadata );
	AppendSimulatorControlJsonString( result, state.diagnosticType );
	result.append( ",\"message\":" );
	AppendSimulatorControlJsonString( result, state.diagnosticMessage );
	result.append( ",\"stackTrace\":" );
	AppendSimulatorControlJsonString( result, state.diagnosticStackTrace );
	result.append( ",\"context\":" );
	result.append(
		state.diagnosticContext.empty() ? "null" : state.diagnosticContext );
	result.append(
		state.diagnosticTruncated ?
			",\"truncated\":true}}" : ",\"truncated\":false}}" );
	return result;
}

static bool
IsSimulatorControlCommandAllowedWhileErrorHalted(
	const std::string& command )
{
	return "runtime-status" == command ||
		"runtime-metrics" == command ||
		"runtime-diagnostics" == command ||
		"runtime-logs" == command ||
		"capture-screenshot" == command ||
		"debug-snapshot" == command ||
		"screen-recording-status" == command ||
		"stop-screen-recording" == command ||
		"display-object-tree" == command ||
		"find-display-object" == command ||
		"hit-test-display-objects" == command ||
		"inspect-lua-value" == command ||
		"add-lua-breakpoint" == command ||
		"remove-lua-breakpoint" == command ||
		"list-lua-breakpoints" == command ||
		"debugger-status" == command ||
		"relaunch-project" == command ||
		"quit-simulator" == command;
}

static bool
IsSimulatorControlCommandAllowedWhileDebuggerPaused(
	const std::string& command )
{
	return "runtime-status" == command ||
		"runtime-metrics" == command ||
		"runtime-diagnostics" == command ||
		"runtime-logs" == command ||
		"inspect-lua-value" == command ||
		"add-lua-breakpoint" == command ||
		"remove-lua-breakpoint" == command ||
		"list-lua-breakpoints" == command ||
		"debugger-status" == command ||
		"debugger-stack" == command ||
		"inspect-debugger-frame" == command ||
		"evaluate-debugger-frame" == command ||
		"continue-debugger" == command ||
		"step-into" == command ||
		"step-over" == command ||
		"step-out" == command;
}

static std::string
BuildSimulatorControlRuntimeErrorHaltedResponse(
	const SimulatorControlRuntimeState& state )
{
	std::string result(
		"{\"ok\":false,\"error\":{\"code\":\"runtime-error-halted\","
		"\"message\":\"runtime execution is halted after an unhandled error; "
		"relaunch the project before issuing this command\"},\"result\":" );
	result.append( BuildSimulatorControlDiagnostics( state ) );
	result.push_back( '}' );
	return result;
}

static std::string
BuildSimulatorControlLogs(
	const SimulatorControlRuntimeState& runtimeState,
	unsigned long sinceSequence, const std::string& filter )
{
	std::deque< SimulatorControlLogEntry > entries;
	unsigned long latestSequence = 0;
	bool droppedEntries = false;
	{
		SimulatorControlLogGuard guard( GetSimulatorControlLogMutex() );
		SimulatorControlLogState& logState = GetSimulatorControlLogState();
		entries = logState.entries;
		latestSequence = logState.sequence;
		droppedEntries = logState.droppedEntries;
	}

	unsigned long oldestSequence =
		entries.empty() ? 0 : entries.front().sequence;
	bool truncated = droppedEntries && oldestSequence > 0 &&
		sinceSequence < oldestSequence - 1;
	std::string result( "{\"sessionId\":" );
	AppendSimulatorControlJsonString( result, runtimeState.sessionId );
	char information[192];
	snprintf(
		information, sizeof( information ),
		",\"generation\":%lu,\"oldestSequence\":%lu,"
		"\"latestSequence\":%lu,\"truncated\":%s,\"entries\":[",
		runtimeState.generation, oldestSequence, latestSequence,
		truncated ? "true" : "false" );
	result.append( information );
	std::string serializedFilter;
	AppendSimulatorControlJsonString( serializedFilter, filter );

	bool first = true;
	bool hasMore = false;
	for ( std::deque< SimulatorControlLogEntry >::const_iterator iterator =
			entries.begin(); entries.end() != iterator; iterator++ )
	{
		if ( iterator->sequence <= sinceSequence )
		{
			continue;
		}
		if ( ! filter.empty() &&
			std::string::npos == iterator->message.find( filter ) )
		{
			continue;
		}

		std::string serialized;
		char sequence[128];
		snprintf(
			sequence, sizeof( sequence ),
			"{\"sequence\":%lu,\"timestampMs\":%llu,\"message\":",
			iterator->sequence, iterator->timestampMilliseconds );
		serialized.append( sequence );
		AppendSimulatorControlJsonString( serialized, iterator->message );
		serialized.push_back( '}' );
		if ( result.length() + serialized.length() +
			serializedFilter.length() + 40 >
			kSimulatorControlMaximumResponseSize )
		{
			hasMore = true;
			break;
		}
		if ( ! first )
		{
			result.push_back( ',' );
		}
		result.append( serialized );
		first = false;
	}
	result.append( "],\"filter\":" );
	result.append( serializedFilter );
	result.append( ",\"hasMore\":" );
	result.append( hasMore ? "true}" : "false}" );
	return result;
}

static std::string
BuildSimulatorControlLogSearch( const std::string& text )
{
	SimulatorControlLogEntry match;
	bool found = false;
	unsigned long latestSequence = 0;
	{
		SimulatorControlLogGuard guard( GetSimulatorControlLogMutex() );
		SimulatorControlLogState& state = GetSimulatorControlLogState();
		latestSequence = state.sequence;
		for ( std::deque< SimulatorControlLogEntry >::const_reverse_iterator iterator =
			state.entries.rbegin(); state.entries.rend() != iterator; iterator++ )
		{
			if ( std::string::npos != iterator->message.find( text ) )
			{
				match = *iterator;
				found = true;
				break;
			}
		}
	}

	std::string result( "{\"satisfied\":" );
	result.append( found ? "true" : "false" );
	char latest[64];
	snprintf(
		latest, sizeof( latest ),
		",\"latestSequence\":%lu,\"entry\":", latestSequence );
	result.append( latest );
	if ( ! found )
	{
		result.append( "null}" );
		return result;
	}
	char entry[128];
	snprintf(
		entry, sizeof( entry ),
		"{\"sequence\":%lu,\"timestampMs\":%llu,\"message\":",
		match.sequence, match.timestampMilliseconds );
	result.append( entry );
	AppendSimulatorControlJsonString( result, match.message );
	result.append( "}}" );
	return result;
}

struct SimulatorControlDisplayNode
{
	SimulatorControlDisplayNode(
		lua_State *L, const DisplayObject& value, U32 parent, int child,
		int nodeDepth, bool nodeVisible )
	: object( & value ),
		handle( 0 ),
		parentHandle( parent ),
		childIndex( child ),
		depth( nodeDepth ),
		visible( nodeVisible ),
		hasAutomationId( false )
	{
		if ( L && value.IsReachable() )
		{
			LuaProxy *proxy = value.GetProxy();
			if ( proxy && proxy->PushTable( L ) )
			{
				int tableIndex = lua_gettop( L );
				handle = (U32)RegisterSimulatorControlHandle( L, tableIndex );
				lua_pushliteral( L, "automationId" );
				lua_rawget( L, tableIndex );
				if ( lua_type( L, -1 ) == LUA_TSTRING )
				{
					size_t length = 0;
					const char *value = lua_tolstring( L, -1, &length );
					if ( value && length > 0 && length <= 128 &&
						strlen( value ) == length )
					{
						automationId.assign( value, length );
						hasAutomationId = true;
					}
				}
				lua_settop( L, tableIndex - 1 );
			}
		}
	}

	const DisplayObject *object;
	U32 handle;
	U32 parentHandle;
	int childIndex;
	int depth;
	bool visible;
	std::string automationId;
	bool hasAutomationId;
};

static void
CollectSimulatorControlDisplayNodes(
	lua_State *L, const GroupObject& group, U32 parentHandle, int depth,
	bool parentVisible,
	std::vector< SimulatorControlDisplayNode >& nodes )
{
	for ( S32 index = 0; index < group.NumChildren(); index++ )
	{
		const DisplayObject& child = group.ChildAt( index );
		bool visible = parentVisible && child.IsVisible() &&
			child.AlphaCumulative() > 0 && ! child.IsOffScreen();
		nodes.push_back( SimulatorControlDisplayNode(
			L, child, parentHandle, (int)index + 1, depth, visible ) );
		const GroupObject *childGroup = child.AsGroupObject();
		if ( childGroup )
		{
			CollectSimulatorControlDisplayNodes(
				L, *childGroup, nodes.back().handle, depth + 1,
				visible, nodes );
		}
	}
}

static void
GetSimulatorControlScreenBounds(
	const Display& display, const DisplayObject& object,
	float& x, float& y, float& width, float& height )
{
	const Rect& bounds = object.StageBounds();
	if ( bounds.IsEmpty() )
	{
		x = y = width = height = 0.0f;
		return;
	}
	x = Rtt_RealToFloat( bounds.xMin );
	y = Rtt_RealToFloat( bounds.yMin );
	width = Rtt_RealToFloat( bounds.xMax - bounds.xMin );
	height = Rtt_RealToFloat( bounds.yMax - bounds.yMin );
	display.ContentToScreenUnrounded( x, y, width, height );
}

static void
AppendSimulatorControlDisplayNode(
	std::string& result, const Display& display,
	const SimulatorControlDisplayNode& node )
{
	const DisplayObject& object = * node.object;
	char prefix[192];
	snprintf(
		prefix, sizeof( prefix ),
		"{\"handle\":%lu,\"parentHandle\":%lu,\"childIndex\":%d,"
		"\"depth\":%d,\"type\":",
		(unsigned long)node.handle,
		(unsigned long)node.parentHandle, node.childIndex, node.depth );
	result.append( prefix );
	AppendSimulatorControlJsonString(
		result, object.GetObjectDesc() ? object.GetObjectDesc() : "DisplayObject" );

	result.append( ",\"automationId\":" );
	if ( node.hasAutomationId )
	{
		AppendSimulatorControlJsonString( result, node.automationId );
	}
	else
	{
		result.append( "null" );
	}

	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	GetSimulatorControlScreenBounds(
		display, object, x, y, width, height );
	char details[384];
	snprintf(
		details, sizeof( details ),
		",\"bounds\":{\"x\":%.9g,\"y\":%.9g,\"width\":%.9g,"
		"\"height\":%.9g,\"centerX\":%.9g,\"centerY\":%.9g},"
		"\"visible\":%s,\"alpha\":%.9g,\"hitTestable\":%s,"
		"\"hittable\":%s,"
		"\"touchListener\":%s,\"offscreen\":%s,\"children\":%d}",
		x, y, width, height, x + width * 0.5f, y + height * 0.5f,
		node.visible ? "true" : "false",
		(float)object.AlphaCumulative() / 255.0f,
		object.IsHitTestable() ? "true" : "false",
		node.visible && object.ShouldHitTest() && ! object.SkipsHitTest() &&
			object.CanHitTest() && width > 0.0f && height > 0.0f ? "true" : "false",
		object.HasListener( DisplayObject::kTouchListener ) ? "true" : "false",
		object.IsOffScreen() ? "true" : "false",
		object.AsGroupObject() ? object.AsGroupObject()->NumChildren() : 0 );
	result.append( details );
}

static std::vector< SimulatorControlDisplayNode >
GetSimulatorControlDisplayNodes( Runtime& runtime )
{
	std::vector< SimulatorControlDisplayNode > nodes;
	StageObject *stage = runtime.GetDisplay().GetStage();
	if ( stage )
	{
		CollectSimulatorControlDisplayNodes(
			runtime.VMContext().L(), *stage, 0, 0, true, nodes );
	}
	return nodes;
}

static const SimulatorControlDisplayNode*
FindSimulatorControlDisplayNode(
	const std::vector< SimulatorControlDisplayNode >& nodes,
	const std::string& selector, std::string& error )
{
	std::string value( selector );
	if ( ! value.empty() && '#' == value[0] )
	{
		value.erase( 0, 1 );
	}
	if ( value.empty() )
	{
		error = "display-object selector cannot be empty";
		return NULL;
	}

	bool findHandle = '@' == value[0];
	unsigned long requestedHandle = 0;
	if ( findHandle )
	{
		const char *digits = value.c_str() + 1;
		char *end = NULL;
		errno = 0;
		requestedHandle = strtoul( digits, &end, 10 );
		if ( ERANGE == errno || end == digits || ! end || end[0] ||
			0 == requestedHandle )
		{
			error = "display-object handle must look like @42";
			return NULL;
		}
	}

	const SimulatorControlDisplayNode *match = NULL;
	int matchCount = 0;
	for ( size_t index = 0; index < nodes.size(); index++ )
	{
		bool matches = findHandle ?
			requestedHandle == (unsigned long)nodes[index].handle :
			( nodes[index].hasAutomationId &&
				value == nodes[index].automationId );
		if ( matches )
		{
			match = & nodes[index];
			matchCount++;
		}
	}
	if ( 0 == matchCount )
	{
		error = findHandle ?
			"display-object handle is stale or does not exist" :
			std::string( "no display object has automationId '" ) + value + "'";
		return NULL;
	}
	if ( matchCount > 1 )
	{
		error = std::string( "automationId '" ) + value +
			"' is ambiguous; identifiers must be unique in the active display tree";
		return NULL;
	}
	return match;
}

static std::string
BuildSimulatorControlDisplayTree( Runtime& runtime, int cursor )
{
	std::vector< SimulatorControlDisplayNode > nodes =
		GetSimulatorControlDisplayNodes( runtime );
	std::string result( "{\"nodes\":[" );
	int emitted = 0;
	for ( size_t index = (size_t)cursor;
		index < nodes.size() && emitted < kSimulatorControlMaximumEntries;
		index++ )
	{
		std::string serialized;
		AppendSimulatorControlDisplayNode(
			serialized, runtime.GetDisplay(), nodes[index] );
		if ( result.length() + serialized.length() + 128 >
			kSimulatorControlMaximumResponseSize )
		{
			break;
		}
		if ( emitted > 0 )
		{
			result.push_back( ',' );
		}
		result.append( serialized );
		emitted++;
	}
	int nextCursor = cursor + emitted;
	char suffix[160];
	snprintf(
		suffix, sizeof( suffix ),
		"],\"cursor\":%d,\"totalCount\":%lu,\"truncated\":%s",
		cursor, (unsigned long)nodes.size(),
		nextCursor < (int)nodes.size() ? "true" : "false" );
	result.append( suffix );
	if ( nextCursor < (int)nodes.size() )
	{
		char cursorSuffix[64];
		snprintf(
			cursorSuffix, sizeof( cursorSuffix ),
			",\"nextCursor\":%d}", nextCursor );
		result.append( cursorSuffix );
	}
	else
	{
		result.append( ",\"nextCursor\":null}" );
	}
	return result;
}

static std::string
BuildSimulatorControlHitTest( Runtime& runtime, double screenX, double screenY )
{
	std::vector< SimulatorControlDisplayNode > nodes =
		GetSimulatorControlDisplayNodes( runtime );
	std::string hits( "[" );
	int emitted = 0;
	for ( std::vector< SimulatorControlDisplayNode >::const_reverse_iterator iterator =
		nodes.rbegin(); nodes.rend() != iterator; iterator++ )
	{
		const DisplayObject& object = * iterator->object;
		if ( ! iterator->visible || ! object.ShouldHitTest() || object.SkipsHitTest() ||
			! object.CanHitTest() || object.IsOffScreen() ||
			0 == object.AlphaCumulative() )
		{
			continue;
		}
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		GetSimulatorControlScreenBounds(
			runtime.GetDisplay(), object, x, y, width, height );
		if ( width <= 0.0f || height <= 0.0f ||
			screenX < x || screenX > x + width ||
			screenY < y || screenY > y + height )
		{
			continue;
		}

		std::string serialized;
		AppendSimulatorControlDisplayNode(
			serialized, runtime.GetDisplay(), *iterator );
		if ( emitted > 0 )
		{
			hits.push_back( ',' );
		}
		hits.append( serialized );
		emitted++;
		if ( emitted >= 20 || hits.length() + 1024 >
			kSimulatorControlMaximumResponseSize )
		{
			break;
		}
	}
	hits.push_back( ']' );

	std::string result( "{\"x\":" );
	char coordinates[128];
	snprintf(
		coordinates, sizeof( coordinates ),
		"%.17g,\"y\":%.17g,\"approximate\":true,\"hits\":",
		screenX, screenY );
	result.append( coordinates );
	result.append( hits );
	result.push_back( '}' );
	return result;
}

static bool
CaptureSimulatorControlScreenshot(
	Runtime& runtime, const std::string& path,
	std::string& result, std::string& error )
{
	BitmapPaint *paint =
		runtime.GetDisplay().CaptureSave( NULL, false, NULL );
	if ( ! paint )
	{
		error = "the Simulator could not capture the current screen";
		return false;
	}

	PlatformBitmap *bitmap = paint->GetBitmap();
	if ( ! bitmap )
	{
		Rtt_DELETE( paint );
		error = "the Simulator screenshot did not contain a bitmap";
		return false;
	}
	int width = bitmap->Width();
	int height = bitmap->Height();
	Data< const char > pngBytes( runtime.GetAllocator() );
	runtime.Platform().SaveBitmap( bitmap, pngBytes );
	Rtt_DELETE( paint );
	if ( ! pngBytes.GetData() || pngBytes.GetLength() <= 0 )
	{
		error = "the Simulator could not encode the screenshot as PNG";
		return false;
	}

	std::string contents(
		pngBytes.GetData(), (size_t)pngBytes.GetLength() );
	if ( ! WriteSimulatorControlFileAtomically( path, contents ) )
	{
		error = "the Simulator could not write the screenshot";
		return false;
	}

	result.assign( "{\"path\":" );
	AppendSimulatorControlJsonString( result, path );
	char information[128];
	snprintf(
		information, sizeof( information ),
		",\"width\":%d,\"height\":%d,\"bytes\":%ld}",
		width, height, (long)pngBytes.GetLength() );
	result.append( information );
	return true;
}

static bool
CaptureSimulatorControlSnapshot(
	Runtime& runtime, const SimulatorControlRuntimeState& state,
	const std::string& screenshotPath, std::string& result,
	std::string& error )
{
	std::string screenshot;
	if ( ! CaptureSimulatorControlScreenshot(
		runtime, screenshotPath, screenshot, error ) )
	{
		return false;
	}

	std::string manifest( "{\"status\":" );
	manifest.append( BuildSimulatorControlStatus( runtime, state ) );
	manifest.append( ",\"metrics\":" );
	manifest.append( BuildSimulatorControlRuntimeMetrics( runtime, state ) );
	manifest.append( ",\"diagnostics\":" );
	manifest.append( BuildSimulatorControlDiagnostics( state ) );
	manifest.append( ",\"logs\":" );
	manifest.append( BuildSimulatorControlLogs( state, 0, std::string() ) );
	manifest.append( ",\"displayTree\":" );
	manifest.append( BuildSimulatorControlDisplayTree( runtime, 0 ) );
	manifest.push_back( '}' );

	std::string manifestPath = screenshotPath + ".json";
	if ( ! WriteSimulatorControlFileAtomically( manifestPath, manifest ) )
	{
		error = "the Simulator could not write the snapshot manifest";
		return false;
	}

	result.assign( "{\"screenshot\":" );
	result.append( screenshot );
	result.append( ",\"manifestPath\":" );
	AppendSimulatorControlJsonString( result, manifestPath );
	char frame[64];
	snprintf(
		frame, sizeof( frame ),
		",\"frame\":%lu}", (unsigned long)runtime.GetFrame() );
	result.append( frame );
	return true;
}

static bool
WriteSimulatorControlSession(
	SimulatorControlRuntimeState& state )
{
	if ( state.sessionWritten )
	{
		return true;
	}

	char session[512];
	snprintf(
		session, sizeof( session ),
		"{\"protocol\":2,\"transport\":\"filesystem\",\"sessionId\":\"%s\","
		"\"generation\":%lu,\"pid\":%ld,\"requestFile\":\"request\"}",
		state.sessionId.c_str(), state.generation,
		GetSimulatorControlProcessId() );
	state.sessionWritten = WriteSimulatorControlFileAtomically(
		SimulatorControlPath( state.directory, "session.json" ), session );
	return state.sessionWritten;
}

static bool
WriteSimulatorControlDiagnostics(
	SimulatorControlRuntimeState& state )
{
	state.diagnosticsWritten = WriteSimulatorControlFileAtomically(
		SimulatorControlPath( state.directory, "diagnostics.json" ),
		SimulatorControlSuccessResponse(
			BuildSimulatorControlDiagnostics( state ) ) );
	return state.diagnosticsWritten;
}

static void
PushSimulatorControlRegistry( lua_State *L )
{
	lua_pushlightuserdata( L, & kSimulatorControlRegistryKey );
	lua_rawget( L, LUA_REGISTRYINDEX );
	if ( lua_istable( L, -1 ) )
	{
		return;
	}
	lua_pop( L, 1 );

	lua_newtable( L );
	int registryIndex = lua_gettop( L );

	lua_newtable( L );
	lua_newtable( L );
	lua_pushliteral( L, "v" );
	lua_setfield( L, -2, "__mode" );
	lua_setmetatable( L, -2 );
	lua_setfield( L, registryIndex, "handles" );

	lua_newtable( L );
	lua_newtable( L );
	lua_pushliteral( L, "k" );
	lua_setfield( L, -2, "__mode" );
	lua_setmetatable( L, -2 );
	lua_setfield( L, registryIndex, "reverse" );

	lua_pushinteger( L, 1 );
	lua_setfield( L, registryIndex, "nextHandle" );

	lua_pushlightuserdata( L, & kSimulatorControlRegistryKey );
	lua_pushvalue( L, registryIndex );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

static int
RegisterSimulatorControlHandle( lua_State *L, int valueIndex )
{
	valueIndex = AbsoluteLuaIndex( L, valueIndex );
	PushSimulatorControlRegistry( L );
	int registryIndex = lua_gettop( L );

	lua_getfield( L, registryIndex, "reverse" );
	int reverseIndex = lua_gettop( L );
	lua_pushvalue( L, valueIndex );
	lua_rawget( L, reverseIndex );
	if ( lua_type( L, -1 ) == LUA_TNUMBER )
	{
		int result = (int)lua_tointeger( L, -1 );
		lua_pop( L, 3 );
		return result;
	}
	lua_pop( L, 1 );

	lua_getfield( L, registryIndex, "nextHandle" );
	int result = (int)lua_tointeger( L, -1 );
	lua_pop( L, 1 );
	lua_pushinteger( L, result + 1 );
	lua_setfield( L, registryIndex, "nextHandle" );

	lua_pushvalue( L, valueIndex );
	lua_pushinteger( L, result );
	lua_rawset( L, reverseIndex );

	lua_getfield( L, registryIndex, "handles" );
	lua_pushvalue( L, valueIndex );
	lua_rawseti( L, -2, result );
	lua_pop( L, 3 );
	return result;
}

static bool
PushSimulatorControlHandle( lua_State *L, int handle )
{
	PushSimulatorControlRegistry( L );
	lua_getfield( L, -1, "handles" );
	lua_rawgeti( L, -1, handle );
	lua_remove( L, -2 );
	lua_remove( L, -2 );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

static int
CountSimulatorControlTableEntries( lua_State *L, int tableIndex )
{
	tableIndex = AbsoluteLuaIndex( L, tableIndex );
	int count = 0;
	lua_pushnil( L );
	while ( lua_next( L, tableIndex ) )
	{
		count++;
		lua_pop( L, 1 );
	}
	return count;
}

static void
AppendSimulatorControlNumber( std::string& result, lua_Number value )
{
	char buffer[64];
	snprintf( buffer, sizeof( buffer ), "%.17g", (double)value );
	result.append( buffer );
}

static void
AppendSimulatorControlValueSummary( lua_State *L, int valueIndex, std::string& result )
{
	valueIndex = AbsoluteLuaIndex( L, valueIndex );
	switch ( lua_type( L, valueIndex ) )
	{
		case LUA_TNIL:
			result.append( "{\"type\":\"nil\",\"value\":null}" );
			break;
		case LUA_TBOOLEAN:
			result.append( lua_toboolean( L, valueIndex ) ?
				"{\"type\":\"boolean\",\"value\":true}" :
				"{\"type\":\"boolean\",\"value\":false}" );
			break;
		case LUA_TNUMBER:
		{
			lua_Number value = lua_tonumber( L, valueIndex );
			result.append( "{\"type\":\"number\",\"value\":" );
			if ( IsFiniteNumber( value ) )
			{
				AppendSimulatorControlNumber( result, value );
			}
			else
			{
				result.append( "null,\"description\":" );
				const char *description = value != value ? "nan" : ( value > 0 ? "infinity" : "-infinity" );
				AppendSimulatorControlJsonString( result, description, strlen( description ) );
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TSTRING:
		{
			size_t length = 0;
			const char *value = lua_tolstring( L, valueIndex, & length );
			size_t serializedLength =
				length > kSimulatorControlMaximumStringSize ? kSimulatorControlMaximumStringSize : length;
			result.append( "{\"type\":\"string\",\"value\":" );
			AppendSimulatorControlJsonString( result, value, serializedLength );
			result.append( ",\"length\":" );
			char buffer[32];
			snprintf( buffer, sizeof( buffer ), "%lu", (unsigned long)length );
			result.append( buffer );
			if ( serializedLength < length )
			{
				result.append( ",\"truncated\":true" );
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TTABLE:
		{
			int handle = RegisterSimulatorControlHandle( L, valueIndex );
			int count = CountSimulatorControlTableEntries( L, valueIndex );
			char buffer[96];
			snprintf(
				buffer, sizeof( buffer ),
				"{\"type\":\"table\",\"handle\":%d,\"count\":%d}",
				handle, count );
			result.append( buffer );
			break;
		}
		case LUA_TFUNCTION:
		{
			result.append( "{\"type\":\"function\"" );
			lua_pushvalue( L, valueIndex );
			lua_Debug information;
			memset( & information, 0, sizeof( information ) );
			if ( lua_getinfo( L, ">S", & information ) )
			{
				if ( information.short_src[0] )
				{
					result.append( ",\"source\":" );
					AppendSimulatorControlJsonString(
						result, information.short_src, strlen( information.short_src ) );
				}
				if ( information.linedefined > 0 )
				{
					char buffer[48];
					snprintf( buffer, sizeof( buffer ), ",\"line\":%d", information.linedefined );
					result.append( buffer );
				}
				if ( information.what )
				{
					result.append( ",\"kind\":" );
					AppendSimulatorControlJsonString(
						result, information.what, strlen( information.what ) );
				}
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TUSERDATA:
		case LUA_TLIGHTUSERDATA:
			result.append( lua_type( L, valueIndex ) == LUA_TUSERDATA ?
				"{\"type\":\"userdata\"" : "{\"type\":\"lightuserdata\"" );
			if ( lua_getmetatable( L, valueIndex ) )
			{
				lua_pushliteral( L, "__name" );
				lua_rawget( L, -2 );
				if ( lua_type( L, -1 ) == LUA_TSTRING )
				{
					size_t length = 0;
					const char *name = lua_tolstring( L, -1, & length );
					result.append( ",\"name\":" );
					AppendSimulatorControlJsonString( result, name, length );
				}
				lua_pop( L, 2 );
			}
			result.push_back( '}' );
			break;
		case LUA_TTHREAD:
			result.append( "{\"type\":\"thread\"}" );
			break;
		default:
			result.append( "{\"type\":\"unknown\"}" );
			break;
	}
}

static void
AppendSimulatorControlDiagnosticShallowValue(
	lua_State *L, int valueIndex, std::string& result, bool& truncated )
{
	valueIndex = AbsoluteLuaIndex( L, valueIndex );
	switch ( lua_type( L, valueIndex ) )
	{
		case LUA_TNIL:
			result.append( "{\"type\":\"nil\",\"value\":null}" );
			break;
		case LUA_TBOOLEAN:
			result.append( lua_toboolean( L, valueIndex ) ?
				"{\"type\":\"boolean\",\"value\":true}" :
				"{\"type\":\"boolean\",\"value\":false}" );
			break;
		case LUA_TNUMBER:
		{
			lua_Number value = lua_tonumber( L, valueIndex );
			result.append( "{\"type\":\"number\",\"value\":" );
			if ( IsFiniteNumber( value ) )
			{
				AppendSimulatorControlNumber( result, value );
			}
			else
			{
				result.append( "null,\"description\":" );
				const char *description = value != value ?
					"nan" : ( value > 0 ? "infinity" : "-infinity" );
				AppendSimulatorControlJsonString(
					result, description, strlen( description ) );
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TSTRING:
		{
			size_t length = 0;
			const char *value = lua_tolstring( L, valueIndex, & length );
			size_t serializedLength = length >
				kSimulatorControlMaximumDiagnosticValueStringSize ?
				kSimulatorControlMaximumDiagnosticValueStringSize : length;
			result.append( "{\"type\":\"string\",\"value\":" );
			AppendSimulatorControlJsonString(
				result, value, serializedLength );
			char information[64];
			snprintf(
				information, sizeof( information ),
				",\"length\":%lu", (unsigned long)length );
			result.append( information );
			if ( serializedLength < length )
			{
				result.append( ",\"truncated\":true" );
				truncated = true;
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TTABLE:
			result.append( "{\"type\":\"table\"}" );
			break;
		case LUA_TFUNCTION:
			result.append( "{\"type\":\"function\"}" );
			break;
		case LUA_TUSERDATA:
			result.append( "{\"type\":\"userdata\"}" );
			break;
		case LUA_TLIGHTUSERDATA:
			result.append( "{\"type\":\"lightuserdata\"}" );
			break;
		case LUA_TTHREAD:
			result.append( "{\"type\":\"thread\"}" );
			break;
		default:
			result.append( "{\"type\":\"unknown\"}" );
			break;
	}
}

static void
AppendSimulatorControlDiagnosticValue(
	lua_State *L, int valueIndex, std::string& result, bool& truncated )
{
	valueIndex = AbsoluteLuaIndex( L, valueIndex );
	if ( ! lua_istable( L, valueIndex ) )
	{
		AppendSimulatorControlDiagnosticShallowValue(
			L, valueIndex, result, truncated );
		return;
	}

	result.append( "{\"type\":\"table\",\"entries\":[" );
	int emitted = 0;
	bool tableTruncated = false;
	lua_pushnil( L );
	while ( lua_next( L, valueIndex ) )
	{
		if ( emitted >= kSimulatorControlMaximumDiagnosticTableEntries )
		{
			tableTruncated = true;
			lua_pop( L, 2 );
			break;
		}
		if ( emitted > 0 )
		{
			result.push_back( ',' );
		}
		result.append( "{\"key\":" );
		AppendSimulatorControlDiagnosticShallowValue(
			L, -2, result, tableTruncated );
		result.append( ",\"value\":" );
		AppendSimulatorControlDiagnosticShallowValue(
			L, -1, result, tableTruncated );
		result.push_back( '}' );
		emitted++;
		lua_pop( L, 1 );
	}
	truncated = truncated || tableTruncated;
	result.append( tableTruncated ?
		"],\"truncated\":true}" : "],\"truncated\":false}" );
}

static std::string
BuildSimulatorControlDiagnosticNamedValues(
	lua_State *L, const lua_Debug& information, int functionIndex,
	bool upvalues, int maximumValues, bool& truncated )
{
	std::string result( "[" );
	int emitted = 0;
	for ( int index = 1; ; index++ )
	{
		const char *name = upvalues ?
			lua_getupvalue( L, functionIndex, index ) :
			lua_getlocal( L, & information, index );
		if ( ! name )
		{
			break;
		}
		if ( emitted >= maximumValues )
		{
			truncated = true;
			lua_pop( L, 1 );
			break;
		}

		std::string entry( emitted > 0 ? ",{\"name\":" : "{\"name\":" );
		size_t nameLength = strlen( name );
		if ( nameLength > kSimulatorControlMaximumDiagnosticValueStringSize )
		{
			nameLength = kSimulatorControlMaximumDiagnosticValueStringSize;
			truncated = true;
		}
		AppendSimulatorControlJsonString( entry, name, nameLength );
		entry.append( ",\"value\":" );
		AppendSimulatorControlDiagnosticValue(
			L, -1, entry, truncated );
		entry.push_back( '}' );
		lua_pop( L, 1 );

		if ( result.length() + entry.length() >
			kSimulatorControlMaximumDiagnosticContextSize / 3 )
		{
			truncated = true;
			break;
		}
		result.append( entry );
		emitted++;
	}
	result.push_back( ']' );
	return result;
}

static bool
BuildSimulatorControlDiagnosticFrame(
	lua_State *L, int stackLevel, int frameLevel,
	std::string& result, bool& truncated )
{
	lua_Debug information;
	memset( & information, 0, sizeof( information ) );
	if ( ! lua_getstack( L, stackLevel, & information ) )
	{
		return false;
	}

	int originalTop = lua_gettop( L );
	if ( ! lua_getinfo( L, "nSluf", & information ) )
	{
		lua_settop( L, originalTop );
		return false;
	}
	int functionIndex = lua_gettop( L );

	char prefix[64];
	snprintf( prefix, sizeof( prefix ), "{\"level\":%d", frameLevel );
	result.assign( prefix );
	if ( information.source )
	{
		size_t length = strlen( information.source );
		if ( length > kSimulatorControlMaximumStringSize )
		{
			length = kSimulatorControlMaximumStringSize;
			truncated = true;
		}
		result.append( ",\"source\":" );
		AppendSimulatorControlJsonString(
			result, information.source, length );
	}
	if ( information.short_src[0] )
	{
		result.append( ",\"shortSource\":" );
		AppendSimulatorControlJsonString(
			result, information.short_src,
			strlen( information.short_src ) );
	}
	if ( information.currentline >= 0 )
	{
		char line[48];
		snprintf(
			line, sizeof( line ),
			",\"line\":%d", information.currentline );
		result.append( line );
	}
	if ( information.name && information.name[0] )
	{
		result.append( ",\"function\":" );
		AppendSimulatorControlJsonString(
			result, information.name, strlen( information.name ) );
	}
	if ( information.what )
	{
		result.append( ",\"kind\":" );
		AppendSimulatorControlJsonString(
			result, information.what, strlen( information.what ) );
	}

	bool localsTruncated = false;
	std::string locals = BuildSimulatorControlDiagnosticNamedValues(
		L, information, functionIndex, false,
		kSimulatorControlMaximumDiagnosticLocals, localsTruncated );
	bool upvaluesTruncated = false;
	std::string capturedUpvalues = BuildSimulatorControlDiagnosticNamedValues(
		L, information, functionIndex, true,
		kSimulatorControlMaximumDiagnosticUpvalues, upvaluesTruncated );
	result.append( ",\"locals\":" );
	result.append( locals );
	result.append( ",\"upvalues\":" );
	result.append( capturedUpvalues );
	truncated = truncated || localsTruncated || upvaluesTruncated;
	result.append( truncated ?
		",\"truncated\":true}" : ",\"truncated\":false}" );
	lua_settop( L, originalTop );
	return true;
}

static std::string
BuildSimulatorControlDiagnosticContext(
	lua_State *L, bool& truncated )
{
	std::string frames;
	int emitted = 0;
	for ( int stackLevel = 1;
		stackLevel <= kSimulatorControlMaximumDiagnosticFrames;
		stackLevel++ )
	{
		std::string frame;
		bool frameTruncated = false;
		if ( ! BuildSimulatorControlDiagnosticFrame(
			L, stackLevel, emitted, frame, frameTruncated ) )
		{
			break;
		}
		if ( frames.length() + frame.length() + 64 >
			kSimulatorControlMaximumDiagnosticContextSize )
		{
			truncated = true;
			break;
		}
		if ( emitted > 0 )
		{
			frames.push_back( ',' );
		}
		frames.append( frame );
		emitted++;
		truncated = truncated || frameTruncated;
	}

	lua_Debug extraFrame;
	if ( emitted >= kSimulatorControlMaximumDiagnosticFrames &&
		lua_getstack(
			L, kSimulatorControlMaximumDiagnosticFrames + 1,
			& extraFrame ) )
	{
		truncated = true;
	}

	std::string result( "{\"frames\":[" );
	result.append( frames );
	result.append( truncated ?
		"],\"truncated\":true}" : "],\"truncated\":false}" );
	return result;
}

static void
AppendSimulatorControlDebuggerValue(
	lua_State *L, int valueIndex, std::string& result )
{
	valueIndex = AbsoluteLuaIndex( L, valueIndex );
	switch ( lua_type( L, valueIndex ) )
	{
		case LUA_TNIL:
			result.append( "{\"type\":\"nil\",\"value\":null}" );
			break;
		case LUA_TBOOLEAN:
			result.append( lua_toboolean( L, valueIndex ) ?
				"{\"type\":\"boolean\",\"value\":true}" :
				"{\"type\":\"boolean\",\"value\":false}" );
			break;
		case LUA_TNUMBER:
		{
			lua_Number value = lua_tonumber( L, valueIndex );
			result.append( "{\"type\":\"number\",\"value\":" );
			if ( IsFiniteNumber( value ) )
			{
				AppendSimulatorControlNumber( result, value );
			}
			else
			{
				result.append( "null,\"description\":" );
				const char *description = value != value ?
					"nan" : ( value > 0 ? "infinity" : "-infinity" );
				AppendSimulatorControlJsonString(
					result, description, strlen( description ) );
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TSTRING:
		{
			size_t length = 0;
			const char *value = lua_tolstring( L, valueIndex, & length );
			size_t serializedLength = length > kSimulatorControlMaximumStringSize ?
				kSimulatorControlMaximumStringSize : length;
			result.append( "{\"type\":\"string\",\"value\":" );
			AppendSimulatorControlJsonString(
				result, value, serializedLength );
			char information[64];
			snprintf(
				information, sizeof( information ),
				",\"length\":%lu", (unsigned long)length );
			result.append( information );
			if ( serializedLength < length )
			{
				result.append( ",\"truncated\":true" );
			}
			result.push_back( '}' );
			break;
		}
		case LUA_TTABLE:
		{
			char information[80];
			snprintf(
				information, sizeof( information ),
				"{\"type\":\"table\",\"handle\":%d}",
				RegisterSimulatorControlHandle( L, valueIndex ) );
			result.append( information );
			break;
		}
		case LUA_TFUNCTION:
			result.append( "{\"type\":\"function\"}" );
			break;
		case LUA_TUSERDATA:
			result.append( "{\"type\":\"userdata\"}" );
			break;
		case LUA_TLIGHTUSERDATA:
			result.append( "{\"type\":\"lightuserdata\"}" );
			break;
		case LUA_TTHREAD:
			result.append( "{\"type\":\"thread\"}" );
			break;
		default:
			result.append( "{\"type\":\"unknown\"}" );
			break;
	}
}

static std::string
BuildSimulatorControlBreakpoints()
{
	SimulatorControlDebuggerConfiguration& configuration =
		GetSimulatorControlDebuggerConfiguration();
	std::string result( "{\"breakpoints\":[" );
	for ( size_t index = 0; index < configuration.breakpoints.size(); index++ )
	{
		if ( index > 0 )
		{
			result.push_back( ',' );
		}
		const SimulatorControlBreakpoint& breakpoint =
			configuration.breakpoints[index];
		char identity[96];
		snprintf(
			identity, sizeof( identity ),
			"{\"id\":%lu,\"path\":", breakpoint.id );
		result.append( identity );
		AppendSimulatorControlJsonString( result, breakpoint.path );
		char line[160];
		snprintf(
			line, sizeof( line ),
			",\"line\":%d,\"hitCount\":%lu,\"hits\":%lu,\"condition\":",
			breakpoint.line, breakpoint.hitCount, breakpoint.hits );
		result.append( line );
		if ( breakpoint.condition.empty() )
		{
			result.append( "null" );
		}
		else
		{
			AppendSimulatorControlJsonString(
				result, breakpoint.condition );
		}
		result.push_back( '}' );
	}
	result.append( "]}" );
	return result;
}

static std::string
BuildSimulatorControlDebuggerStatus(
	const SimulatorControlRuntimeState& state )
{
	char prefix[192];
	snprintf(
		prefix, sizeof( prefix ),
		"{\"paused\":%s,\"pauseSequence\":%lu,\"breakpointId\":%lu,"
		"\"line\":%d,\"reason\":",
		state.debuggerPaused ? "true" : "false",
		state.debuggerPauseSequence,
		state.debuggerBreakpointId,
		state.debuggerLine );
	std::string result( prefix );
	if ( state.debuggerPauseReason.empty() )
	{
		result.append( "null" );
	}
	else
	{
		AppendSimulatorControlJsonString(
			result, state.debuggerPauseReason );
	}
	result.append( ",\"source\":" );
	if ( state.debuggerSource.empty() )
	{
		result.append( "null" );
	}
	else
	{
		AppendSimulatorControlJsonString( result, state.debuggerSource );
	}
	result.append( ",\"projectSource\":" );
	if ( state.debuggerProjectSource.empty() )
	{
		result.append( "null" );
	}
	else
	{
		AppendSimulatorControlJsonString(
			result, state.debuggerProjectSource );
	}
	result.append( ",\"function\":" );
	if ( state.debuggerFunction.empty() )
	{
		result.append( "null" );
	}
	else
	{
		AppendSimulatorControlJsonString(
			result, state.debuggerFunction );
	}
	result.append( ",\"stepMode\":" );
	if ( state.debuggerStepMode.empty() )
	{
		result.append( "null" );
	}
	else
	{
		AppendSimulatorControlJsonString( result, state.debuggerStepMode );
	}
	result.append( ",\"conditionError\":" );
	if ( state.debuggerConditionError.empty() )
	{
		result.append( "null}" );
	}
	else
	{
		AppendSimulatorControlJsonString(
			result, state.debuggerConditionError );
		result.push_back( '}' );
	}
	return result;
}

static void
AppendSimulatorControlDebuggerFrameInformation(
	lua_State *L, lua_Debug& information, int level,
	std::string& result )
{
	char prefix[64];
	snprintf( prefix, sizeof( prefix ), "{\"level\":%d", level );
	result.append( prefix );
	if ( information.source )
	{
		std::string source =
			NormalizeSimulatorControlDebuggerPath( information.source );
		result.append( ",\"source\":" );
		AppendSimulatorControlJsonString( result, source );
	}
	if ( information.short_src[0] )
	{
		result.append( ",\"shortSource\":" );
		AppendSimulatorControlJsonString(
			result, information.short_src,
			strlen( information.short_src ) );
	}
	char line[64];
	snprintf(
		line, sizeof( line ), ",\"line\":%d", information.currentline );
	result.append( line );
	if ( information.name && information.name[0] )
	{
		result.append( ",\"function\":" );
		AppendSimulatorControlJsonString(
			result, information.name, strlen( information.name ) );
	}
	if ( information.what )
	{
		result.append( ",\"kind\":" );
		AppendSimulatorControlJsonString(
			result, information.what, strlen( information.what ) );
	}
	result.push_back( '}' );
}

static std::string
BuildSimulatorControlDebuggerStack(
	const SimulatorControlRuntimeState& state )
{
	std::string result( "{\"pauseSequence\":" );
	char sequence[48];
	snprintf(
		sequence, sizeof( sequence ),
		"%lu,\"frames\":[", state.debuggerPauseSequence );
	result.append( sequence );
	bool first = true;
	int level = 0;
	for ( ; level < kSimulatorControlMaximumDebuggerFrames; level++ )
	{
		lua_Debug information;
		memset( & information, 0, sizeof( information ) );
		if ( ! lua_getstack( state.debuggerThread, level, & information ) ||
			! lua_getinfo( state.debuggerThread, "nSl", & information ) )
		{
			break;
		}
		if ( ! first )
		{
			result.push_back( ',' );
		}
		AppendSimulatorControlDebuggerFrameInformation(
			state.debuggerThread, information, level, result );
		first = false;
	}
	lua_Debug extraFrame;
	bool truncated = level >= kSimulatorControlMaximumDebuggerFrames &&
		lua_getstack( state.debuggerThread, level, & extraFrame );
	result.append( truncated ?
		"],\"truncated\":true}" : "],\"truncated\":false}" );
	return result;
}

static void
AppendSimulatorControlDebuggerNamedValues(
	lua_State *L, const lua_Debug& information, int functionIndex,
	bool upvalues, std::string& result, bool& truncated )
{
	result.push_back( '[' );
	int emitted = 0;
	for ( int index = 1; ; index++ )
	{
		const char *name = upvalues ?
			lua_getupvalue( L, functionIndex, index ) :
			lua_getlocal( L, & information, index );
		if ( ! name )
		{
			break;
		}
		if ( emitted >= kSimulatorControlMaximumDebuggerValues )
		{
			lua_pop( L, 1 );
			truncated = true;
			break;
		}
		if ( emitted > 0 )
		{
			result.push_back( ',' );
		}
		result.append( "{\"name\":" );
		AppendSimulatorControlJsonString(
			result, name, strlen( name ) );
		result.append( ",\"value\":" );
		AppendSimulatorControlDebuggerValue( L, -1, result );
		result.push_back( '}' );
		lua_pop( L, 1 );
		emitted++;
	}
	result.push_back( ']' );
}

static std::string
BuildSimulatorControlDebuggerFrame(
	const SimulatorControlRuntimeState& state, int level )
{
	lua_State *L = state.debuggerThread;
	lua_Debug information;
	memset( & information, 0, sizeof( information ) );
	if ( ! lua_getstack( L, level, & information ) )
	{
		return std::string();
	}
	int originalTop = lua_gettop( L );
	if ( ! lua_getinfo( L, "nSlf", & information ) )
	{
		lua_settop( L, originalTop );
		return std::string();
	}
	int functionIndex = lua_gettop( L );
	std::string result( "{\"pauseSequence\":" );
	char sequence[48];
	snprintf(
		sequence, sizeof( sequence ),
		"%lu,\"frame\":", state.debuggerPauseSequence );
	result.append( sequence );
	AppendSimulatorControlDebuggerFrameInformation(
		L, information, level, result );
	result.append( ",\"locals\":" );
	bool truncated = false;
	AppendSimulatorControlDebuggerNamedValues(
		L, information, functionIndex, false, result, truncated );
	result.append( ",\"upvalues\":" );
	AppendSimulatorControlDebuggerNamedValues(
		L, information, functionIndex, true, result, truncated );
	result.append( truncated ?
		",\"truncated\":true}" : ",\"truncated\":false}" );
	lua_settop( L, originalTop );
	return result;
}

static void
SkipSimulatorControlPathWhitespace( const std::string& path, size_t& offset )
{
	while ( offset < path.length() && isspace( (unsigned char)path[offset] ) )
	{
		offset++;
	}
}

static bool
ParseSimulatorControlIdentifier(
	const std::string& path, size_t& offset, std::string& result )
{
	size_t start = offset;
	if ( offset >= path.length() ||
		( '_' != path[offset] && ! isalpha( (unsigned char)path[offset] ) ) )
	{
		return false;
	}
	offset++;
	while ( offset < path.length() &&
		( '_' == path[offset] || isalnum( (unsigned char)path[offset] ) ) )
	{
		offset++;
	}
	result.assign( path, start, offset - start );
	return true;
}

static bool
ParseSimulatorControlQuotedString(
	const std::string& path, size_t& offset, std::string& result, std::string& error )
{
	if ( offset >= path.length() || ( '\'' != path[offset] && '"' != path[offset] ) )
	{
		return false;
	}

	char quote = path[offset++];
	result.clear();
	while ( offset < path.length() )
	{
		char character = path[offset++];
		if ( quote == character )
		{
			return true;
		}
		if ( '\\' == character )
		{
			if ( offset >= path.length() )
			{
					error = "inspect-lua-value path ends inside an escape sequence";
				return false;
			}
			character = path[offset++];
			switch ( character )
			{
				case 'n': result.push_back( '\n' ); break;
				case 'r': result.push_back( '\r' ); break;
				case 't': result.push_back( '\t' ); break;
				case '\\': result.push_back( '\\' ); break;
				case '\'': result.push_back( '\'' ); break;
				case '"': result.push_back( '"' ); break;
				default:
					error = "inspect-lua-value path contains an unsupported escape sequence";
					return false;
			}
		}
		else
		{
			result.push_back( character );
		}
	}

	error = "inspect-lua-value path contains an unterminated string";
	return false;
}

static bool
PushSimulatorControlInspectedValue(
	lua_State *L, const std::string& path, std::string& error )
{
	size_t offset = 0;
	SkipSimulatorControlPathWhitespace( path, offset );
	if ( offset >= path.length() )
	{
		error = "inspect-lua-value expects a global path or handle";
		return false;
	}

	if ( '@' == path[offset] )
	{
		offset++;
		size_t start = offset;
		while ( offset < path.length() && isdigit( (unsigned char)path[offset] ) )
		{
			offset++;
		}
		if ( start == offset )
		{
			error = "inspect-lua-value handle must contain a number after '@'";
			return false;
		}
		int handle = atoi( path.substr( start, offset - start ).c_str() );
		if ( handle <= 0 || ! PushSimulatorControlHandle( L, handle ) )
		{
			error = "inspect-lua-value handle has expired or does not exist";
			return false;
		}
	}
	else
	{
		std::string identifier;
		if ( ! ParseSimulatorControlIdentifier( path, offset, identifier ) )
		{
			error = "inspect-lua-value path must start with a global identifier or handle";
			return false;
		}
		lua_pushvalue( L, LUA_GLOBALSINDEX );
		lua_pushlstring( L, identifier.data(), identifier.length() );
		lua_rawget( L, -2 );
		lua_remove( L, -2 );
	}

	for ( ;; )
	{
		SkipSimulatorControlPathWhitespace( path, offset );
		if ( offset >= path.length() )
		{
			return true;
		}

		if ( ! lua_istable( L, -1 ) )
		{
			lua_pop( L, 1 );
			error = "inspect-lua-value path attempts to index a non-table value";
			return false;
		}

		if ( '.' == path[offset] )
		{
			offset++;
			std::string identifier;
			if ( ! ParseSimulatorControlIdentifier( path, offset, identifier ) )
			{
				lua_pop( L, 1 );
				error = "inspect-lua-value path expects a field name after '.'";
				return false;
			}
			lua_pushlstring( L, identifier.data(), identifier.length() );
			lua_rawget( L, -2 );
			lua_remove( L, -2 );
		}
		else if ( '[' == path[offset] )
		{
			offset++;
			SkipSimulatorControlPathWhitespace( path, offset );
			if ( offset >= path.length() )
			{
				lua_pop( L, 1 );
				error = "inspect-lua-value path contains an unterminated '['";
				return false;
			}

			if ( '\'' == path[offset] || '"' == path[offset] )
			{
				std::string key;
				if ( ! ParseSimulatorControlQuotedString( path, offset, key, error ) )
				{
					lua_pop( L, 1 );
					return false;
				}
				lua_pushlstring( L, key.data(), key.length() );
			}
			else
			{
				bool negative = false;
				if ( '-' == path[offset] )
				{
					negative = true;
					offset++;
				}
				size_t start = offset;
				while ( offset < path.length() && isdigit( (unsigned char)path[offset] ) )
				{
					offset++;
				}
				if ( start == offset )
				{
					lua_pop( L, 1 );
					error = "inspect-lua-value brackets support only integer or quoted-string keys";
					return false;
				}
				lua_Integer value = (lua_Integer)atol( path.substr( start, offset - start ).c_str() );
				lua_pushinteger( L, negative ? -value : value );
			}

			SkipSimulatorControlPathWhitespace( path, offset );
			if ( offset >= path.length() || ']' != path[offset] )
			{
				lua_pop( L, 2 );
				error = "inspect-lua-value path expects a closing ']'";
				return false;
			}
			offset++;
			lua_rawget( L, -2 );
			lua_remove( L, -2 );
		}
		else
		{
			lua_pop( L, 1 );
			error = "inspect-lua-value path supports only '.', integer keys, and quoted-string keys";
			return false;
		}
	}
}

static std::string
BuildSimulatorControlInspection( lua_State *L, int valueIndex, int cursor )
{
	valueIndex = AbsoluteLuaIndex( L, valueIndex );
	if ( ! lua_istable( L, valueIndex ) )
	{
		std::string result;
		AppendSimulatorControlValueSummary( L, valueIndex, result );
		return result;
	}

	int handle = RegisterSimulatorControlHandle( L, valueIndex );
	std::string entries;
	int count = 0;
	int emitted = 0;
	lua_pushnil( L );
	while ( lua_next( L, valueIndex ) )
	{
		if ( count >= cursor && emitted < kSimulatorControlMaximumEntries &&
			entries.length() < kSimulatorControlMaximumResponseSize )
		{
			if ( emitted > 0 )
			{
				entries.push_back( ',' );
			}
			entries.append( "{\"key\":" );
			AppendSimulatorControlValueSummary( L, -2, entries );
			entries.append( ",\"value\":" );
			AppendSimulatorControlValueSummary( L, -1, entries );
			entries.push_back( '}' );
			emitted++;
		}
		count++;
		lua_pop( L, 1 );
	}

	char prefix[128];
	snprintf(
		prefix, sizeof( prefix ),
		"{\"type\":\"table\",\"handle\":%d,\"count\":%d,\"cursor\":%d,\"entries\":[",
		handle, count, cursor );
	std::string result( prefix );
	result.append( entries );
	result.push_back( ']' );
	if ( cursor + emitted < count )
	{
		char suffix[96];
		snprintf(
			suffix, sizeof( suffix ),
			",\"truncated\":true,\"nextCursor\":%d",
			cursor + emitted );
		result.append( suffix );
	}
	else
	{
		result.append( ",\"truncated\":false" );
	}
	result.push_back( '}' );
	return result;
}

static int
SimulatorControlTraceback( lua_State *L )
{
	const char *message = lua_tostring( L, 1 );
	if ( ! message )
	{
		message = "(error object is not a string)";
	}

	lua_getfield( L, LUA_GLOBALSINDEX, "debug" );
	if ( ! lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushstring( L, message );
		return 1;
	}
	lua_getfield( L, -1, "traceback" );
	if ( ! lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		lua_pushstring( L, message );
		return 1;
	}
	lua_pushstring( L, message );
	lua_pushinteger( L, 2 );
	lua_call( L, 2, 1 );
	return 1;
}

static bool
ExecuteSimulatorControlChunk(
	lua_State *L, const std::string& payload, const char *chunkName,
	bool isExpression, bool isFile, std::string& jsonResult, std::string& error,
	bool *firstResultTruthy = NULL, bool fullJson = false,
	const char **errorCode = NULL )
{
	if ( errorCode )
	{
		*errorCode = "lua-error";
	}
	int originalTop = lua_gettop( L );
	int status = 0;
	if ( isFile )
	{
		if ( payload.find( '\0' ) != std::string::npos )
		{
			error = "execute-lua-file path cannot contain a null byte";
			return false;
		}
		std::string code;
		if ( ! ReadSimulatorControlFile(
			payload, code, kSimulatorControlMaximumExecFileSize ) )
		{
			error =
				"unable to read execute-lua-file or file exceeded sixteen megabytes";
			return false;
		}
		std::string fileChunkName = "@" + payload;
		status = luaL_loadbuffer(
			L, code.data(), code.length(), fileChunkName.c_str() );
	}
	else
	{
		std::string code = isExpression ? std::string( "return " ) + payload : payload;
		status = luaL_loadbuffer( L, code.data(), code.length(), chunkName );
	}

	if ( 0 != status )
	{
		const char *message = lua_tostring( L, -1 );
		error = message ? message : "unable to load Lua code";
		lua_settop( L, originalTop );
		return false;
	}

	int functionIndex = originalTop + 1;
	lua_pushcfunction( L, SimulatorControlTraceback );
	lua_insert( L, functionIndex );
	status = lua_pcall( L, 0, LUA_MULTRET, functionIndex );
	lua_remove( L, functionIndex );
	if ( 0 != status )
	{
		const char *message = lua_tostring( L, -1 );
		error = message ? message : "Lua execution failed";
		lua_settop( L, originalTop );
		return false;
	}

	jsonResult.assign( "[" );
	int resultCount = lua_gettop( L ) - originalTop;
	if ( firstResultTruthy )
	{
		*firstResultTruthy = resultCount > 0 &&
			0 != lua_toboolean( L, originalTop + 1 );
	}
	for ( int index = 0; index < resultCount; index++ )
	{
		if ( index > 0 )
		{
			jsonResult.push_back( ',' );
		}
		if ( fullJson )
		{
			// Keep encoder errors inside Lua's protected call. In particular,
			// macOS Lua exceptions must not unwind through a C++ callback.
			luaL_loadstring( L,
				"local value = ...; return require('json').encode(value, {"
				"exception = function(reason, value, state, message) error(message) end})" );
			lua_pushvalue( L, originalTop + index + 1 );
			if ( 0 != lua_pcall( L, 1, 1, 0 ) )
			{
				error = "Lua ran, but its result could not be encoded: ";
				const char *message = lua_tostring( L, -1 );
				error.append( message ? message : "JSON encoder raised a non-string error" );
				if ( errorCode )
				{
					*errorCode = "serialization-error";
				}
				lua_settop( L, originalTop );
				return false;
			}
			size_t length = 0;
			const char *encoded = lua_tolstring( L, -1, & length );
			if ( jsonResult.length() + length + 1 > kSimulatorControlMaximumJsonResultSize )
			{
				error = "Lua ran, but its JSON result exceeded sixteen megabytes";
				if ( errorCode )
				{
					*errorCode = "result-too-large";
				}
				lua_settop( L, originalTop );
				return false;
			}
			jsonResult.append( encoded, length );
			lua_pop( L, 1 );
		}
		else
		{
			AppendSimulatorControlValueSummary( L, originalTop + index + 1, jsonResult );
		}
	}
	jsonResult.push_back( ']' );
	lua_settop( L, originalTop );
	return true;
}

static int
SimulatorControlDebuggerEnvironmentIndex( lua_State *L )
{
	lua_pushvalue( L, 2 );
	lua_rawget( L, lua_upvalueindex( 1 ) );
	bool isShadowedNil = 0 != lua_toboolean( L, -1 );
	lua_pop( L, 1 );
	if ( isShadowedNil )
	{
		return 0;
	}

	lua_pushvalue( L, 2 );
	lua_rawget( L, lua_upvalueindex( 2 ) );
	return 1;
}

static void
SetSimulatorControlDebuggerEnvironmentValue(
	lua_State *L, int environmentIndex, int nilNamesIndex,
	const char *name, int valueIndex )
{
	environmentIndex = AbsoluteLuaIndex( L, environmentIndex );
	nilNamesIndex = AbsoluteLuaIndex( L, nilNamesIndex );
	valueIndex = AbsoluteLuaIndex( L, valueIndex );

	lua_pushstring( L, name );
	lua_pushvalue( L, valueIndex );
	lua_rawset( L, environmentIndex );

	lua_pushstring( L, name );
	if ( lua_isnil( L, valueIndex ) )
	{
		lua_pushboolean( L, 1 );
	}
	else
	{
		lua_pushnil( L );
	}
	lua_rawset( L, nilNamesIndex );
}

static bool
EvaluateSimulatorControlDebuggerExpression(
	SimulatorControlRuntimeState& state, int frameLevel,
	const std::string& expression, std::string& jsonResult,
	std::string& error, bool *firstResultTruthy = NULL,
	bool serializeResults = true )
{
	lua_State *L = state.debuggerThread;
	if ( ! L || frameLevel < 0 )
	{
		error = "debugger frame level was not found";
		return false;
	}
	if ( expression.empty() )
	{
		error = "debugger frame evaluation expects a Lua expression";
		return false;
	}
	if ( expression.length() > kSimulatorControlMaximumRequestSize ||
		expression.find( '\0' ) != std::string::npos )
	{
		error = "debugger frame expression is too large or contains a null byte";
		return false;
	}

	int originalTop = lua_gettop( L );
	lua_Debug information;
	memset( & information, 0, sizeof( information ) );
	if ( ! lua_getstack( L, frameLevel, & information ) ||
		! lua_getinfo( L, "f", & information ) )
	{
		lua_settop( L, originalTop );
		error = "debugger frame level was not found";
		return false;
	}
	int frameFunctionIndex = lua_gettop( L );

	std::string code( "return " );
	code.append( expression );
	int status = luaL_loadbuffer(
		L, code.data(), code.length(), "=@simulator-control/debugger-frame" );
	if ( 0 != status )
	{
		const char *message = lua_tostring( L, -1 );
		error = message ? message : "unable to load debugger frame expression";
		lua_settop( L, originalTop );
		return false;
	}
	int evaluationFunctionIndex = lua_gettop( L );

	lua_newtable( L );
	int environmentIndex = lua_gettop( L );
	lua_newtable( L );
	int nilNamesIndex = lua_gettop( L );

	for ( int index = 1; ; index++ )
	{
		const char *name = lua_getupvalue( L, frameFunctionIndex, index );
		if ( ! name )
		{
			break;
		}
		SetSimulatorControlDebuggerEnvironmentValue(
			L, environmentIndex, nilNamesIndex, name, -1 );
		lua_pop( L, 1 );
	}
	for ( int index = 1; ; index++ )
	{
		const char *name = lua_getlocal( L, & information, index );
		if ( ! name )
		{
			break;
		}
		SetSimulatorControlDebuggerEnvironmentValue(
			L, environmentIndex, nilNamesIndex, name, -1 );
		lua_pop( L, 1 );
	}

	lua_newtable( L );
	lua_pushvalue( L, nilNamesIndex );
	lua_pushvalue( L, LUA_GLOBALSINDEX );
	lua_pushcclosure( L, SimulatorControlDebuggerEnvironmentIndex, 2 );
	lua_setfield( L, -2, "__index" );
	lua_setmetatable( L, environmentIndex );
	lua_pushvalue( L, environmentIndex );
	lua_setfenv( L, evaluationFunctionIndex );

	lua_settop( L, evaluationFunctionIndex );
	lua_remove( L, frameFunctionIndex );
	int functionIndex = originalTop + 1;
	lua_pushcfunction( L, SimulatorControlTraceback );
	lua_insert( L, functionIndex );
	bool wasEvaluating = state.debuggerEvaluating;
	state.debuggerEvaluating = true;
	status = lua_pcall( L, 0, LUA_MULTRET, functionIndex );
	state.debuggerEvaluating = wasEvaluating;
	lua_remove( L, functionIndex );
	if ( 0 != status )
	{
		const char *message = lua_tostring( L, -1 );
		error = message ? message : "debugger frame evaluation failed";
		lua_settop( L, originalTop );
		return false;
	}

	jsonResult.assign( "[" );
	int resultCount = lua_gettop( L ) - originalTop;
	if ( firstResultTruthy )
	{
		*firstResultTruthy = resultCount > 0 &&
			0 != lua_toboolean( L, originalTop + 1 );
	}
	for ( int index = 0; index < resultCount; index++ )
	{
		if ( ! serializeResults )
		{
			break;
		}
		if ( index > 0 )
		{
			jsonResult.push_back( ',' );
		}
		AppendSimulatorControlValueSummary(
			L, originalTop + index + 1, jsonResult );
	}
	jsonResult.push_back( ']' );
	lua_settop( L, originalTop );
	return true;
}

static void
StripSimulatorControlCarriageReturn( std::string& value )
{
	if ( ! value.empty() && '\r' == value[value.length() - 1] )
	{
		value.erase( value.length() - 1 );
	}
}

static bool
ParseSimulatorControlRequest(
	const std::string& contents, std::string& identifier,
	std::string& sessionId, std::string& command,
	std::string& payload, std::string& error )
{
	size_t firstLineEnd = contents.find( '\n' );
	size_t secondLineEnd =
		std::string::npos == firstLineEnd ? std::string::npos : contents.find( '\n', firstLineEnd + 1 );
	size_t thirdLineEnd =
		std::string::npos == secondLineEnd ? std::string::npos : contents.find( '\n', secondLineEnd + 1 );
	if ( std::string::npos == firstLineEnd ||
		std::string::npos == secondLineEnd ||
		std::string::npos == thirdLineEnd )
	{
		error =
			"control request must contain identifier, session, and command lines";
		return false;
	}

	identifier.assign( contents, 0, firstLineEnd );
	sessionId.assign(
		contents, firstLineEnd + 1, secondLineEnd - firstLineEnd - 1 );
	command.assign(
		contents, secondLineEnd + 1, thirdLineEnd - secondLineEnd - 1 );
	payload.assign(
		contents, thirdLineEnd + 1, contents.length() - thirdLineEnd - 1 );
	StripSimulatorControlCarriageReturn( identifier );
	StripSimulatorControlCarriageReturn( sessionId );
	StripSimulatorControlCarriageReturn( command );

	if ( identifier.empty() || identifier.length() > 128 )
	{
		error = "control request identifier must contain 1 through 128 characters";
		identifier.clear();
		return false;
	}
	for ( size_t index = 0; index < identifier.length(); index++ )
	{
		char character = identifier[index];
		if ( '-' != character && '_' != character &&
			! isalnum( (unsigned char)character ) )
		{
			error = "control request identifier contains an unsupported character";
			identifier.clear();
			return false;
		}
	}
	if ( sessionId.empty() || sessionId.length() > 127 )
	{
		error = "control request session ID must contain 1 through 127 characters";
		return false;
	}
	for ( size_t index = 0; index < sessionId.length(); index++ )
	{
		char character = sessionId[index];
		if ( '-' != character && '_' != character &&
			! isalnum( (unsigned char)character ) )
		{
			error = "control request session ID contains an unsupported character";
			return false;
		}
	}
	if ( command.empty() || command.length() > 32 )
	{
		error = "control request command must contain 1 through 32 characters";
		return false;
	}
	return true;
}

static bool
ParseSimulatorControlInspectPayload(
	const std::string& payload, std::string& path, int& cursor, std::string& error )
{
	size_t lineEnd = payload.find( '\n' );
	path = std::string::npos == lineEnd ? payload : payload.substr( 0, lineEnd );
	StripSimulatorControlCarriageReturn( path );
	cursor = 0;

	if ( std::string::npos != lineEnd )
	{
		std::string options = payload.substr( lineEnd + 1 );
		StripSimulatorControlCarriageReturn( options );
		if ( ! options.empty() )
		{
			static const char kCursorPrefix[] = "cursor=";
			if ( 0 != options.compare( 0, sizeof( kCursorPrefix ) - 1, kCursorPrefix ) )
			{
				error = "inspect-lua-value supports only a cursor option";
				return false;
			}
			std::string cursorString = options.substr( sizeof( kCursorPrefix ) - 1 );
			if ( cursorString.empty() )
			{
				error = "inspect-lua-value cursor must be a non-negative integer";
				return false;
			}
			for ( size_t index = 0; index < cursorString.length(); index++ )
			{
				if ( ! isdigit( (unsigned char)cursorString[index] ) )
				{
					error = "inspect-lua-value cursor must be a non-negative integer";
					return false;
				}
			}
			errno = 0;
			char *cursorEnd = NULL;
			unsigned long cursorValue =
				strtoul( cursorString.c_str(), & cursorEnd, 10 );
			if ( ERANGE == errno || ! cursorEnd || cursorEnd[0] ||
				cursorValue > INT_MAX )
			{
				error = "inspect-lua-value cursor is too large";
				return false;
			}
			cursor = (int)cursorValue;
		}
	}
	return true;
}

static bool
ParseSimulatorControlTapPayload(
	const std::string& payload, MSimulatorHost::Input& input,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string extra;
	if ( ! ( stream >> input.x >> input.y ) || stream >> extra ||
		! IsFiniteNumber( (lua_Number)input.x ) ||
		! IsFiniteNumber( (lua_Number)input.y ) )
	{
		error = "tap-screen expects finite X and Y screen coordinates";
		return false;
	}
	input.type = MSimulatorHost::Input::kTouchInput;
	input.phase = MSimulatorHost::Input::kBeganPhase;
	input.xStart = input.x;
	input.yStart = input.y;
	return true;
}

static bool
ParseSimulatorControlKeyPayload(
	const std::string& payload, MSimulatorHost::Input& input,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string phase( "pressed" );
	if ( ! ( stream >> input.keyName ) || input.keyName.length() > 128 )
	{
		error = "send-key-event expects a key name, optional phase, and modifiers";
		return false;
	}
	std::string argument;
	bool hasPhase = false;
	while ( stream >> argument )
	{
		if ( "down" == argument || "up" == argument ||
			"pressed" == argument )
		{
			if ( hasPhase )
			{
				error = "send-key-event phase can only be provided once";
				return false;
			}
			phase = argument;
			hasPhase = true;
		}
		else if ( "--shift" == argument )
		{
			input.isShiftDown = true;
		}
		else if ( "--alt" == argument )
		{
			input.isAltDown = true;
		}
		else if ( "--ctrl" == argument )
		{
			input.isCtrlDown = true;
		}
		else if ( "--command" == argument )
		{
			input.isCommandDown = true;
		}
		else
		{
			error = "send-key-event modifiers are --shift, --alt, --ctrl, and --command";
			return false;
		}
	}

	input.type = MSimulatorHost::Input::kKeyInput;
	if ( "pressed" == phase )
	{
		input.phase = MSimulatorHost::Input::kPressedPhase;
	}
	else if ( "down" == phase )
	{
		input.phase = MSimulatorHost::Input::kDownPhase;
	}
	else if ( "up" == phase )
	{
		input.phase = MSimulatorHost::Input::kUpPhase;
	}
	else
	{
		error = "send-key-event phase must be 'down', 'up', or 'pressed'";
		return false;
	}
	return true;
}

static bool
ParseSimulatorControlScrollPayload(
	const std::string& payload, MSimulatorHost::Input& input,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string extra;
	if ( ! ( stream >> input.x >> input.y >> input.scrollX >> input.scrollY ) ||
		stream >> extra ||
		! IsFiniteNumber( (lua_Number)input.x ) ||
		! IsFiniteNumber( (lua_Number)input.y ) ||
		! IsFiniteNumber( (lua_Number)input.scrollX ) ||
		! IsFiniteNumber( (lua_Number)input.scrollY ) )
	{
		error = "send-scroll-event expects finite X, Y, SCROLL_X, and SCROLL_Y values";
		return false;
	}
	input.type = MSimulatorHost::Input::kMouseInput;
	input.phase = MSimulatorHost::Input::kScrollPhase;
	return true;
}

static bool
ParseSimulatorControlTouchPayload(
	const std::string& payload, MSimulatorHost::Input& input,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string phase;
	std::vector< double > values;
	double value = 0.0;
	std::string extra;
	if ( ! ( stream >> phase ) )
	{
		error = "send-touch-event expects PHASE X Y and optional X_START Y_START";
		return false;
	}
	while ( stream >> value )
	{
		values.push_back( value );
	}
	if ( ! stream.eof() || ( 2 != values.size() && 4 != values.size() ) )
	{
		error = "send-touch-event expects PHASE X Y and optional X_START Y_START";
		return false;
	}
	for ( size_t index = 0; index < values.size(); index++ )
	{
		if ( ! IsFiniteNumber( (lua_Number)values[index] ) )
		{
			error = "send-touch-event coordinates must be finite";
			return false;
		}
	}
	if ( "began" == phase )
	{
		input.phase = MSimulatorHost::Input::kBeganPhase;
	}
	else if ( "moved" == phase )
	{
		input.phase = MSimulatorHost::Input::kMovedPhase;
	}
	else if ( "ended" == phase )
	{
		input.phase = MSimulatorHost::Input::kEndedPhase;
	}
	else if ( "cancelled" == phase )
	{
		input.phase = MSimulatorHost::Input::kCancelledPhase;
	}
	else
	{
		error = "send-touch-event phase must be began, moved, ended, or cancelled";
		return false;
	}
	input.type = MSimulatorHost::Input::kTouchInput;
	input.x = values[0];
	input.y = values[1];
	input.xStart = 4 == values.size() ? values[2] : input.x;
	input.yStart = 4 == values.size() ? values[3] : input.y;
	return true;
}

static bool
ParseSimulatorControlMousePayload(
	const std::string& payload, MSimulatorHost::Input& input,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string phase;
	std::string extra;
	if ( ! ( stream >> phase >> input.x >> input.y ) ||
		! IsFiniteNumber( (lua_Number)input.x ) ||
		! IsFiniteNumber( (lua_Number)input.y ) )
	{
		error = "send-mouse-event expects PHASE X Y and optional SCROLL_X SCROLL_Y";
		return false;
	}
	input.type = MSimulatorHost::Input::kMouseInput;
	if ( "down" == phase )
	{
		input.phase = MSimulatorHost::Input::kDownPhase;
		input.clickCount = 1;
		input.isPrimaryButtonDown = true;
	}
	else if ( "up" == phase )
	{
		input.phase = MSimulatorHost::Input::kUpPhase;
		input.clickCount = 1;
	}
	else if ( "drag" == phase )
	{
		input.phase = MSimulatorHost::Input::kDragPhase;
		input.isPrimaryButtonDown = true;
	}
	else if ( "move" == phase )
	{
		input.phase = MSimulatorHost::Input::kMovePhase;
	}
	else if ( "exit" == phase )
	{
		input.phase = MSimulatorHost::Input::kExitPhase;
	}
	else if ( "scroll" == phase )
	{
		input.phase = MSimulatorHost::Input::kScrollPhase;
		if ( ! ( stream >> input.scrollX >> input.scrollY ) ||
			! IsFiniteNumber( (lua_Number)input.scrollX ) ||
			! IsFiniteNumber( (lua_Number)input.scrollY ) )
		{
			error = "send-mouse-event scroll expects PHASE X Y SCROLL_X SCROLL_Y";
			return false;
		}
	}
	else
	{
		error = "send-mouse-event phase must be down, up, drag, move, exit, or scroll";
		return false;
	}
	if ( stream >> extra )
	{
		error = "send-mouse-event received unexpected arguments";
		return false;
	}
	return true;
}

struct SimulatorControlSwipe
{
	SimulatorControlSwipe()
	: xStart( 0.0 ), yStart( 0.0 ), xEnd( 0.0 ), yEnd( 0.0 ), steps( 8 )
	{
	}
	double xStart;
	double yStart;
	double xEnd;
	double yEnd;
	int steps;
};

static bool
ParseSimulatorControlSwipePayload(
	const std::string& payload, SimulatorControlSwipe& swipe,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string steps;
	std::string extra;
	if ( ! ( stream >> swipe.xStart >> swipe.yStart >> swipe.xEnd >> swipe.yEnd ) )
	{
		error = "swipe-screen expects X_START Y_START X_END Y_END and optional STEPS";
		return false;
	}
	if ( stream >> steps )
	{
		std::istringstream stepStream( steps );
		if ( ! ( stepStream >> swipe.steps ) || stepStream >> extra ||
			swipe.steps < 1 || swipe.steps > 120 )
		{
			error = "swipe-screen steps must be an integer from 1 through 120";
			return false;
		}
	}
	if ( stream >> extra ||
		! IsFiniteNumber( (lua_Number)swipe.xStart ) ||
		! IsFiniteNumber( (lua_Number)swipe.yStart ) ||
		! IsFiniteNumber( (lua_Number)swipe.xEnd ) ||
		! IsFiniteNumber( (lua_Number)swipe.yEnd ) )
	{
		error = "swipe-screen coordinates must be finite";
		return false;
	}
	return true;
}

struct SimulatorControlNodeSwipe
{
	SimulatorControlNodeSwipe()
	: deltaX( 0.0 ), deltaY( 0.0 ), steps( 8 )
	{
	}
	std::string selector;
	double deltaX;
	double deltaY;
	int steps;
};

static bool
ParseSimulatorControlNodeSwipePayload(
	const std::string& payload, SimulatorControlNodeSwipe& swipe,
	std::string& error )
{
	std::istringstream stream( payload );
	std::string steps;
	std::string extra;
	if ( ! ( stream >> swipe.selector >> swipe.deltaX >> swipe.deltaY ) )
	{
		error = "swipe-display-object expects ID|@HANDLE DELTA_X DELTA_Y and optional STEPS";
		return false;
	}
	if ( stream >> steps )
	{
		std::istringstream stepStream( steps );
		if ( ! ( stepStream >> swipe.steps ) || stepStream >> extra ||
			swipe.steps < 1 || swipe.steps > 120 )
		{
			error = "swipe-display-object steps must be an integer from 1 through 120";
			return false;
		}
	}
	if ( stream >> extra ||
		! IsFiniteNumber( (lua_Number)swipe.deltaX ) ||
		! IsFiniteNumber( (lua_Number)swipe.deltaY ) )
	{
		error = "swipe-display-object deltas must be finite";
		return false;
	}
	return true;
}

static bool
SendSimulatorControlSwipe(
	const MSimulatorHost& host, const SimulatorControlSwipe& swipe )
{
	MSimulatorHost::Input input;
	input.type = MSimulatorHost::Input::kTouchInput;
	input.phase = MSimulatorHost::Input::kBeganPhase;
	input.x = input.xStart = swipe.xStart;
	input.y = input.yStart = swipe.yStart;
	if ( ! host.SendInput( input ) )
	{
		return false;
	}
	input.phase = MSimulatorHost::Input::kMovedPhase;
	for ( int step = 1; step <= swipe.steps; step++ )
	{
		double progress = (double)step / (double)( swipe.steps + 1 );
		input.x = swipe.xStart + ( swipe.xEnd - swipe.xStart ) * progress;
		input.y = swipe.yStart + ( swipe.yEnd - swipe.yStart ) * progress;
		if ( ! host.SendInput( input ) )
		{
			return false;
		}
	}
	input.phase = MSimulatorHost::Input::kEndedPhase;
	input.x = swipe.xEnd;
	input.y = swipe.yEnd;
	return host.SendInput( input );
}

static bool
ParseSimulatorControlControllerArguments(
	const std::vector< std::string >& arguments,
	bool allowConfiguration, MSimulatorHost::Input& input,
	std::vector< std::string >& positionalArguments,
	std::string& error )
{
	bool hasIdentifier = false;
	for ( size_t index = 0; index < arguments.size(); index++ )
	{
		const std::string& argument = arguments[index];
		if ( "--id" != argument &&
			"--profile" != argument &&
			"--player" != argument )
		{
			positionalArguments.push_back( argument );
			continue;
		}
		if ( index + 1 >= arguments.size() )
		{
			error = argument + " expects a value";
			return false;
		}

		const std::string& value = arguments[++index];
		if ( "--id" == argument )
		{
			if ( hasIdentifier || value.empty() || value.length() > 64 )
			{
				error = "controller --id expects one identifier of 1 through 64 characters";
				return false;
			}
			for ( size_t characterIndex = 0;
				characterIndex < value.length(); characterIndex++ )
			{
				const char character = value[characterIndex];
				if ( '-' != character && '_' != character &&
					'.' != character &&
					! isalnum( (unsigned char)character ) )
				{
					error = "controller --id supports letters, numbers, periods, hyphens, and underscores";
					return false;
				}
			}
			hasIdentifier = true;
			input.controllerId = value;
		}
		else if ( ! allowConfiguration )
		{
			error = argument + " is only supported by controller connect";
			return false;
		}
		else if ( "--profile" == argument )
		{
			if ( input.hasControllerProfile ||
				( "xbox" != value && "playstation" != value &&
					"nintendo" != value && "generic" != value ) )
			{
				error = "controller --profile expects xbox, playstation, nintendo, or generic";
				return false;
			}
			input.hasControllerProfile = true;
			input.controllerProfile = value;
		}
		else
		{
			if ( input.hasControllerPlayerNumber )
			{
				error = "controller --player can only be provided once";
				return false;
			}
			std::istringstream playerStream( value );
			std::string extra;
			if ( ! ( playerStream >> input.controllerPlayerNumber ) ||
				playerStream >> extra ||
				input.controllerPlayerNumber < 1 ||
				input.controllerPlayerNumber > 4 )
			{
				error = "controller --player expects a number from 1 through 4";
				return false;
			}
			input.hasControllerPlayerNumber = true;
		}
	}
	return true;
}

static bool
ParseSimulatorControlControllerPayload(
	const std::string& payload, MSimulatorHost::Input& input,
	std::string& error )
{
	std::istringstream stream( payload );
	std::vector< std::string > arguments;
	std::string argument;
	while ( stream >> argument )
	{
		arguments.push_back( argument );
	}
	if ( arguments.empty() )
	{
		error = "controller expects connect, disconnect, send-button-event, or set-axis";
		return false;
	}

	const std::string action = arguments.front();
	input.type = MSimulatorHost::Input::kControllerInput;
	std::vector< std::string > positionalArguments;
	if ( ! ParseSimulatorControlControllerArguments(
		std::vector< std::string >( arguments.begin() + 1, arguments.end() ),
		"connect" == action, input, positionalArguments, error ) )
	{
		return false;
	}
	if ( "connect" == action || "disconnect" == action )
	{
		if ( ! positionalArguments.empty() )
		{
			error = "controller connect and disconnect only accept options";
			return false;
		}
		input.controllerAction = "connect" == action ?
			MSimulatorHost::Input::kConnectController :
			MSimulatorHost::Input::kDisconnectController;
		return true;
	}
	if ( "send-button-event" == action )
	{
		if ( positionalArguments.empty() ||
			positionalArguments.size() > 2 ||
			positionalArguments.front().length() > 128 )
		{
			error = "controller send-button-event expects a key name and optional phase";
			return false;
		}
		input.keyName = positionalArguments.front();
		const std::string phase = positionalArguments.size() > 1 ?
			positionalArguments[1] : "pressed";
		input.controllerAction = MSimulatorHost::Input::kButtonController;
		if ( "pressed" == phase )
		{
			input.phase = MSimulatorHost::Input::kPressedPhase;
		}
		else if ( "down" == phase )
		{
			input.phase = MSimulatorHost::Input::kDownPhase;
		}
		else if ( "up" == phase )
		{
			input.phase = MSimulatorHost::Input::kUpPhase;
		}
		else
		{
			error = "controller send-button-event phase must be 'down', 'up', or 'pressed'";
			return false;
		}
		return true;
	}
	if ( "set-axis" == action )
	{
		if ( 2 != positionalArguments.size() )
		{
			error = "controller set-axis expects an axis name and finite value";
			return false;
		}
		input.axisName = positionalArguments.front();
		std::istringstream valueStream( positionalArguments[1] );
		std::string extra;
		if ( ! ( valueStream >> input.axisValue ) ||
			valueStream >> extra ||
			! IsFiniteNumber( (lua_Number)input.axisValue ) )
		{
			error = "controller set-axis expects an axis name and finite value";
			return false;
		}
		const bool isTrigger =
			"leftTrigger" == input.axisName ||
			"rightTrigger" == input.axisName;
		const bool isStick =
			"leftX" == input.axisName ||
			"leftY" == input.axisName ||
			"rightX" == input.axisName ||
			"rightY" == input.axisName;
		if ( ( ! isTrigger && ! isStick ) ||
			input.axisValue < ( isTrigger ? 0.0 : -1.0 ) ||
			input.axisValue > 1.0 )
		{
			error = "controller set-axis supports leftX, leftY, rightX, rightY (-1 to 1), and leftTrigger or rightTrigger (0 to 1)";
			return false;
		}
		input.controllerAction = MSimulatorHost::Input::kAxisController;
		return true;
	}

	error = "controller expects connect, disconnect, send-button-event, or set-axis";
	return false;
}

static bool
HasSimulatorControlMp4Extension( const std::string& path )
{
	if ( path.length() < 4 )
	{
		return false;
	}
	const size_t extension = path.length() - 4;
	return '.' == path[extension] &&
		'm' == tolower( (unsigned char)path[extension + 1] ) &&
		'p' == tolower( (unsigned char)path[extension + 2] ) &&
		'4' == path[extension + 3];
}

static bool
BuildSimulatorControlScreenRecordingPayload(
	const std::vector< std::string >& arguments, std::string& payload,
	std::string& error )
{
	if ( arguments.empty() || arguments.front().empty() )
	{
		error = "start-screen-recording expects an MP4 output path";
		return false;
	}
	if ( std::string::npos != arguments.front().find_first_of( "\r\n" ) ||
		! HasSimulatorControlMp4Extension( arguments.front() ) )
	{
		error = "start-screen-recording path must end in .mp4 and cannot contain a newline";
		return false;
	}

	std::string absolutePath;
	if ( ! MakeSimulatorControlAbsoluteOutputPath(
		arguments.front(), absolutePath ) )
	{
		error = "start-screen-recording expects a valid output path";
		return false;
	}

	int framesPerSecond = 60;
	double resolutionScale = 1.0;
	std::string captureResolutionType( "best" );
	int outputWidth = 0;
	int outputHeight = 0;
	bool includeAudio = true;
	bool showsCursor = false;
	bool reuseCaptureStream = false;
	bool overwrite = false;
	bool hasFramesPerSecond = false;
	bool hasResolutionScale = false;
	bool hasCaptureResolutionType = false;
	bool hasOutputWidth = false;
	bool hasOutputHeight = false;
	bool hasNoAudio = false;
	bool hasShowCursor = false;
	bool hasReuseCaptureStream = false;
	bool hasOverwrite = false;
	for ( size_t index = 1; index < arguments.size(); index++ )
	{
		const std::string& argument = arguments[index];
		if ( "--fps" == argument )
		{
			if ( hasFramesPerSecond || index + 1 >= arguments.size() )
			{
				error = "start-screen-recording --fps expects one integer from 1 through 240";
				return false;
			}
			const std::string& value = arguments[++index];
			std::istringstream stream( value );
			std::string extra;
			if ( ! ( stream >> framesPerSecond ) || stream >> extra ||
				framesPerSecond < 1 || framesPerSecond > 240 )
			{
				error = "start-screen-recording --fps expects one integer from 1 through 240";
				return false;
			}
			hasFramesPerSecond = true;
		}
		else if ( "--resolution-scale" == argument )
		{
			if ( hasResolutionScale || index + 1 >= arguments.size() )
			{
				error = "start-screen-recording --resolution-scale expects a number greater than 0 and no greater than 1";
				return false;
			}
			const std::string& value = arguments[++index];
			std::istringstream stream( value );
			std::string extra;
			if ( ! ( stream >> resolutionScale ) || stream >> extra ||
				! IsFiniteNumber( (lua_Number)resolutionScale ) ||
				resolutionScale <= 0.0 || resolutionScale > 1.0 )
			{
				error = "start-screen-recording --resolution-scale expects a number greater than 0 and no greater than 1";
				return false;
			}
			hasResolutionScale = true;
		}
		else if ( "--capture-resolution-type" == argument )
		{
			if ( hasCaptureResolutionType || index + 1 >= arguments.size() )
			{
				error = "start-screen-recording --capture-resolution-type expects automatic, best, or nominal";
				return false;
			}
			captureResolutionType = arguments[++index];
			if ( "automatic" != captureResolutionType &&
				"best" != captureResolutionType &&
				"nominal" != captureResolutionType )
			{
				error = "start-screen-recording --capture-resolution-type expects automatic, best, or nominal";
				return false;
			}
			hasCaptureResolutionType = true;
		}
		else if ( "--output-width" == argument || "--output-height" == argument )
		{
			bool& hasValue = "--output-width" == argument ? hasOutputWidth : hasOutputHeight;
			int& result = "--output-width" == argument ? outputWidth : outputHeight;
			if ( hasValue || index + 1 >= arguments.size() )
			{
				error = argument + " expects one even integer from 2 through 16384";
				return false;
			}
			const std::string& value = arguments[++index];
			std::istringstream stream( value );
			std::string extra;
			if ( ! ( stream >> result ) || stream >> extra ||
				result < 2 || result > 16384 || 0 != result % 2 )
			{
				error = argument + " expects one even integer from 2 through 16384";
				return false;
			}
			hasValue = true;
		}
		else if ( "--no-audio" == argument )
		{
			if ( hasNoAudio )
			{
				error = "start-screen-recording --no-audio can only be provided once";
				return false;
			}
			hasNoAudio = true;
			includeAudio = false;
		}
		else if ( "--show-cursor" == argument )
		{
			if ( hasShowCursor )
			{
				error = "start-screen-recording --show-cursor can only be provided once";
				return false;
			}
			hasShowCursor = true;
			showsCursor = true;
		}
		else if ( "--reuse-capture-stream" == argument )
		{
			if ( hasReuseCaptureStream )
			{
				error = "start-screen-recording --reuse-capture-stream can only be provided once";
				return false;
			}
			hasReuseCaptureStream = true;
			reuseCaptureStream = true;
		}
		else if ( "--overwrite" == argument )
		{
			if ( hasOverwrite )
			{
				error = "start-screen-recording --overwrite can only be provided once";
				return false;
			}
			hasOverwrite = true;
			overwrite = true;
		}
		else
		{
			error = "start-screen-recording options are --fps, --resolution-scale, --capture-resolution-type, --output-width, --output-height, --no-audio, --show-cursor, --reuse-capture-stream, and --overwrite";
			return false;
		}
	}
	if ( hasOutputWidth != hasOutputHeight )
	{
		error = "start-screen-recording --output-width and --output-height must be provided together";
		return false;
	}

	char options[192];
	snprintf(
		options, sizeof( options ), "\n%d %.17g %s %d %d %d %d %d %d",
		framesPerSecond, resolutionScale, captureResolutionType.c_str(),
		outputWidth, outputHeight, includeAudio ? 1 : 0,
		showsCursor ? 1 : 0, reuseCaptureStream ? 1 : 0,
		overwrite ? 1 : 0 );
	payload.assign( absolutePath );
	payload.append( options );
	return true;
}

static bool
ParseSimulatorControlScreenRecordingPayload(
	const std::string& payload,
	MSimulatorHost::ScreenRecordingOptions& options, std::string& error )
{
	size_t lineEnd = payload.find( '\n' );
	if ( std::string::npos == lineEnd )
	{
		error = "start-screen-recording received an invalid payload";
		return false;
	}
	options.path.assign( payload, 0, lineEnd );
	int includeAudio = -1;
	int showsCursor = -1;
	int reuseCaptureStream = -1;
	int overwrite = -1;
	std::string extra;
	std::istringstream stream( payload.substr( lineEnd + 1 ) );
	if ( options.path.empty() || ! HasSimulatorControlMp4Extension( options.path ) ||
		! ( stream >> options.framesPerSecond >> options.resolutionScale >>
			options.captureResolutionType >> options.outputWidth >> options.outputHeight >>
			includeAudio >> showsCursor >> reuseCaptureStream >> overwrite ) || stream >> extra ||
		options.framesPerSecond < 1 || options.framesPerSecond > 240 ||
		! IsFiniteNumber( (lua_Number)options.resolutionScale ) ||
		options.resolutionScale <= 0.0 || options.resolutionScale > 1.0 ||
		( "automatic" != options.captureResolutionType &&
			"best" != options.captureResolutionType &&
			"nominal" != options.captureResolutionType ) ||
		( ( 0 == options.outputWidth ) != ( 0 == options.outputHeight ) ) ||
		( 0 != options.outputWidth &&
			( options.outputWidth < 2 || options.outputWidth > 16384 ||
			options.outputHeight < 2 || options.outputHeight > 16384 ||
			0 != options.outputWidth % 2 || 0 != options.outputHeight % 2 ) ) ||
		( 0 != includeAudio && 1 != includeAudio ) ||
		( 0 != showsCursor && 1 != showsCursor ) ||
		( 0 != reuseCaptureStream && 1 != reuseCaptureStream ) ||
		( 0 != overwrite && 1 != overwrite ) )
	{
		error = "start-screen-recording received an invalid payload";
		return false;
	}
	options.includeAudio = 0 != includeAudio;
	options.showsCursor = 0 != showsCursor;
	options.reuseCaptureStream = 0 != reuseCaptureStream;
	options.overwrite = 0 != overwrite;
	return true;
}

static const char *
SimulatorControlScreenRecordingStateName(
	MSimulatorHost::ScreenRecordingState state )
{
	switch ( state )
	{
		case MSimulatorHost::kScreenRecordingIdle: return "idle";
		case MSimulatorHost::kScreenRecordingStarting: return "starting";
		case MSimulatorHost::kScreenRecordingRecording: return "recording";
		case MSimulatorHost::kScreenRecordingStopping: return "stopping";
		case MSimulatorHost::kScreenRecordingUnavailable: break;
	}
	return "unavailable";
}

static bool
ParseSimulatorControlUnsignedLong(
	const std::string& value, const char *key, unsigned long& result )
{
	size_t offset = value.find( key );
	if ( std::string::npos == offset )
	{
		return false;
	}
	offset += strlen( key );
	while ( offset < value.length() && isspace( (unsigned char)value[offset] ) )
	{
		offset++;
	}
	if ( offset >= value.length() ||
		! isdigit( (unsigned char)value[offset] ) )
	{
		return false;
	}

	char *end = NULL;
	errno = 0;
	result = strtoul( value.c_str() + offset, & end, 10 );
	return ERANGE != errno && end && end != value.c_str() + offset;
}

static bool
ParseSimulatorControlJsonString(
	const std::string& value, const char *key, std::string& result )
{
	size_t offset = value.find( key );
	if ( std::string::npos == offset )
	{
		return false;
	}
	offset += strlen( key );
	while ( offset < value.length() && isspace( (unsigned char)value[offset] ) )
	{
		offset++;
	}
	if ( offset >= value.length() || '"' != value[offset++] )
	{
		return false;
	}

	size_t start = offset;
	while ( offset < value.length() && '"' != value[offset] )
	{
		unsigned char character = (unsigned char)value[offset];
		if ( '-' != character && '_' != character && ! isalnum( character ) )
		{
			return false;
		}
		offset++;
	}
	if ( offset >= value.length() || start == offset || offset - start > 127 )
	{
		return false;
	}
	result.assign( value, start, offset - start );
	return true;
}

static bool
ParseSimulatorControlSession(
	const std::string& value, SimulatorControlSession& result )
{
	unsigned long protocol = 0;
	return ParseSimulatorControlUnsignedLong(
			value, "\"protocol\":", protocol ) &&
		2 == protocol &&
		ParseSimulatorControlUnsignedLong(
			value, "\"generation\":", result.generation ) &&
		ParseSimulatorControlUnsignedLong(
			value, "\"pid\":", result.processId ) &&
		ParseSimulatorControlJsonString(
			value, "\"sessionId\":", result.sessionId );
}

static bool
ReadSimulatorControlSession(
	const std::string& directory, SimulatorControlSession& result )
{
	std::string contents;
	return ReadSimulatorControlFile(
			SimulatorControlPath( directory, "session.json" ), contents ) &&
		ParseSimulatorControlSession( contents, result );
}

static bool
IsSimulatorControlProtocolFilename( const char *filename )
{
	if ( ! filename || ! filename[0] )
	{
		return false;
	}
	if ( 0 == strcmp( filename, "session.json" ) ||
		0 == strcmp( filename, "session.json.tmp" ) ||
		0 == strcmp( filename, "diagnostics.json" ) ||
		0 == strcmp( filename, "diagnostics.json.tmp" ) ||
		0 == strcmp( filename, "screenshot.png" ) ||
		0 == strcmp( filename, "screenshot.png.tmp" ) ||
		0 == strcmp( filename, "request" ) ||
		0 == strcmp( filename, "request.tmp" ) ||
		0 == strcmp( filename, ".simulator-control.lock" ) )
	{
		return true;
	}
	if ( 0 == strncmp(
			filename, ".simulator-control.lock.stale.",
			strlen( ".simulator-control.lock.stale." ) ) ||
		0 == strncmp(
			filename, ".simulator-control.owner.stale.",
			strlen( ".simulator-control.owner.stale." ) ) )
	{
		return true;
	}
	if ( 0 == strncmp(
		filename, "request.processing.", strlen( "request.processing." ) ) )
	{
		const char *generation = filename + strlen( "request.processing." );
		if ( ! generation[0] )
		{
			return false;
		}
		while ( generation[0] )
		{
			if ( ! isdigit( (unsigned char)generation[0] ) )
			{
				return false;
			}
			generation++;
		}
		return true;
	}
	if ( 0 != strncmp( filename, "response.", strlen( "response." ) ) )
	{
		return false;
	}

	const char *identifier = filename + strlen( "response." );
	const char *suffix = strstr( identifier, ".json" );
	if ( ! suffix || suffix == identifier ||
		( suffix[5] && 0 != strcmp( suffix + 5, ".tmp" ) ) )
	{
		return false;
	}
	while ( identifier < suffix )
	{
		if ( '-' != identifier[0] && '_' != identifier[0] &&
			! isalnum( (unsigned char)identifier[0] ) )
		{
			return false;
		}
		identifier++;
	}
	return true;
}

static bool
CleanupSimulatorControlDirectory( const std::string& directory )
{
	bool succeeded = true;
#ifdef Rtt_WIN_ENV
	std::string pattern = SimulatorControlPath( directory, "*" );
	std::wstring widePattern;
	if ( ! ConvertSimulatorControlUtf8ToWide( pattern, widePattern ) )
	{
		return false;
	}
	WIN32_FIND_DATAW information;
	HANDLE findHandle =
		FindFirstFileW( widePattern.c_str(), & information );
	if ( INVALID_HANDLE_VALUE == findHandle )
	{
		return ERROR_FILE_NOT_FOUND == GetLastError();
	}
	do
	{
		std::string filename;
		if ( ConvertSimulatorControlWideToUtf8(
				information.cFileName, filename ) &&
			IsSimulatorControlProtocolFilename( filename.c_str() ) &&
			0 != DeleteSimulatorControlFile(
				SimulatorControlPath(
					directory, filename.c_str() ) ) )
		{
			succeeded = false;
		}
	}
	while ( FindNextFileW( findHandle, & information ) );
	FindClose( findHandle );
#else
	DIR *directoryHandle = opendir( directory.c_str() );
	if ( ! directoryHandle )
	{
		return false;
	}
	struct dirent *entry = NULL;
	while ( NULL != ( entry = readdir( directoryHandle ) ) )
	{
		if ( IsSimulatorControlProtocolFilename( entry->d_name ) &&
			0 != DeleteSimulatorControlFile(
				SimulatorControlPath(
					directory, entry->d_name ) ) )
		{
			succeeded = false;
		}
	}
	closedir( directoryHandle );
#endif
	return succeeded;
}

static bool
PrepareSimulatorControlDirectory(
	const std::string& directory, std::string& error )
{
	std::string ownerPath =
		SimulatorControlPath( directory, ".simulator-control.owner" );
	while ( ! CreateSimulatorControlExclusivePidFile( ownerPath ) )
	{
		if ( EEXIST != errno )
		{
			error = "unable to claim the Simulator control directory";
			return false;
		}

		unsigned long ownerProcessId = 0;
		if ( ReadSimulatorControlFileOwner( ownerPath, ownerProcessId ) )
		{
			if ( IsSimulatorControlProcessAlive( ownerProcessId ) )
			{
				char message[192];
				snprintf(
					message, sizeof( message ),
					"control directory is already owned by live process %lu",
					ownerProcessId );
				error.assign( message );
				return false;
			}
			if ( ! MoveSimulatorControlFileAside( ownerPath ) &&
				SimulatorControlFileExists( ownerPath ) )
			{
				error = "unable to remove stale Simulator control ownership";
				return false;
			}
			continue;
		}
		if ( IsSimulatorControlFileStale( ownerPath, 30 ) )
		{
			if ( ! MoveSimulatorControlFileAside( ownerPath ) &&
				SimulatorControlFileExists( ownerPath ) )
			{
				error = "unable to remove stale Simulator control ownership";
				return false;
			}
			continue;
		}

		error = "control directory has an unreadable active ownership file";
		return false;
	}

	std::string existingSession;
	if ( ReadSimulatorControlFile(
		SimulatorControlPath( directory, "session.json" ), existingSession ) )
	{
		unsigned long ownerProcessId = 0;
		if ( ParseSimulatorControlUnsignedLong(
				existingSession, "\"pid\":", ownerProcessId ) &&
			IsSimulatorControlProcessAlive( ownerProcessId ) )
		{
			char message[192];
			snprintf(
				message, sizeof( message ),
				"control directory is already owned by live process %lu",
				ownerProcessId );
			error.assign( message );
			DeleteSimulatorControlFile( ownerPath );
			return false;
		}
	}

	if ( ! CleanupSimulatorControlDirectory( directory ) )
	{
		error = "unable to clean stale Simulator control files";
		DeleteSimulatorControlFile( ownerPath );
		return false;
	}
	return true;
}

static bool
WaitForSimulatorControlSession(
	const std::string& directory, unsigned long timeoutMilliseconds,
	bool requireNewSession, const std::string& previousSessionId,
	SimulatorControlSession& session, std::string& error )
{
	std::string sessionPath = SimulatorControlPath( directory, "session.json" );
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	bool foundExitedProcess = false;
	bool foundIncompatibleSession = false;
	while ( GetSimulatorControlMilliseconds() < deadline )
	{
		std::string contents;
		if ( ReadSimulatorControlFile( sessionPath, contents ) )
		{
			SimulatorControlSession candidate;
			if ( ParseSimulatorControlSession( contents, candidate ) )
			{
				if ( IsSimulatorControlProcessAlive( candidate.processId ) )
				{
					if ( ! requireNewSession ||
						candidate.sessionId != previousSessionId )
					{
						session = candidate;
						return true;
					}
				}
				else
				{
					foundExitedProcess = true;
				}
			}
			else
			{
				foundIncompatibleSession = true;
			}
		}
		SleepSimulatorControlMilliseconds( 20 );
	}

	if ( requireNewSession )
	{
		error =
			"the Simulator accepted relaunch but its replacement Lua runtime "
			"did not become ready";
	}
	else if ( foundExitedProcess )
	{
		error =
			"the Simulator control session belongs to a process that is no "
			"longer running";
	}
	else if ( foundIncompatibleSession )
	{
		error = "no compatible Simulator control session was found";
	}
	else
	{
		error = "no ready Simulator control session was found";
	}
	return false;
}

static bool
WaitForSimulatorControlDiagnostics(
	const std::string& directory,
	const SimulatorControlSession& session,
	unsigned long timeoutMilliseconds,
	std::string& response, std::string& error )
{
	std::string diagnosticsPath =
		SimulatorControlPath( directory, "diagnostics.json" );
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	while ( GetSimulatorControlMilliseconds() < deadline )
	{
		std::string contents;
		std::string sessionId;
		if ( ReadSimulatorControlFile( diagnosticsPath, contents ) &&
			ParseSimulatorControlJsonString(
				contents, "\"sessionId\":", sessionId ) &&
			sessionId == session.sessionId )
		{
			response = contents;
			return true;
		}
		if ( ! IsSimulatorControlProcessAlive( session.processId ) )
		{
			error =
				"the Simulator exited before diagnostics could be read";
			return false;
		}
		SleepSimulatorControlMilliseconds( 20 );
	}

	error = "the Simulator did not publish diagnostics before the timeout";
	return false;
}

class SimulatorControlClientLock
{
	public:
		SimulatorControlClientLock()
		{
		}

		~SimulatorControlClientLock()
		{
			unsigned long ownerProcessId = 0;
			if ( ! fPath.empty() &&
				ReadSimulatorControlFileOwner( fPath, ownerProcessId ) &&
				ownerProcessId ==
					(unsigned long)GetSimulatorControlProcessId() )
			{
				DeleteSimulatorControlFile( fPath );
			}
		}

		bool Acquire(
			const std::string& directory, unsigned long timeoutMilliseconds,
			std::string& error, const char **errorCode = NULL )
		{
			std::string path;
			if ( ! AcquireSimulatorControlLock(
				directory, timeoutMilliseconds, path, error, errorCode ) )
			{
				return false;
			}
			fPath = path;
			return true;
		}

	private:
		std::string fPath;
};

static bool
PerformSimulatorControlClientRequest(
	const std::string& directory, const std::string& command,
	const std::string& payload, unsigned long timeoutMilliseconds,
	std::string& response, std::string& error, const char **errorCode = NULL )
{
	SimulatorControlSession session;
	if ( ! WaitForSimulatorControlSession(
		directory, timeoutMilliseconds, false, std::string(), session, error ) )
	{
		if ( errorCode )
		{
			*errorCode = "session-unavailable";
		}
		return false;
	}

	if ( "runtime-diagnostics" == command )
	{
		return WaitForSimulatorControlDiagnostics(
			directory, session, timeoutMilliseconds,
			response, error );
	}

	SimulatorControlClientLock lock;
	if ( ! lock.Acquire( directory, timeoutMilliseconds, error, errorCode ) )
	{
		return false;
	}

	SimulatorControlSession lockedSession;
	if ( ! ReadSimulatorControlSession( directory, lockedSession ) ||
		lockedSession.sessionId != session.sessionId ||
		lockedSession.processId != session.processId ||
		! IsSimulatorControlProcessAlive( lockedSession.processId ) )
	{
		error = "the Simulator control session changed before the request was sent";
		if ( errorCode )
		{
			*errorCode = "session-changed";
		}
		return false;
	}

	std::string requestPath = SimulatorControlPath( directory, "request" );
	if ( SimulatorControlFileExists( requestPath ) )
	{
		error = "the Simulator has not consumed an existing control request";
		if ( errorCode )
		{
			*errorCode = "control-busy";
		}
		return false;
	}

	static unsigned long requestCounter = 0;
	char identifier[128];
	snprintf(
		identifier, sizeof( identifier ), "%ld_%llu_%lu",
		GetSimulatorControlProcessId(), GetSimulatorControlMilliseconds(),
		++requestCounter );
	std::string responsePath = SimulatorControlPath(
		directory, ( std::string( "response." ) + identifier + ".json" ).c_str() );
	DeleteSimulatorControlFile( responsePath );

	std::string request( identifier );
	request.push_back( '\n' );
	request.append( session.sessionId );
	request.push_back( '\n' );
	request.append( command );
	request.push_back( '\n' );
	request.append( payload );
	if ( request.length() > kSimulatorControlMaximumRequestSize )
	{
		error = "control request exceeds one megabyte; use execute-lua-file for larger scripts";
		if ( errorCode )
		{
			*errorCode = "request-too-large";
		}
		return false;
	}
	if ( ! WriteSimulatorControlFileAtomically( requestPath, request ) )
	{
		int fileError = errno;
		error = "unable to write the Simulator control request at " + requestPath + ": " + strerror( fileError );
		if ( errorCode )
		{
			*errorCode = ( EACCES == fileError || EPERM == fileError ) ? "permission-denied" : "control-io-error";
		}
		return false;
	}

	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	while ( GetSimulatorControlMilliseconds() < deadline )
	{
		if ( ReadSimulatorControlFile( responsePath, response, kSimulatorControlMaximumJsonResultSize + 1024 ) )
		{
			DeleteSimulatorControlFile( responsePath );
			if ( "relaunch-project" == command &&
				0 == response.compare( 0, 10, "{\"ok\":true" ) )
			{
				SimulatorControlSession replacementSession;
				if ( ! WaitForSimulatorControlSession(
					directory, timeoutMilliseconds, true, session.sessionId,
					replacementSession, error ) )
				{
					return false;
				}
			}
			return true;
		}
		SleepSimulatorControlMilliseconds( 10 );
	}

	DeleteSimulatorControlFile( requestPath );
	error = "the Simulator did not answer the control request before the timeout; execution may already have occurred";
	if ( errorCode )
	{
		*errorCode = "control-timeout";
	}
	return false;
}

static bool
IsSimulatorControlSuccessResponse( const std::string& response )
{
	return 0 == response.compare( 0, 10, "{\"ok\":true" );
}

static std::string
BuildSimulatorControlLogsPayload(
	unsigned long sinceSequence, const std::string& filter )
{
	char sequence[32];
	snprintf( sequence, sizeof( sequence ), "%lu", sinceSequence );
	std::string result( sequence );
	if ( ! filter.empty() )
	{
		result.push_back( '\n' );
		result.append( filter );
	}
	return result;
}

static bool
ParseSimulatorControlLogsResponse(
	const std::string& response, std::string& sessionId,
	unsigned long& latestSequence, unsigned long& lastEntrySequence,
	bool& hasEntries, bool& hasMore )
{
	if ( ! IsSimulatorControlSuccessResponse( response ) ||
		! ParseSimulatorControlJsonString(
			response, "\"sessionId\":", sessionId ) ||
		! ParseSimulatorControlUnsignedLong(
			response, "\"latestSequence\":", latestSequence ) )
	{
		return false;
	}

	size_t entriesStart = response.find( "\"entries\":[" );
	if ( std::string::npos == entriesStart )
	{
		return false;
	}
	entriesStart += strlen( "\"entries\":[" );
	size_t entriesEnd = response.find( "],\"filter\":", entriesStart );
	if ( std::string::npos == entriesEnd )
	{
		return false;
	}

	hasEntries = entriesStart < entriesEnd;
	lastEntrySequence = 0;
	size_t offset = entriesStart;
	while ( offset < entriesEnd )
	{
		offset = response.find( "\"sequence\":", offset );
		if ( std::string::npos == offset || offset >= entriesEnd )
		{
			break;
		}
		unsigned long sequence = 0;
		if ( ! ParseSimulatorControlUnsignedLong(
			response.substr( offset, entriesEnd - offset ),
			"\"sequence\":", sequence ) )
		{
			return false;
		}
		lastEntrySequence = sequence;
		offset += strlen( "\"sequence\":" );
	}
	if ( hasEntries && 0 == lastEntrySequence )
	{
		return false;
	}

	size_t hasMoreOffset = response.find( "\"hasMore\":", entriesEnd );
	if ( std::string::npos == hasMoreOffset )
	{
		return false;
	}
	hasMoreOffset += strlen( "\"hasMore\":" );
	hasMore = 0 == response.compare( hasMoreOffset, 4, "true" );
	return hasMore || 0 == response.compare( hasMoreOffset, 5, "false" );
}

static bool
PerformSimulatorControlClientFollowLogs(
	const std::string& directory, unsigned long sinceSequence,
	const std::string& filter, unsigned long timeoutMilliseconds,
	std::string& response, std::string& error )
{
	std::string sessionId;
	for ( ;; )
	{
		std::string candidate;
		if ( ! PerformSimulatorControlClientRequest(
			directory, "runtime-logs",
			BuildSimulatorControlLogsPayload( sinceSequence, filter ),
			timeoutMilliseconds, candidate, error ) )
		{
			return false;
		}
		if ( ! IsSimulatorControlSuccessResponse( candidate ) )
		{
			response = candidate;
			return true;
		}

		std::string candidateSessionId;
		unsigned long latestSequence = 0;
		unsigned long lastEntrySequence = 0;
		bool hasEntries = false;
		bool hasMore = false;
		if ( ! ParseSimulatorControlLogsResponse(
			candidate, candidateSessionId, latestSequence,
			lastEntrySequence, hasEntries, hasMore ) )
		{
			error = "the Simulator returned an invalid runtime-logs response";
			return false;
		}
		if ( ! sessionId.empty() && sessionId != candidateSessionId )
		{
			sessionId = candidateSessionId;
			sinceSequence = 0;
			continue;
		}
		sessionId = candidateSessionId;

		if ( hasEntries )
		{
			if ( candidate.length() != fwrite(
					candidate.data(), 1, candidate.length(), stdout ) ||
				EOF == fputc( '\n', stdout ) || 0 != fflush( stdout ) )
			{
				error = "unable to write followed logs to standard output";
				return false;
			}
		}
		sinceSequence = hasMore && hasEntries ?
			lastEntrySequence : latestSequence;
		SleepSimulatorControlMilliseconds( 100 );
	}
}

static std::string
BuildSimulatorControlFailureWithSnapshot(
	const std::string& directory, const std::string& failureResponse )
{
	std::string snapshot;
	std::string snapshotError;
	std::string snapshotPath =
		SimulatorControlPath( directory, "automation-failure.png" );
	bool captured = PerformSimulatorControlClientRequest(
		directory, "debug-snapshot", snapshotPath, 3000,
		snapshot, snapshotError );

	std::string result(
		"{\"ok\":false,\"error\":\"automation command failed\","
		"\"result\":{\"failure\":" );
	result.append( failureResponse );
	result.append( ",\"artifacts\":" );
	if ( captured && IsSimulatorControlSuccessResponse( snapshot ) )
	{
		result.append( snapshot );
	}
	else
	{
		result.append( "null,\"artifactError\":" );
		AppendSimulatorControlJsonString(
			result, captured ? "snapshot command failed" : snapshotError );
	}
	result.append( "}}" );
	return result;
}

static bool
PerformSimulatorControlClientWait(
	const std::string& directory, const std::string& command,
	const std::string& payload, unsigned long timeoutMilliseconds,
	std::string& response, std::string& error )
{
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	while ( GetSimulatorControlMilliseconds() < deadline )
	{
		unsigned long long now = GetSimulatorControlMilliseconds();
		unsigned long remaining = (unsigned long)( deadline - now );
		std::string candidate;
		if ( ! PerformSimulatorControlClientRequest(
			directory, command, payload, remaining,
			candidate, error ) )
		{
			return false;
		}
		if ( "condition" == command || "log-search" == command ||
			"node-ready" == command )
		{
			if ( ! IsSimulatorControlSuccessResponse( candidate ) ||
				std::string::npos != candidate.find( "\"satisfied\":true" ) )
			{
				response = candidate;
				return true;
			}
		}
		else if ( IsSimulatorControlSuccessResponse( candidate ) )
		{
			response = candidate;
			return true;
		}
		SleepSimulatorControlMilliseconds( 10 );
	}

	response.assign( "{\"ok\":false,\"error\":" );
	AppendSimulatorControlJsonString(
		response,
		"condition was not satisfied before the client timeout" );
	response.push_back( '}' );
	return true;
}

static bool
PerformSimulatorControlClientWaitForDebuggerPause(
	const std::string& directory, unsigned long afterSequence,
	unsigned long timeoutMilliseconds, std::string& response,
	std::string& error )
{
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	while ( GetSimulatorControlMilliseconds() < deadline )
	{
		unsigned long long now = GetSimulatorControlMilliseconds();
		unsigned long remaining = (unsigned long)( deadline - now );
		std::string candidate;
		if ( ! PerformSimulatorControlClientRequest(
			directory, "debugger-status", "", remaining,
			candidate, error ) )
		{
			return false;
		}
		unsigned long sequence = 0;
		if ( IsSimulatorControlSuccessResponse( candidate ) &&
			std::string::npos != candidate.find( "\"paused\":true" ) &&
			ParseSimulatorControlUnsignedLong(
				candidate, "\"pauseSequence\":", sequence ) &&
			sequence > afterSequence )
		{
			response = candidate;
			return true;
		}
		SleepSimulatorControlMilliseconds( 10 );
	}
	error = "Lua execution did not become debug-paused before the timeout";
	return false;
}

static bool
PerformSimulatorControlClientDebuggerStep(
	const std::string& directory, const std::string& command,
	unsigned long timeoutMilliseconds, std::string& response,
	std::string& error )
{
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	std::string scheduledResponse;
	if ( ! PerformSimulatorControlClientRequest(
		directory, command, "", timeoutMilliseconds,
		scheduledResponse, error ) )
	{
		return false;
	}
	if ( ! IsSimulatorControlSuccessResponse( scheduledResponse ) )
	{
		response = scheduledResponse;
		return true;
	}
	unsigned long pauseSequence = 0;
	if ( ! ParseSimulatorControlUnsignedLong(
		scheduledResponse, "\"pauseSequence\":", pauseSequence ) )
	{
		error = "the Simulator returned an invalid debugger step response";
		return false;
	}
	unsigned long long now = GetSimulatorControlMilliseconds();
	if ( now >= deadline )
	{
		error = "Lua execution did not complete the debugger step before the timeout";
		return false;
	}
	return PerformSimulatorControlClientWaitForDebuggerPause(
		directory, pauseSequence, (unsigned long)( deadline - now ),
		response, error );
}

static bool
PerformSimulatorControlClientStep(
	const std::string& directory, const std::string& payload,
	unsigned long timeoutMilliseconds, std::string& response,
	std::string& error )
{
	unsigned long long deadline =
		GetSimulatorControlMilliseconds() + timeoutMilliseconds;
	std::string scheduledResponse;
	if ( ! PerformSimulatorControlClientRequest(
		directory, "step-runtime-frame", payload, timeoutMilliseconds,
		scheduledResponse, error ) )
	{
		return false;
	}
	if ( ! IsSimulatorControlSuccessResponse( scheduledResponse ) )
	{
		response = scheduledResponse;
		return true;
	}

	unsigned long long now = GetSimulatorControlMilliseconds();
	if ( now >= deadline )
	{
		error =
			"the runtime did not complete the requested frame steps before the timeout";
		return false;
	}
	return PerformSimulatorControlClientRequest(
		directory, "runtime-status", "",
		(unsigned long)( deadline - now ), response, error );
}

static void
TrimSimulatorControlScenarioLine( std::string& line )
{
	size_t start = 0;
	while ( start < line.length() &&
		isspace( (unsigned char)line[start] ) )
	{
		start++;
	}
	size_t end = line.length();
	while ( end > start && isspace( (unsigned char)line[end - 1] ) )
	{
		end--;
	}
	line.assign( line, start, end - start );
}

static bool
IsSimulatorControlScenarioCommand( const std::string& command )
{
	static const char *commands[] =
	{
		"runtime-status", "runtime-metrics", "runtime-diagnostics", "runtime-logs",
		"pause-runtime", "resume-runtime", "step-runtime-frame",
		"add-lua-breakpoint", "remove-lua-breakpoint",
		"list-lua-breakpoints", "debugger-status",
		"wait-for-debugger-pause", "debugger-stack",
		"inspect-debugger-frame", "evaluate-debugger-frame",
		"continue-debugger",
		"step-into", "step-over", "step-out",
		"capture-screenshot", "debug-snapshot", "start-screen-recording",
		"stop-screen-recording", "screen-recording-status",
		"display-object-tree",
		"find-display-object", "hit-test-display-objects",
		"tap-display-object", "swipe-display-object", "tap-screen",
		"send-touch-event", "swipe-screen", "send-mouse-event",
		"press-back-button", "send-key-event", "type-text",
		"send-scroll-event", "controller", "wait-for-condition",
		"wait-for-display-object", "wait-for-log", "assert-condition",
		"evaluate-lua", "execute-lua", "execute-lua-file",
		"inspect-lua-value", "relaunch-project", "quit-simulator", NULL
	};
	for ( int index = 0; commands[index]; index++ )
	{
		if ( command == commands[index] )
		{
			return true;
		}
	}
	return false;
}

static bool
PerformSimulatorControlClientScenario(
	const std::string& directory, const std::string& path,
	unsigned long timeoutMilliseconds, std::string& response,
	std::string& error )
{
	std::string contents;
	if ( ! ReadSimulatorControlFile(
		path, contents, kSimulatorControlMaximumRequestSize ) )
	{
		error = "unable to read scenario or scenario exceeded one megabyte";
		return false;
	}

	std::istringstream stream( contents );
	std::string line;
	std::string serializedSteps;
	int lineNumber = 0;
	int stepCount = 0;
	while ( std::getline( stream, line ) )
	{
		lineNumber++;
		TrimSimulatorControlScenarioLine( line );
		if ( line.empty() || 0 == line.compare( 0, 2, "--" ) )
		{
			continue;
		}
		if ( stepCount >= kSimulatorControlMaximumEntries )
		{
			error = "scenario cannot contain more than 100 commands";
			return false;
		}

		size_t separator = line.find_first_of( " \t" );
		std::string command = std::string::npos == separator ?
			line : line.substr( 0, separator );
		std::string payload = std::string::npos == separator ?
			std::string() : line.substr( separator + 1 );
		TrimSimulatorControlScenarioLine( payload );
		if ( ! IsSimulatorControlScenarioCommand( command ) )
		{
			char message[256];
			snprintf(
				message, sizeof( message ),
				"scenario line %d contains unknown command '%s'",
				lineNumber, command.c_str() );
			error.assign( message );
			return false;
		}
		if ( ( "runtime-logs" == command ||
			"display-object-tree" == command ) &&
			payload.empty() )
		{
			payload.assign( "0" );
		}
		else if ( "step-runtime-frame" == command && payload.empty() )
		{
			payload.assign( "1" );
		}
		else if ( "add-lua-breakpoint" == command )
		{
			const std::string conditionOption( " --condition " );
			const std::string hitCountOption( " --hit-count " );
			size_t conditionPosition = payload.find( conditionOption );
			size_t hitCountPosition = payload.find( hitCountOption );
			size_t optionsPosition = Min(
				std::string::npos == conditionPosition ? payload.length() :
					conditionPosition,
				std::string::npos == hitCountPosition ? payload.length() :
					hitCountPosition );
			std::string condition;
			if ( std::string::npos != conditionPosition )
			{
				size_t start = conditionPosition + conditionOption.length();
				size_t end = std::string::npos != hitCountPosition &&
					hitCountPosition > conditionPosition ?
					hitCountPosition : payload.length();
				condition = payload.substr( start, end - start );
				TrimSimulatorControlScenarioLine( condition );
			}
			std::string hitCountText( "0" );
			if ( std::string::npos != hitCountPosition )
			{
				size_t start = hitCountPosition + hitCountOption.length();
				size_t end = std::string::npos != conditionPosition &&
					conditionPosition > hitCountPosition ?
					conditionPosition : payload.length();
				hitCountText = payload.substr( start, end - start );
				TrimSimulatorControlScenarioLine( hitCountText );
			}
			std::string breakpoint = payload.substr( 0, optionsPosition );
			TrimSimulatorControlScenarioLine( breakpoint );
			size_t lineSeparator = breakpoint.find_last_of( " \t" );
			if ( std::string::npos == lineSeparator )
			{
				error = "scenario add-lua-breakpoint expects a source path and line, followed by optional --condition and --hit-count values";
				return false;
			}
			std::string sourcePath = breakpoint.substr( 0, lineSeparator );
			std::string lineText = breakpoint.substr( lineSeparator + 1 );
			TrimSimulatorControlScenarioLine( sourcePath );
			TrimSimulatorControlScenarioLine( lineText );
			std::string absoluteSourcePath;
			if ( sourcePath.empty() || lineText.empty() ||
				( std::string::npos != conditionPosition && condition.empty() ) ||
				( std::string::npos != hitCountPosition && hitCountText.empty() ) ||
				! MakeSimulatorControlAbsolutePath(
					sourcePath, absoluteSourcePath ) )
			{
				error = "scenario add-lua-breakpoint expects an existing source path and line";
				return false;
			}
			payload = absoluteSourcePath;
			payload.push_back( '\n' );
			payload.append( lineText );
			payload.push_back( '\n' );
			payload.append( hitCountText );
			payload.push_back( '\n' );
			payload.append( condition );
		}
		else if ( "evaluate-debugger-frame" == command )
		{
			size_t separator = payload.find_first_of( " \t" );
			if ( std::string::npos == separator )
			{
				error = "scenario evaluate-debugger-frame expects a frame level and Lua expression";
				return false;
			}
			std::string frame = payload.substr( 0, separator );
			std::string expression = payload.substr( separator + 1 );
			TrimSimulatorControlScenarioLine( frame );
			TrimSimulatorControlScenarioLine( expression );
			if ( frame.empty() || expression.empty() )
			{
				error = "scenario evaluate-debugger-frame expects a frame level and Lua expression";
				return false;
			}
			payload = frame;
			payload.push_back( '\n' );
			payload.append( expression );
		}
		else if ( "quit-simulator" == command && payload.empty() )
		{
			payload.assign( "0" );
		}
		else if ( "capture-screenshot" == command ||
			"debug-snapshot" == command )
		{
			std::string outputPath = payload.empty() ?
				SimulatorControlPath(
					directory,
					"debug-snapshot" == command ?
						"snapshot.png" : "screenshot.png" ) :
				payload;
			if ( ! MakeSimulatorControlAbsoluteOutputPath(
				outputPath, payload ) )
			{
				error = "scenario screenshot path is invalid";
				return false;
			}
		}
		else if ( "start-screen-recording" == command )
		{
			std::istringstream argumentStream( payload );
			std::vector< std::string > tokens;
			std::string argument;
			while ( argumentStream >> argument )
			{
				tokens.push_back( argument );
			}
			std::vector< std::string > arguments;
			if ( ! tokens.empty() )
			{
				size_t optionIndex = 1;
				while ( optionIndex < tokens.size() &&
					0 != tokens[optionIndex].compare( 0, 2, "--" ) )
				{
					optionIndex++;
				}
				std::string outputPath( tokens.front() );
				for ( size_t index = 1; index < optionIndex; index++ )
				{
					outputPath.push_back( ' ' );
					outputPath.append( tokens[index] );
				}
				arguments.push_back( outputPath );
				arguments.insert(
					arguments.end(), tokens.begin() + optionIndex,
					tokens.end() );
			}
			if ( ! BuildSimulatorControlScreenRecordingPayload(
				arguments, payload, error ) )
			{
				return false;
			}
		}

		std::string stepResponse;
		bool performed = false;
		if ( "wait-for-condition" == command )
		{
			performed = PerformSimulatorControlClientWait(
				directory, "condition", payload, timeoutMilliseconds,
				stepResponse, error );
		}
		else if ( "wait-for-display-object" == command )
		{
			performed = PerformSimulatorControlClientWait(
				directory, "node-ready", payload, timeoutMilliseconds,
				stepResponse, error );
		}
		else if ( "wait-for-log" == command )
		{
			performed = PerformSimulatorControlClientWait(
				directory, "log-search", payload, timeoutMilliseconds,
				stepResponse, error );
		}
		else if ( "wait-for-debugger-pause" == command )
		{
			performed = PerformSimulatorControlClientWaitForDebuggerPause(
				directory, 0, timeoutMilliseconds,
				stepResponse, error );
		}
		else if ( "step-into" == command ||
			"step-over" == command || "step-out" == command )
		{
			performed = PerformSimulatorControlClientDebuggerStep(
				directory, command, timeoutMilliseconds,
				stepResponse, error );
		}
		else if ( "step-runtime-frame" == command )
		{
			performed = PerformSimulatorControlClientStep(
				directory, payload, timeoutMilliseconds,
				stepResponse, error );
		}
		else
		{
			performed = PerformSimulatorControlClientRequest(
				directory, command, payload, timeoutMilliseconds,
				stepResponse, error );
		}
		if ( ! performed )
		{
			return false;
		}

		if ( ! serializedSteps.empty() )
		{
			serializedSteps.push_back( ',' );
		}
		char prefix[96];
		snprintf(
			prefix, sizeof( prefix ),
			"{\"line\":%d,\"command\":", lineNumber );
		serializedSteps.append( prefix );
		AppendSimulatorControlJsonString( serializedSteps, command );
		serializedSteps.append( ",\"response\":" );
		serializedSteps.append( stepResponse );
		serializedSteps.push_back( '}' );
		stepCount++;

		if ( ! IsSimulatorControlSuccessResponse( stepResponse ) )
		{
			std::string failureWithArtifacts =
				BuildSimulatorControlFailureWithSnapshot(
					directory, stepResponse );
			response.assign(
				"{\"ok\":false,\"error\":\"scenario step failed\","
				"\"result\":{\"steps\":[" );
			response.append( serializedSteps );
			response.append( "],\"failure\":" );
			response.append( failureWithArtifacts );
			response.append( "}}" );
			return true;
		}
		if ( serializedSteps.length() + 512 >
			kSimulatorControlMaximumResponseSize )
		{
			error = "scenario results exceeded the maximum response size";
			return false;
		}
	}

	response.assign( "{\"ok\":true,\"result\":{\"steps\":[" );
	response.append( serializedSteps );
	char suffix[64];
	snprintf(
		suffix, sizeof( suffix ), "],\"count\":%d}}", stepCount );
	response.append( suffix );
	return true;
}

static bool
IsSimulatorControlArgument( const char *argument, const char *expected )
{
	return argument && 0 == strcmp( argument, expected );
}

static bool
ParseSimulatorControlClientTimeout(
	const char *value, unsigned long& timeoutMilliseconds )
{
	if ( ! value || ! value[0] )
	{
		return false;
	}
	char *end = NULL;
	double seconds = strtod( value, & end );
	if ( ! end || end == value || end[0] ||
		! IsFiniteNumber( (lua_Number)seconds ) ||
		seconds <= 0.0 || seconds > 3600.0 )
	{
		return false;
	}
	timeoutMilliseconds = (unsigned long)( seconds * 1000.0 );
	return timeoutMilliseconds > 0;
}

static std::string
JoinSimulatorControlClientArguments(
	int argc, const char * const argv[], int firstArgument )
{
	std::string result;
	for ( int index = firstArgument; index < argc; index++ )
	{
		if ( index > firstArgument )
		{
			result.push_back( ' ' );
		}
		result.append( argv[index] );
	}
	return result;
}

static void
PrintSimulatorControlClientHelp()
{
	fputs(
		"Usage:\n"
		"  [--timeout SECONDS] COMMAND [ARGUMENTS]\n"
		"\n"
		"Commands:\n"
		"  runtime-status\n"
		"  runtime-metrics\n"
		"  runtime-diagnostics\n"
		"  runtime-logs [--since SEQUENCE] [--filter TEXT] [--follow]\n"
		"  pause-runtime\n"
		"  resume-runtime\n"
		"  step-runtime-frame [COUNT]\n"
		"  add-lua-breakpoint PATH LINE [--condition EXPRESSION] [--hit-count COUNT]\n"
		"  remove-lua-breakpoint ID\n"
		"  list-lua-breakpoints\n"
		"  debugger-status\n"
		"  wait-for-debugger-pause\n"
		"  debugger-stack\n"
		"  inspect-debugger-frame FRAME\n"
		"  evaluate-debugger-frame FRAME [LUA EXPRESSION]\n"
		"  continue-debugger\n"
		"  step-into\n"
		"  step-over\n"
		"  step-out\n"
		"  capture-screenshot [PATH]\n"
		"  debug-snapshot [PATH]\n"
		"  start-screen-recording PATH [--fps FPS] [--resolution-scale SCALE] [--capture-resolution-type TYPE] [--output-width WIDTH --output-height HEIGHT] [--no-audio] [--show-cursor] [--reuse-capture-stream] [--overwrite]\n"
		"  stop-screen-recording\n"
		"  screen-recording-status\n"
		"  display-object-tree [--cursor OFFSET]\n"
		"  find-display-object ID|@HANDLE\n"
		"  hit-test-display-objects X Y\n"
		"  tap-display-object ID|@HANDLE\n"
		"  swipe-display-object ID|@HANDLE DELTA_X DELTA_Y [STEPS]\n"
		"  wait-for-condition [LUA EXPRESSION]\n"
		"  wait-for-display-object ID|@HANDLE\n"
		"  wait-for-log [TEXT]\n"
		"  assert-condition [LUA EXPRESSION]\n"
		"  run-scenario PATH\n"
		"  tap-screen X Y\n"
		"  send-touch-event PHASE X Y [X_START Y_START]\n"
		"  swipe-screen X_START Y_START X_END Y_END [STEPS]\n"
		"  send-mouse-event PHASE X Y [SCROLL_X SCROLL_Y]\n"
		"  press-back-button\n"
		"  send-key-event NAME [down|up|pressed] [--shift] [--alt] [--ctrl] [--command]\n"
		"  type-text [TEXT]\n"
		"  send-scroll-event X Y SCROLL_X SCROLL_Y\n"
		"  controller connect [--id ID] [--profile PROFILE] [--player NUMBER]\n"
		"  controller disconnect [--id ID]\n"
		"  controller send-button-event NAME [down|up|pressed] [--id ID]\n"
		"  controller set-axis NAME VALUE [--id ID]\n"
		"  evaluate-lua [--json] [LUA EXPRESSION]\n"
		"  execute-lua [--json] [LUA STATEMENTS]\n"
		"  execute-lua-file [--json] PATH\n"
		"  inspect-lua-value PATH [--cursor OFFSET]\n"
		"  relaunch-project\n"
		"  quit-simulator [EXIT CODE]\n"
		"\n"
		"type-text, evaluate-debugger-frame, evaluate-lua, execute-lua, wait-for-condition, wait-for-log, and assert-condition read standard input when their payload is omitted.\n",
		stdout );
}

static bool
RunSimulatorControlClientInternal(
	int argc, const char * const argv[], int& exitCode )
{
	int controlArgumentIndex = -1;
	for ( int index = 1; index < argc; index++ )
	{
		if ( IsSimulatorControlArgument( argv[index], "-simulator-control" ) )
		{
			controlArgumentIndex = index;
			break;
		}
	}
	if ( controlArgumentIndex < 0 )
	{
		return false;
	}

	exitCode = 2;
	const char *directoryArgument = NULL;
	for ( int index = 1; index < controlArgumentIndex; index++ )
	{
		if ( IsSimulatorControlArgument(
			argv[index], "-simulator-control-dir" ) )
		{
			if ( index + 1 >= controlArgumentIndex )
			{
				fprintf(
					stderr,
					"Simulator control: -simulator-control-dir expects a path\n" );
				return true;
			}
			directoryArgument = argv[++index];
		}
	}

	int argumentIndex = controlArgumentIndex + 1;
	unsigned long timeoutMilliseconds = 10000;
	if ( argumentIndex < argc &&
		( IsSimulatorControlArgument( argv[argumentIndex], "--timeout" ) ||
			IsSimulatorControlArgument( argv[argumentIndex], "-timeout" ) ) )
	{
		if ( argumentIndex + 1 >= argc ||
			! ParseSimulatorControlClientTimeout(
				argv[argumentIndex + 1], timeoutMilliseconds ) )
		{
			fprintf(
				stderr,
				"Simulator control: --timeout expects more than 0 and at most "
				"3600 seconds\n" );
			return true;
		}
		argumentIndex += 2;
	}

	if ( argumentIndex >= argc ||
		IsSimulatorControlArgument( argv[argumentIndex], "--help" ) ||
		IsSimulatorControlArgument( argv[argumentIndex], "-help" ) ||
		IsSimulatorControlArgument( argv[argumentIndex], "help" ) )
	{
		PrintSimulatorControlClientHelp();
		exitCode = 0;
		return true;
	}

	if ( ! directoryArgument || ! directoryArgument[0] )
	{
		fprintf(
			stderr,
			"Simulator control: -simulator-control-dir is required\n" );
		return true;
	}

	std::string directory;
	if ( ! MakeSimulatorControlAbsolutePath( directoryArgument, directory ) ||
		! SimulatorControlDirectoryExists( directory ) )
	{
		std::string response = SimulatorControlErrorResponse(
			std::string( "control directory does not exist: " ) + directoryArgument,
			"session-unavailable" );
		printf( "%s\n", response.c_str() );
		return true;
	}

	std::string command( argv[argumentIndex++] );
	bool fullJson = ( "evaluate-lua" == command || "execute-lua" == command ||
		"execute-lua-file" == command ) && argumentIndex < argc &&
		0 == strcmp( argv[argumentIndex], "--json" );
	if ( fullJson )
	{
		argumentIndex++;
	}
	std::string payload;
	bool followRuntimeLogs = false;
	unsigned long runtimeLogsSinceSequence = 0;
	std::string runtimeLogsFilter;
	if ( "runtime-status" == command ||
		"runtime-metrics" == command ||
		"runtime-diagnostics" == command ||
		"pause-runtime" == command ||
		"resume-runtime" == command ||
		"list-lua-breakpoints" == command ||
		"debugger-status" == command ||
		"wait-for-debugger-pause" == command ||
		"debugger-stack" == command ||
		"continue-debugger" == command ||
		"step-into" == command ||
		"step-over" == command ||
		"step-out" == command ||
		"relaunch-project" == command ||
		"press-back-button" == command ||
		"stop-screen-recording" == command ||
		"screen-recording-status" == command )
	{
		if ( argumentIndex != argc )
		{
			fprintf(
				stderr, "Simulator control: %s does not accept arguments\n",
				command.c_str() );
			return true;
		}
	}
	else if ( "add-lua-breakpoint" == command )
	{
		if ( argumentIndex + 2 > argc )
		{
			fprintf(
				stderr,
				"Simulator control: add-lua-breakpoint expects a source path and line, followed by optional --condition and --hit-count values\n" );
			return true;
		}
		std::string sourcePath;
		if ( ! MakeSimulatorControlAbsolutePath(
			argv[argumentIndex], sourcePath ) ||
			! SimulatorControlFileExists( sourcePath ) )
		{
			fprintf(
				stderr,
				"Simulator control: add-lua-breakpoint expects an existing source path\n" );
			return true;
		}
		const char *line = argv[argumentIndex + 1];
		for ( const char *character = line; *character; character++ )
		{
			if ( ! isdigit( (unsigned char)*character ) )
			{
				fprintf(
					stderr,
					"Simulator control: Lua breakpoint lines must be positive integers\n" );
				return true;
			}
		}
		errno = 0;
		char *lineEnd = NULL;
		unsigned long lineValue = strtoul( line, & lineEnd, 10 );
		if ( ! line[0] || ERANGE == errno || ! lineEnd || lineEnd[0] ||
			0 == lineValue || lineValue > INT_MAX )
		{
			fprintf(
				stderr,
				"Simulator control: Lua breakpoint lines must be positive integers\n" );
			return true;
		}
		argumentIndex += 2;
		std::string condition;
		unsigned long hitCount = 0;
		bool hasCondition = false;
		bool hasHitCount = false;
		while ( argumentIndex < argc )
		{
			if ( IsSimulatorControlArgument(
				argv[argumentIndex], "--condition" ) )
			{
				if ( hasCondition || argumentIndex + 1 >= argc ||
					! argv[argumentIndex + 1][0] ||
					strlen( argv[argumentIndex + 1] ) >
						kSimulatorControlMaximumStringSize )
				{
					fprintf(
						stderr,
						"Simulator control: --condition expects one non-empty Lua expression of at most 4096 bytes\n" );
					return true;
				}
				condition.assign( argv[argumentIndex + 1] );
				hasCondition = true;
				argumentIndex += 2;
				continue;
			}
			if ( IsSimulatorControlArgument(
				argv[argumentIndex], "--hit-count" ) )
			{
				if ( hasHitCount || argumentIndex + 1 >= argc ||
					! argv[argumentIndex + 1][0] )
				{
					fprintf(
						stderr,
						"Simulator control: --hit-count expects one positive integer\n" );
					return true;
				}
				const char *count = argv[argumentIndex + 1];
				for ( const char *character = count; *character; character++ )
				{
					if ( ! isdigit( (unsigned char)*character ) )
					{
						fprintf(
							stderr,
							"Simulator control: --hit-count expects one positive integer\n" );
						return true;
					}
				}
				errno = 0;
				char *countEnd = NULL;
				hitCount = strtoul( count, & countEnd, 10 );
				if ( ERANGE == errno || ! countEnd || countEnd[0] ||
					0 == hitCount )
				{
					fprintf(
						stderr,
						"Simulator control: --hit-count expects one positive integer\n" );
					return true;
				}
				hasHitCount = true;
				argumentIndex += 2;
				continue;
			}
			fprintf(
				stderr,
				"Simulator control: add-lua-breakpoint accepts only --condition EXPRESSION and --hit-count COUNT after the source path and line\n" );
			return true;
		}
		payload = sourcePath;
		payload.push_back( '\n' );
		payload.append( line );
		char count[48];
		snprintf( count, sizeof( count ), "\n%lu\n", hitCount );
		payload.append( count );
		payload.append( condition );
	}
	else if ( "remove-lua-breakpoint" == command )
	{
		if ( argumentIndex + 1 != argc || ! argv[argumentIndex][0] )
		{
			fprintf(
				stderr,
				"Simulator control: remove-lua-breakpoint expects one positive ID\n" );
			return true;
		}
		payload.assign( argv[argumentIndex] );
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				fprintf(
					stderr,
					"Simulator control: remove-lua-breakpoint expects one positive ID\n" );
				return true;
			}
		}
		if ( "0" == payload )
		{
			fprintf(
				stderr,
				"Simulator control: remove-lua-breakpoint expects one positive ID\n" );
			return true;
		}
	}
	else if ( "inspect-debugger-frame" == command )
	{
		if ( argumentIndex + 1 != argc || ! argv[argumentIndex][0] )
		{
			fprintf(
				stderr,
				"Simulator control: inspect-debugger-frame expects one non-negative frame level\n" );
			return true;
		}
		payload.assign( argv[argumentIndex] );
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				fprintf(
					stderr,
					"Simulator control: inspect-debugger-frame expects one non-negative frame level\n" );
				return true;
			}
		}
	}
	else if ( "evaluate-debugger-frame" == command )
	{
		if ( argumentIndex >= argc || ! argv[argumentIndex][0] )
		{
			fprintf(
				stderr,
				"Simulator control: evaluate-debugger-frame expects a non-negative frame level and Lua expression\n" );
			return true;
		}
		std::string frame( argv[argumentIndex++] );
		for ( size_t index = 0; index < frame.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)frame[index] ) )
			{
				fprintf(
					stderr,
					"Simulator control: evaluate-debugger-frame frame level must be non-negative\n" );
				return true;
			}
		}
		std::string expression;
		if ( argumentIndex < argc )
		{
			expression = JoinSimulatorControlClientArguments(
				argc, argv, argumentIndex );
		}
		else
		{
			expression.assign(
				std::istreambuf_iterator< char >( std::cin ),
				std::istreambuf_iterator< char >() );
		}
		if ( expression.empty() )
		{
			fprintf(
				stderr,
				"Simulator control: evaluate-debugger-frame expects a Lua expression\n" );
			return true;
		}
		payload = frame;
		payload.push_back( '\n' );
		payload.append( expression );
	}
	else if ( "step-runtime-frame" == command )
	{
		payload.assign( argumentIndex < argc ? argv[argumentIndex++] : "1" );
		if ( argumentIndex != argc || payload.empty() )
		{
			fprintf(
				stderr,
				"Simulator control: step-runtime-frame accepts at most one count\n" );
			return true;
		}
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				fprintf(
					stderr,
					"Simulator control: step-runtime-frame count must be an integer from 1 through 1000\n" );
				return true;
			}
		}
		errno = 0;
		char *countEnd = NULL;
		unsigned long count = strtoul( payload.c_str(), & countEnd, 10 );
		if ( ERANGE == errno || ! countEnd || countEnd[0] || 0 == count ||
			count > kSimulatorControlMaximumStepFrames )
		{
			fprintf(
				stderr,
				"Simulator control: step-runtime-frame count must be an integer from 1 through 1000\n" );
			return true;
		}
	}
	else if ( "runtime-logs" == command )
	{
		bool hasSince = false;
		bool hasFilter = false;
		while ( argumentIndex < argc )
		{
			if ( IsSimulatorControlArgument(
				argv[argumentIndex], "--follow" ) )
			{
				if ( followRuntimeLogs )
				{
					fprintf(
						stderr,
						"Simulator control: runtime-logs accepts --follow only once\n" );
					return true;
				}
				followRuntimeLogs = true;
				argumentIndex++;
				continue;
			}
			if ( IsSimulatorControlArgument(
					argv[argumentIndex], "--filter" ) )
			{
				if ( hasFilter || argumentIndex + 1 >= argc ||
					! argv[argumentIndex + 1][0] ||
					strlen( argv[argumentIndex + 1] ) >
						kSimulatorControlMaximumStringSize )
				{
					fprintf(
						stderr,
						"Simulator control: --filter expects one non-empty text value of at most 4096 bytes\n" );
					return true;
				}
				runtimeLogsFilter.assign( argv[argumentIndex + 1] );
				hasFilter = true;
				argumentIndex += 2;
				continue;
			}
			if ( IsSimulatorControlArgument(
					argv[argumentIndex], "--since" ) ||
				IsSimulatorControlArgument(
					argv[argumentIndex], "-since" ) )
			{
				if ( hasSince || argumentIndex + 1 >= argc )
				{
					fprintf(
						stderr,
						"Simulator control: --since expects one non-negative sequence\n" );
					return true;
				}
				const char *sequence = argv[argumentIndex + 1];
				if ( ! sequence[0] )
				{
					fprintf(
						stderr,
						"Simulator control: runtime-logs sequence must be non-negative\n" );
					return true;
				}
				for ( const char *character = sequence; *character; character++ )
				{
					if ( ! isdigit( (unsigned char)*character ) )
					{
						fprintf(
							stderr,
							"Simulator control: runtime-logs sequence must be non-negative\n" );
						return true;
					}
				}
				errno = 0;
				char *sequenceEnd = NULL;
				unsigned long sequenceValue =
					strtoul( sequence, & sequenceEnd, 10 );
				if ( ERANGE == errno || ! sequenceEnd || sequenceEnd[0] )
				{
					fprintf(
						stderr, "Simulator control: runtime-logs sequence is too large\n" );
					return true;
				}
				runtimeLogsSinceSequence = sequenceValue;
				hasSince = true;
				argumentIndex += 2;
				continue;
			}
			fprintf(
				stderr,
				"Simulator control: runtime-logs accepts only --since SEQUENCE, --filter TEXT, and --follow\n" );
			return true;
		}
		payload = BuildSimulatorControlLogsPayload(
			runtimeLogsSinceSequence, runtimeLogsFilter );
	}
	else if ( "capture-screenshot" == command ||
		"debug-snapshot" == command )
	{
		if ( argumentIndex + 1 < argc )
		{
			fprintf(
				stderr,
				"Simulator control: %s accepts at most one path\n",
				command.c_str() );
			return true;
		}
		const char *path = argumentIndex < argc ?
			argv[argumentIndex] :
			( "debug-snapshot" == command ?
				"snapshot.png" : "screenshot.png" );
		std::string outputPath = argumentIndex < argc ?
			std::string( path ) :
			SimulatorControlPath( directory, path );
		if ( ! MakeSimulatorControlAbsoluteOutputPath(
			outputPath, payload ) )
		{
			fprintf(
				stderr,
				"Simulator control: %s expects a valid output path\n",
				command.c_str() );
			return true;
		}
	}
	else if ( "start-screen-recording" == command )
	{
		std::vector< std::string > arguments;
		for ( int index = argumentIndex; index < argc; index++ )
		{
			arguments.push_back( argv[index] );
		}
		std::string error;
		if ( ! BuildSimulatorControlScreenRecordingPayload(
			arguments, payload, error ) )
		{
			fprintf( stderr, "Simulator control: %s\n", error.c_str() );
			return true;
		}
	}
	else if ( "display-object-tree" == command )
	{
		payload.assign( "0" );
		if ( argumentIndex < argc )
		{
			if ( argumentIndex + 2 != argc ||
				( ! IsSimulatorControlArgument(
					argv[argumentIndex], "--cursor" ) &&
					! IsSimulatorControlArgument(
						argv[argumentIndex], "-cursor" ) ) )
			{
				fprintf(
					stderr,
					"Simulator control: display-object-tree accepts only --cursor OFFSET\n" );
				return true;
			}
			const char *cursor = argv[argumentIndex + 1];
			if ( ! cursor[0] )
			{
				fprintf(
					stderr,
					"Simulator control: display-object-tree cursor must be non-negative\n" );
				return true;
			}
			for ( const char *character = cursor; *character; character++ )
			{
				if ( ! isdigit( (unsigned char)*character ) )
				{
					fprintf(
						stderr,
						"Simulator control: display-object-tree cursor must be non-negative\n" );
					return true;
				}
			}
			payload.assign( cursor );
		}
	}
	else if ( "find-display-object" == command ||
		"tap-display-object" == command ||
		"wait-for-display-object" == command )
	{
		if ( argumentIndex + 1 != argc || ! argv[argumentIndex][0] )
		{
			fprintf(
				stderr, "Simulator control: %s expects one automationId or @handle\n",
				command.c_str() );
			return true;
		}
		payload.assign( argv[argumentIndex] );
	}
	else if ( "swipe-display-object" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		SimulatorControlNodeSwipe swipe;
		std::string error;
		if ( ! ParseSimulatorControlNodeSwipePayload(
			payload, swipe, error ) )
		{
			fprintf( stderr, "Simulator control: %s\n", error.c_str() );
			return true;
		}
	}
	else if ( "hit-test-display-objects" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlTapPayload( payload, input, error ) )
		{
			fprintf(
				stderr, "Simulator control: hit-test-display-objects expects finite X and Y screen coordinates\n" );
			return true;
		}
	}
	else if ( "tap-screen" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlTapPayload(
			payload, input, error ) )
		{
			fprintf(
				stderr, "Simulator control: %s\n",
				error.c_str() );
			return true;
		}
	}
	else if ( "send-touch-event" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlTouchPayload(
			payload, input, error ) )
		{
			fprintf( stderr, "Simulator control: %s\n", error.c_str() );
			return true;
		}
	}
	else if ( "swipe-screen" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		SimulatorControlSwipe swipe;
		std::string error;
		if ( ! ParseSimulatorControlSwipePayload(
			payload, swipe, error ) )
		{
			fprintf( stderr, "Simulator control: %s\n", error.c_str() );
			return true;
		}
	}
	else if ( "send-mouse-event" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlMousePayload(
			payload, input, error ) )
		{
			fprintf( stderr, "Simulator control: %s\n", error.c_str() );
			return true;
		}
	}
	else if ( "send-key-event" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlKeyPayload(
			payload, input, error ) )
		{
			fprintf(
				stderr, "Simulator control: %s\n",
				error.c_str() );
			return true;
		}
	}
	else if ( "send-scroll-event" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlScrollPayload(
			payload, input, error ) )
		{
			fprintf(
				stderr, "Simulator control: %s\n",
				error.c_str() );
			return true;
		}
	}
	else if ( "controller" == command )
	{
		payload = JoinSimulatorControlClientArguments(
			argc, argv, argumentIndex );
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlControllerPayload(
			payload, input, error ) )
		{
			fprintf(
				stderr, "Simulator control: %s\n",
				error.c_str() );
			return true;
		}
	}
	else if ( "type-text" == command ||
		"evaluate-lua" == command || "execute-lua" == command ||
		"wait-for-condition" == command ||
		"wait-for-log" == command || "assert-condition" == command )
	{
		if ( argumentIndex < argc )
		{
			payload = JoinSimulatorControlClientArguments(
				argc, argv, argumentIndex );
		}
		else
		{
			payload.assign(
				std::istreambuf_iterator< char >( std::cin ),
				std::istreambuf_iterator< char >() );
		}
		if ( payload.empty() )
		{
			const char *expectation = "Lua code";
			if ( "type-text" == command )
			{
				expectation = "non-empty text";
			}
			else if ( "wait-for-log" == command )
			{
				expectation = "log text";
			}
			fprintf(
				stderr, "Simulator control: %s expects %s\n",
				command.c_str(), expectation );
			return true;
		}
	}
	else if ( "execute-lua-file" == command ||
		"run-scenario" == command )
	{
		if ( argumentIndex + 1 != argc ||
			! MakeSimulatorControlAbsolutePath(
				argv[argumentIndex], payload ) ||
			! SimulatorControlFileExists( payload ) )
		{
			fprintf(
				stderr,
				"Simulator control: %s expects one existing file path\n",
				command.c_str() );
			return true;
		}
	}
	else if ( "inspect-lua-value" == command )
	{
		if ( argumentIndex >= argc )
		{
			fprintf(
				stderr, "Simulator control: inspect-lua-value expects a path\n" );
			return true;
		}
		payload.assign( argv[argumentIndex++] );
		if ( argumentIndex < argc )
		{
			if ( argumentIndex + 2 != argc ||
				( ! IsSimulatorControlArgument(
					argv[argumentIndex], "--cursor" ) &&
					! IsSimulatorControlArgument(
						argv[argumentIndex], "-cursor" ) ) )
			{
				fprintf(
					stderr,
					"Simulator control: inspect-lua-value accepts only --cursor OFFSET\n" );
				return true;
			}
			const char *cursor = argv[argumentIndex + 1];
			if ( ! cursor[0] )
			{
				fprintf(
					stderr,
					"Simulator control: inspect-lua-value cursor must be non-negative\n" );
				return true;
			}
			for ( const char *character = cursor; *character; character++ )
			{
				if ( ! isdigit( (unsigned char)*character ) )
				{
					fprintf(
						stderr,
						"Simulator control: inspect-lua-value cursor must be non-negative\n" );
					return true;
				}
			}
			errno = 0;
			char *cursorEnd = NULL;
			unsigned long cursorValue = strtoul( cursor, & cursorEnd, 10 );
			if ( ERANGE == errno || ! cursorEnd || cursorEnd[0] ||
				cursorValue > INT_MAX )
			{
				fprintf(
					stderr,
					"Simulator control: inspect-lua-value cursor is too large\n" );
				return true;
			}
			payload.append( "\ncursor=" );
			payload.append( cursor );
		}
	}
	else if ( "quit-simulator" == command )
	{
		payload.assign( argumentIndex < argc ? argv[argumentIndex++] : "0" );
		if ( argumentIndex != argc )
		{
			fprintf(
				stderr, "Simulator control: quit-simulator accepts one exit code\n" );
			return true;
		}
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				fprintf(
					stderr,
					"Simulator control: quit-simulator exit code must be 0 through 255\n" );
				return true;
			}
		}
		if ( payload.empty() )
		{
			fprintf(
				stderr,
				"Simulator control: quit-simulator exit code must be 0 through 255\n" );
			return true;
		}
		errno = 0;
		char *exitCodeEnd = NULL;
		unsigned long quitExitCode =
			strtoul( payload.c_str(), & exitCodeEnd, 10 );
		if ( ERANGE == errno || ! exitCodeEnd || exitCodeEnd[0] ||
			quitExitCode > 255 )
		{
			fprintf(
				stderr,
				"Simulator control: quit-simulator exit code must be 0 through 255\n" );
			return true;
		}
	}
	else
	{
		fprintf(
			stderr, "Simulator control: unknown command '%s'\n",
			command.c_str() );
		return true;
	}

	if ( fullJson )
	{
		command.append( "-json" );
	}
	std::string response;
	std::string error;
	const char *errorCode = "control-error";
	bool performed = false;
	if ( "wait-for-condition" == command )
	{
		performed = PerformSimulatorControlClientWait(
			directory, "condition", payload, timeoutMilliseconds,
			response, error );
	}
	else if ( "wait-for-display-object" == command )
	{
		performed = PerformSimulatorControlClientWait(
			directory, "node-ready", payload, timeoutMilliseconds,
			response, error );
	}
	else if ( "wait-for-log" == command )
	{
		performed = PerformSimulatorControlClientWait(
			directory, "log-search", payload, timeoutMilliseconds,
			response, error );
	}
	else if ( "run-scenario" == command )
	{
		performed = PerformSimulatorControlClientScenario(
			directory, payload, timeoutMilliseconds, response, error );
	}
	else if ( "runtime-logs" == command && followRuntimeLogs )
	{
		performed = PerformSimulatorControlClientFollowLogs(
			directory, runtimeLogsSinceSequence, runtimeLogsFilter,
			timeoutMilliseconds, response, error );
	}
	else if ( "wait-for-debugger-pause" == command )
	{
		performed = PerformSimulatorControlClientWaitForDebuggerPause(
			directory, 0, timeoutMilliseconds, response, error );
	}
	else if ( "step-into" == command ||
		"step-over" == command || "step-out" == command )
	{
		performed = PerformSimulatorControlClientDebuggerStep(
			directory, command, timeoutMilliseconds, response, error );
	}
	else if ( "step-runtime-frame" == command )
	{
		performed = PerformSimulatorControlClientStep(
			directory, payload, timeoutMilliseconds, response, error );
	}
	else
	{
		performed = PerformSimulatorControlClientRequest(
			directory, command, payload, timeoutMilliseconds,
			response, error, & errorCode );
	}
	if ( ! performed )
	{
		fprintf( stderr, "Simulator control: %s\n", error.c_str() );
		response = SimulatorControlErrorResponse( error, errorCode );
		printf( "%s\n", response.c_str() );
		return true;
	}
	if ( ( "assert-condition" == command ||
		"wait-for-condition" == command ||
		"wait-for-display-object" == command ||
		"wait-for-log" == command ) &&
		! IsSimulatorControlSuccessResponse( response ) )
	{
		response = BuildSimulatorControlFailureWithSnapshot(
			directory, response );
	}

	fwrite( response.data(), 1, response.length(), stdout );
	fputc( '\n', stdout );
	exitCode =
		0 == response.compare( 0, 10, "{\"ok\":true" ) ? 0 : 1;
	return true;
}

static void
ResumeSimulatorControlDebugger(
	Runtime& runtime, SimulatorControlRuntimeState& state,
	const char *stepMode )
{
	if ( stepMode && stepMode[0] )
	{
		state.debuggerStepMode.assign( stepMode );
		state.debuggerStepThread = state.debuggerThread;
		state.debuggerStepDepth =
			GetSimulatorControlLuaStackDepth( state.debuggerThread );
	}
	else
	{
		state.debuggerStepMode.clear();
		state.debuggerStepThread = NULL;
		state.debuggerStepDepth = 0;
	}
	UpdateSimulatorControlLuaHook( runtime, state );
	if ( state.debuggerOwnsRuntimeClock )
	{
		runtime.EndSimulatorControlPause( false );
	}
	state.debuggerOwnsRuntimeClock = false;
	state.debuggerPaused = false;
	state.debuggerThread = NULL;
}

static std::string
ProcessSimulatorControlRequest(
	Runtime& runtime, SimulatorControlRuntimeState& state,
	const std::string& command, const std::string& payload, int& pendingQuitExitCode )
{
	if ( state.runtimeErrorHalted &&
		! IsSimulatorControlCommandAllowedWhileErrorHalted( command ) )
	{
		return BuildSimulatorControlRuntimeErrorHaltedResponse( state );
	}
	if ( state.debuggerPaused &&
		! IsSimulatorControlCommandAllowedWhileDebuggerPaused( command ) )
	{
		return SimulatorControlErrorResponse(
			"this command is unavailable while Lua execution is debug-paused", "debugger-paused" );
	}

	if ( "debugger-status" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"debugger-status does not accept arguments" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlDebuggerStatus( state ) );
	}

	if ( "list-lua-breakpoints" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"list-lua-breakpoints does not accept arguments" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlBreakpoints() );
	}

	if ( "add-lua-breakpoint" == command )
	{
		size_t pathSeparator = payload.find( '\n' );
		if ( std::string::npos == pathSeparator || 0 == pathSeparator ||
			pathSeparator + 1 >= payload.length() )
		{
			return SimulatorControlErrorResponse(
				"add-lua-breakpoint expects a source path and line" );
		}
		size_t lineSeparator = payload.find( '\n', pathSeparator + 1 );
		size_t countSeparator = std::string::npos == lineSeparator ?
			std::string::npos : payload.find( '\n', lineSeparator + 1 );
		std::string path = payload.substr( 0, pathSeparator );
		std::string lineText = std::string::npos == lineSeparator ?
			payload.substr( pathSeparator + 1 ) :
			payload.substr(
				pathSeparator + 1, lineSeparator - pathSeparator - 1 );
		std::string hitCountText = std::string::npos == lineSeparator ?
			std::string() : ( std::string::npos == countSeparator ?
				payload.substr( lineSeparator + 1 ) :
				payload.substr(
					lineSeparator + 1, countSeparator - lineSeparator - 1 ) );
		std::string condition = std::string::npos == countSeparator ?
			std::string() : payload.substr( countSeparator + 1 );
		StripSimulatorControlCarriageReturn( path );
		StripSimulatorControlCarriageReturn( lineText );
		StripSimulatorControlCarriageReturn( hitCountText );
		if ( path.empty() || path.length() > kSimulatorControlMaximumStringSize )
		{
			return SimulatorControlErrorResponse(
				"Lua breakpoint source paths must contain 1 through 4096 bytes" );
		}
		for ( size_t index = 0; index < lineText.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)lineText[index] ) )
			{
				return SimulatorControlErrorResponse(
					"Lua breakpoint lines must be positive integers" );
			}
		}
		errno = 0;
		char *lineEnd = NULL;
		unsigned long line = strtoul( lineText.c_str(), & lineEnd, 10 );
		if ( lineText.empty() || ERANGE == errno || ! lineEnd || lineEnd[0] ||
			0 == line || line > INT_MAX )
		{
			return SimulatorControlErrorResponse(
				"Lua breakpoint lines must be positive integers" );
		}
		if ( condition.length() > kSimulatorControlMaximumStringSize ||
			condition.find( '\0' ) != std::string::npos )
		{
			return SimulatorControlErrorResponse(
				"Lua breakpoint conditions cannot exceed 4096 bytes or contain a null byte" );
		}
		for ( size_t index = 0; index < hitCountText.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)hitCountText[index] ) )
			{
				return SimulatorControlErrorResponse(
					"Lua breakpoint hit count must be a non-negative integer" );
			}
		}
		errno = 0;
		char *hitCountEnd = NULL;
		unsigned long hitCount = hitCountText.empty() ? 0 :
			strtoul( hitCountText.c_str(), & hitCountEnd, 10 );
		if ( ! hitCountText.empty() &&
			( ERANGE == errno || ! hitCountEnd || hitCountEnd[0] ) )
		{
			return SimulatorControlErrorResponse(
				"Lua breakpoint hit count is too large" );
		}

		std::string comparisonPath =
			NormalizeSimulatorControlDebuggerPath( path.c_str() );
		SimulatorControlDebuggerConfiguration& configuration =
			GetSimulatorControlDebuggerConfiguration();
		for ( std::vector< SimulatorControlBreakpoint >::iterator iterator =
				configuration.breakpoints.begin();
			configuration.breakpoints.end() != iterator; iterator++ )
		{
			if ( iterator->line == (int)line &&
				iterator->comparisonPath == comparisonPath )
			{
				bool updated = iterator->condition != condition ||
					iterator->hitCount != hitCount;
				if ( updated )
				{
					iterator->condition = condition;
					iterator->hitCount = hitCount;
					iterator->hits = 0;
				}
				char existing[96];
				snprintf(
					existing, sizeof( existing ),
					"{\"id\":%lu,\"existing\":true,\"updated\":%s}",
					iterator->id, updated ? "true" : "false" );
				return SimulatorControlSuccessResponse( existing );
			}
		}

		SimulatorControlBreakpoint breakpoint;
		breakpoint.id = ++configuration.nextBreakpointId;
		breakpoint.line = (int)line;
		breakpoint.hitCount = hitCount;
		breakpoint.path = comparisonPath;
		breakpoint.comparisonPath = comparisonPath;
		breakpoint.condition = condition;
		configuration.breakpoints.push_back( breakpoint );
		UpdateSimulatorControlLuaHook( runtime, state );
		char added[64];
		snprintf(
			added, sizeof( added ),
			"{\"id\":%lu,\"existing\":false,\"updated\":false}", breakpoint.id );
		return SimulatorControlSuccessResponse( added );
	}

	if ( "remove-lua-breakpoint" == command )
	{
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				return SimulatorControlErrorResponse(
					"remove-lua-breakpoint expects a positive breakpoint ID" );
			}
		}
		errno = 0;
		char *identifierEnd = NULL;
		unsigned long identifier =
			strtoul( payload.c_str(), & identifierEnd, 10 );
		if ( payload.empty() || ERANGE == errno || ! identifierEnd ||
			identifierEnd[0] || 0 == identifier )
		{
			return SimulatorControlErrorResponse(
				"remove-lua-breakpoint expects a positive breakpoint ID" );
		}
		SimulatorControlDebuggerConfiguration& configuration =
			GetSimulatorControlDebuggerConfiguration();
		for ( std::vector< SimulatorControlBreakpoint >::iterator iterator =
				configuration.breakpoints.begin();
			configuration.breakpoints.end() != iterator; iterator++ )
		{
			if ( iterator->id == identifier )
			{
				configuration.breakpoints.erase( iterator );
				UpdateSimulatorControlLuaHook( runtime, state );
				return SimulatorControlSuccessResponse( "true" );
			}
		}
		return SimulatorControlErrorResponse( "Lua breakpoint ID was not found" );
	}

	if ( "debugger-stack" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"debugger-stack does not accept arguments" );
		}
		if ( ! state.debuggerPaused || ! state.debuggerThread )
		{
			return SimulatorControlErrorResponse(
				"debugger-stack requires debug-paused Lua execution" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlDebuggerStack( state ) );
	}

	if ( "inspect-debugger-frame" == command )
	{
		if ( ! state.debuggerPaused || ! state.debuggerThread )
		{
			return SimulatorControlErrorResponse(
				"inspect-debugger-frame requires debug-paused Lua execution" );
		}
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				return SimulatorControlErrorResponse(
					"inspect-debugger-frame expects a non-negative frame level" );
			}
		}
		errno = 0;
		char *levelEnd = NULL;
		unsigned long level = strtoul( payload.c_str(), & levelEnd, 10 );
		if ( payload.empty() || ERANGE == errno || ! levelEnd || levelEnd[0] ||
			level > INT_MAX )
		{
			return SimulatorControlErrorResponse(
				"inspect-debugger-frame expects a non-negative frame level" );
		}
		std::string frame = BuildSimulatorControlDebuggerFrame(
			state, (int)level );
		if ( frame.empty() )
		{
			return SimulatorControlErrorResponse(
				"debugger frame level was not found" );
		}
		return SimulatorControlSuccessResponse( frame );
	}

	if ( "evaluate-debugger-frame" == command )
	{
		if ( ! state.debuggerPaused || ! state.debuggerThread )
		{
			return SimulatorControlErrorResponse(
				"evaluate-debugger-frame requires debug-paused Lua execution" );
		}
		size_t separator = payload.find( '\n' );
		if ( std::string::npos == separator || 0 == separator ||
			separator + 1 >= payload.length() )
		{
			return SimulatorControlErrorResponse(
				"evaluate-debugger-frame expects a non-negative frame level and Lua expression" );
		}
		std::string levelText = payload.substr( 0, separator );
		std::string expression = payload.substr( separator + 1 );
		StripSimulatorControlCarriageReturn( levelText );
		for ( size_t index = 0; index < levelText.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)levelText[index] ) )
			{
				return SimulatorControlErrorResponse(
					"evaluate-debugger-frame frame level must be non-negative" );
			}
		}
		errno = 0;
		char *levelEnd = NULL;
		unsigned long level = strtoul(
			levelText.c_str(), & levelEnd, 10 );
		if ( levelText.empty() || ERANGE == errno || ! levelEnd ||
			levelEnd[0] || level > INT_MAX )
		{
			return SimulatorControlErrorResponse(
				"evaluate-debugger-frame frame level must be non-negative" );
		}

		std::string values;
		std::string error;
		if ( ! EvaluateSimulatorControlDebuggerExpression(
			state, (int)level, expression, values, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}
		char prefix[128];
		snprintf(
			prefix, sizeof( prefix ),
			"{\"pauseSequence\":%lu,\"frame\":%lu,\"values\":",
			state.debuggerPauseSequence, level );
		std::string result( prefix );
		result.append( values );
		result.push_back( '}' );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "continue-debugger" == command ||
		"step-into" == command ||
		"step-over" == command ||
		"step-out" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				command + " does not accept arguments" );
		}
		if ( ! state.debuggerPaused || ! state.debuggerThread )
		{
			return SimulatorControlErrorResponse(
				command + " requires debug-paused Lua execution" );
		}
		const char *stepMode = NULL;
		if ( "step-into" == command )
		{
			stepMode = "into";
		}
		else if ( "step-over" == command )
		{
			stepMode = "over";
		}
		else if ( "step-out" == command )
		{
			stepMode = "out";
		}
		ResumeSimulatorControlDebugger( runtime, state, stepMode );
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlDebuggerStatus( state ) );
	}

	if ( "pause-runtime" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"pause-runtime does not accept arguments" );
		}
		runtime.BeginSimulatorControlPause();
		state.controlPaused = true;
		state.stepFramesRemaining = 0;
		state.stepStartPending = false;
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlStatus( runtime, state ) );
	}

	if ( "resume-runtime" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"resume-runtime does not accept arguments" );
		}
		runtime.EndSimulatorControlPause( true );
		state.controlPaused = false;
		state.stepFramesRemaining = 0;
		state.stepStartPending = false;
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlStatus( runtime, state ) );
	}

	if ( "step-runtime-frame" == command )
	{
		if ( ! state.controlPaused )
		{
			return SimulatorControlErrorResponse(
				"step-runtime-frame requires a control-paused runtime" );
		}
		if ( runtime.IsSuspended() )
		{
			return SimulatorControlErrorResponse(
				"step-runtime-frame cannot advance a suspended runtime" );
		}
		if ( payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"step-runtime-frame expects a frame count" );
		}
		for ( size_t index = 0; index < payload.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)payload[index] ) )
			{
				return SimulatorControlErrorResponse(
					"step-runtime-frame count must be an integer from 1 through 1000" );
			}
		}
		errno = 0;
		char *countEnd = NULL;
		unsigned long count = strtoul( payload.c_str(), & countEnd, 10 );
		if ( ERANGE == errno || ! countEnd || countEnd[0] || 0 == count ||
			count > kSimulatorControlMaximumStepFrames )
		{
			return SimulatorControlErrorResponse(
				"step-runtime-frame count must be an integer from 1 through 1000" );
		}
		state.stepFramesRemaining = count;
		state.stepStartPending = true;
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlStatus( runtime, state ) );
	}

	if ( "runtime-status" == command )
	{
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlStatus( runtime, state ) );
	}

	if ( "runtime-metrics" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"runtime-metrics does not accept arguments" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlRuntimeMetrics( runtime, state ) );
	}

	if ( "runtime-diagnostics" == command )
	{
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlDiagnostics( state ) );
	}

	if ( "runtime-logs" == command )
	{
		size_t separator = payload.find( '\n' );
		std::string sequenceText = std::string::npos == separator ?
			payload : payload.substr( 0, separator );
		std::string filter = std::string::npos == separator ?
			std::string() : payload.substr( separator + 1 );
		if ( sequenceText.empty() )
		{
			return SimulatorControlErrorResponse(
				"runtime-logs expects a non-negative sequence" );
		}
		if ( filter.length() > kSimulatorControlMaximumStringSize )
		{
			return SimulatorControlErrorResponse(
				"runtime-logs filter cannot exceed 4096 bytes" );
		}
		for ( size_t index = 0; index < sequenceText.length(); index++ )
		{
			if ( ! isdigit( (unsigned char)sequenceText[index] ) )
			{
				return SimulatorControlErrorResponse(
					"runtime-logs expects a non-negative sequence" );
			}
		}
		errno = 0;
		char *sequenceEnd = NULL;
		unsigned long sinceSequence =
			strtoul( sequenceText.c_str(), & sequenceEnd, 10 );
		if ( ERANGE == errno || ! sequenceEnd || sequenceEnd[0] )
		{
			return SimulatorControlErrorResponse(
				"runtime-logs sequence is too large" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlLogs( state, sinceSequence, filter ) );
	}

	if ( "log-search" == command )
	{
		if ( payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"log-search expects non-empty text" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlLogSearch( payload ) );
	}

	if ( "display-object-tree" == command )
	{
		errno = 0;
		char *cursorEnd = NULL;
		unsigned long cursorValue =
			strtoul( payload.c_str(), &cursorEnd, 10 );
		if ( payload.empty() || ERANGE == errno || ! cursorEnd ||
			cursorEnd[0] || cursorValue > INT_MAX )
		{
			return SimulatorControlErrorResponse(
				"display-object-tree cursor must be a non-negative integer" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlDisplayTree( runtime, (int)cursorValue ) );
	}

	if ( "find-display-object" == command )
	{
		std::vector< SimulatorControlDisplayNode > nodes =
			GetSimulatorControlDisplayNodes( runtime );
		std::string error;
		const SimulatorControlDisplayNode *node =
			FindSimulatorControlDisplayNode( nodes, payload, error );
		if ( ! node )
		{
			return SimulatorControlErrorResponse( error );
		}
		std::string result;
		AppendSimulatorControlDisplayNode(
			result, runtime.GetDisplay(), *node );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "node-ready" == command )
	{
		std::vector< SimulatorControlDisplayNode > nodes =
			GetSimulatorControlDisplayNodes( runtime );
		std::string error;
		const SimulatorControlDisplayNode *node =
			FindSimulatorControlDisplayNode( nodes, payload, error );
		if ( ! node )
		{
			std::string result( "{\"satisfied\":false,\"reason\":" );
			AppendSimulatorControlJsonString( result, error );
			result.push_back( '}' );
			return SimulatorControlSuccessResponse( result );
		}
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		GetSimulatorControlScreenBounds(
			runtime.GetDisplay(), * node->object,
			x, y, width, height );
		bool ready = node->visible && node->object->ShouldHitTest() &&
			! node->object->SkipsHitTest() &&
			node->object->CanHitTest() && width > 0.0f && height > 0.0f;
		std::string result( "{\"satisfied\":" );
		result.append( ready ? "true,\"node\":" : "false,\"node\":" );
		AppendSimulatorControlDisplayNode(
			result, runtime.GetDisplay(), *node );
		result.push_back( '}' );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "hit-test-display-objects" == command )
	{
		MSimulatorHost::Input input;
		std::string error;
		if ( ! ParseSimulatorControlTapPayload( payload, input, error ) )
		{
			return SimulatorControlErrorResponse(
				"hit-test-display-objects expects finite X and Y screen coordinates" );
		}
		return SimulatorControlSuccessResponse(
			BuildSimulatorControlHitTest( runtime, input.x, input.y ) );
	}

	if ( "tap-display-object" == command )
	{
		std::vector< SimulatorControlDisplayNode > nodes =
			GetSimulatorControlDisplayNodes( runtime );
		std::string error;
		const SimulatorControlDisplayNode *node =
			FindSimulatorControlDisplayNode( nodes, payload, error );
		if ( ! node )
		{
			return SimulatorControlErrorResponse( error );
		}
		const DisplayObject& object = * node->object;
		if ( ! node->visible || ! object.ShouldHitTest() || object.SkipsHitTest() ||
			! object.CanHitTest() || object.IsOffScreen() )
		{
			return SimulatorControlErrorResponse(
				"the selected display object is not currently hittable" );
		}
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		GetSimulatorControlScreenBounds(
			runtime.GetDisplay(), object, x, y, width, height );
		if ( width <= 0.0f || height <= 0.0f )
		{
			return SimulatorControlErrorResponse(
				"the selected display object has empty screen bounds" );
		}
		const MSimulatorHost *host = runtime.Platform().GetSimulatorHost();
		if ( ! host )
		{
			return SimulatorControlErrorResponse(
				"the Simulator cannot send input to this runtime" );
		}
		MSimulatorHost::Input input;
		input.type = MSimulatorHost::Input::kTouchInput;
		input.phase = MSimulatorHost::Input::kBeganPhase;
		input.x = input.xStart = x + width * 0.5f;
		input.y = input.yStart = y + height * 0.5f;
		if ( ! host->SendInput( input ) )
		{
			return SimulatorControlErrorResponse(
				"the Simulator could not queue the requested tap" );
		}
		input.phase = MSimulatorHost::Input::kEndedPhase;
		if ( ! host->SendInput( input ) )
		{
			return SimulatorControlErrorResponse(
				"the Simulator could not queue the requested tap" );
		}

		std::string result( "{\"node\":" );
		AppendSimulatorControlDisplayNode(
			result, runtime.GetDisplay(), *node );
		char coordinates[128];
		snprintf(
			coordinates, sizeof( coordinates ),
			",\"x\":%.9g,\"y\":%.9g}", input.x, input.y );
		result.append( coordinates );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "swipe-display-object" == command )
	{
		SimulatorControlNodeSwipe nodeSwipe;
		std::string error;
		if ( ! ParseSimulatorControlNodeSwipePayload(
			payload, nodeSwipe, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}
		std::vector< SimulatorControlDisplayNode > nodes =
			GetSimulatorControlDisplayNodes( runtime );
		const SimulatorControlDisplayNode *node =
			FindSimulatorControlDisplayNode(
				nodes, nodeSwipe.selector, error );
		if ( ! node )
		{
			return SimulatorControlErrorResponse( error );
		}
		if ( ! node->visible || node->object->SkipsHitTest() ||
			! node->object->CanHitTest() )
		{
			return SimulatorControlErrorResponse(
				"the selected display object is not currently hittable" );
		}
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		GetSimulatorControlScreenBounds(
			runtime.GetDisplay(), * node->object,
			x, y, width, height );
		if ( width <= 0.0f || height <= 0.0f )
		{
			return SimulatorControlErrorResponse(
				"the selected display object has empty screen bounds" );
		}
		const MSimulatorHost *host = runtime.Platform().GetSimulatorHost();
		if ( ! host )
		{
			return SimulatorControlErrorResponse(
				"the Simulator cannot send input to this runtime" );
		}
		SimulatorControlSwipe swipe;
		swipe.xStart = x + width * 0.5f;
		swipe.yStart = y + height * 0.5f;
		swipe.xEnd = swipe.xStart + nodeSwipe.deltaX;
		swipe.yEnd = swipe.yStart + nodeSwipe.deltaY;
		swipe.steps = nodeSwipe.steps;
		if ( ! SendSimulatorControlSwipe( *host, swipe ) )
		{
			return SimulatorControlErrorResponse(
				"the Simulator could not queue the requested swipe" );
		}
		return SimulatorControlSuccessResponse( "true" );
	}

	if ( "capture-screenshot" == command )
	{
		std::string result;
		std::string error;
		if ( ! CaptureSimulatorControlScreenshot(
			runtime, payload, result, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}
		return SimulatorControlSuccessResponse( result );
	}

	if ( "debug-snapshot" == command )
	{
		std::string result;
		std::string error;
		if ( ! CaptureSimulatorControlSnapshot(
			runtime, state, payload, result, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}
		return SimulatorControlSuccessResponse( result );
	}

	if ( "screen-recording-status" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"screen-recording-status does not accept arguments" );
		}
		const MSimulatorHost *host =
			runtime.Platform().GetSimulatorHost();
		const MSimulatorHost::ScreenRecordingState recordingState = host ?
			host->GetScreenRecordingState() :
			MSimulatorHost::kScreenRecordingUnavailable;
		std::string result( "{\"state\":" );
		AppendSimulatorControlJsonString(
			result,
			SimulatorControlScreenRecordingStateName( recordingState ) );
		result.push_back( '}' );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "start-screen-recording" == command )
	{
		MSimulatorHost::ScreenRecordingOptions options;
		std::string error;
		if ( ! ParseSimulatorControlScreenRecordingPayload(
			payload, options, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}
		const MSimulatorHost *host =
			runtime.Platform().GetSimulatorHost();
		if ( ! host || ! host->StartScreenRecording( options, error ) )
		{
			return SimulatorControlErrorResponse(
				error.empty() ?
					"the Simulator could not start screen recording" : error );
		}

		std::string result( "{\"state\":" );
		AppendSimulatorControlJsonString(
			result,
			SimulatorControlScreenRecordingStateName(
				host->GetScreenRecordingState() ) );
		result.append( ",\"path\":" );
		AppendSimulatorControlJsonString( result, options.path );
		char settings[320];
		snprintf(
			settings, sizeof( settings ),
			",\"fps\":%d,\"resolutionScale\":%.17g,"
			"\"captureResolutionType\":\"%s\",\"outputWidth\":%d,"
			"\"outputHeight\":%d,\"includeAudio\":%s,\"showCursor\":%s,"
			"\"reuseCaptureStream\":%s,\"overwrite\":%s}",
			options.framesPerSecond, options.resolutionScale,
			options.captureResolutionType.c_str(),
			options.outputWidth, options.outputHeight,
			options.includeAudio ? "true" : "false",
			options.showsCursor ? "true" : "false",
			options.reuseCaptureStream ? "true" : "false",
			options.overwrite ? "true" : "false" );
		result.append( settings );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "stop-screen-recording" == command )
	{
		if ( ! payload.empty() )
		{
			return SimulatorControlErrorResponse(
				"stop-screen-recording does not accept arguments" );
		}
		const MSimulatorHost *host =
			runtime.Platform().GetSimulatorHost();
		std::string error;
		if ( ! host || ! host->StopScreenRecording( error ) )
		{
			return SimulatorControlErrorResponse(
				error.empty() ?
					"the Simulator could not stop screen recording" : error );
		}
		std::string result( "{\"state\":" );
		AppendSimulatorControlJsonString(
			result,
			SimulatorControlScreenRecordingStateName(
				host->GetScreenRecordingState() ) );
		result.push_back( '}' );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "tap-screen" == command || "send-touch-event" == command ||
		"swipe-screen" == command || "send-mouse-event" == command ||
		"press-back-button" == command || "send-key-event" == command ||
		"type-text" == command || "send-scroll-event" == command ||
		"controller" == command )
	{
		const MSimulatorHost *host = runtime.Platform().GetSimulatorHost();
		if ( ! host )
		{
			return SimulatorControlErrorResponse(
				"the Simulator cannot send input to this runtime" );
		}

		MSimulatorHost::Input input;
		std::string error;
		if ( "press-back-button" == command )
		{
			if ( ! payload.empty() )
			{
				return SimulatorControlErrorResponse(
					"press-back-button does not accept arguments" );
			}
			input.type = MSimulatorHost::Input::kBackInput;
		}
		else if ( "tap-screen" == command )
		{
			if ( ! ParseSimulatorControlTapPayload(
				payload, input, error ) )
			{
				return SimulatorControlErrorResponse( error );
			}
			if ( ! host->SendInput( input ) )
			{
				return SimulatorControlErrorResponse(
					"the Simulator could not queue the requested tap" );
			}
			input.phase = MSimulatorHost::Input::kEndedPhase;
		}
		else if ( "send-touch-event" == command )
		{
			if ( ! ParseSimulatorControlTouchPayload(
				payload, input, error ) )
			{
				return SimulatorControlErrorResponse( error );
			}
		}
		else if ( "swipe-screen" == command )
		{
			SimulatorControlSwipe swipe;
			if ( ! ParseSimulatorControlSwipePayload(
				payload, swipe, error ) )
			{
				return SimulatorControlErrorResponse( error );
			}
			if ( ! SendSimulatorControlSwipe( *host, swipe ) )
			{
				return SimulatorControlErrorResponse(
					"the Simulator could not queue the requested swipe" );
			}
			return SimulatorControlSuccessResponse( "true" );
		}
		else if ( "send-mouse-event" == command )
		{
			if ( ! ParseSimulatorControlMousePayload(
				payload, input, error ) )
			{
				return SimulatorControlErrorResponse( error );
			}
		}
		else if ( "send-key-event" == command )
		{
			if ( ! ParseSimulatorControlKeyPayload(
				payload, input, error ) )
			{
				return SimulatorControlErrorResponse( error );
			}
			if ( MSimulatorHost::Input::kPressedPhase == input.phase )
			{
				input.phase = MSimulatorHost::Input::kDownPhase;
				if ( ! host->SendInput( input ) )
				{
					return SimulatorControlErrorResponse(
						"the Simulator could not queue the requested key" );
				}
				input.phase = MSimulatorHost::Input::kUpPhase;
			}
		}
		else if ( "type-text" == command )
		{
			if ( payload.empty() )
			{
				return SimulatorControlErrorResponse(
					"type-text expects non-empty text" );
			}
			input.type = MSimulatorHost::Input::kTextInput;
			input.text = payload;
		}
		else if ( "controller" == command )
		{
			if ( ! ParseSimulatorControlControllerPayload(
				payload, input, error ) )
			{
				return SimulatorControlErrorResponse( error );
			}
			if ( MSimulatorHost::Input::kButtonController ==
					input.controllerAction ||
				MSimulatorHost::Input::kAxisController ==
					input.controllerAction )
			{
				MSimulatorHost::Input connectInput;
				connectInput.type = MSimulatorHost::Input::kControllerInput;
				connectInput.controllerAction =
					MSimulatorHost::Input::kConnectController;
				connectInput.controllerId = input.controllerId;
				if ( ! host->SendInput( connectInput ) )
				{
					return SimulatorControlErrorResponse(
						"the Simulator could not queue the virtual controller connection" );
				}
			}
			if ( MSimulatorHost::Input::kButtonController ==
					input.controllerAction &&
				MSimulatorHost::Input::kPressedPhase == input.phase )
			{
				input.phase = MSimulatorHost::Input::kDownPhase;
				if ( ! host->SendInput( input ) )
				{
					return SimulatorControlErrorResponse(
						"the Simulator could not queue the requested controller input" );
				}
				input.phase = MSimulatorHost::Input::kUpPhase;
			}
		}
		else if ( ! ParseSimulatorControlScrollPayload(
			payload, input, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}
		if ( ! host->SendInput( input ) )
		{
			return SimulatorControlErrorResponse(
				"the Simulator could not queue the requested input" );
		}
		return SimulatorControlSuccessResponse( "true" );
	}

	lua_State *L = runtime.VMContext().L();
	if ( ! L )
	{
		return SimulatorControlErrorResponse( "the Lua runtime is not available" );
	}

	if ( "condition" == command || "assert-condition" == command )
	{
		if ( payload.empty() )
		{
			return SimulatorControlErrorResponse(
				command + " expects a Lua expression" );
		}
		std::string values;
		std::string error;
		bool satisfied = false;
		if ( ! ExecuteSimulatorControlChunk(
			L, payload, "=@simulator-control/condition", true, false,
			values, error, &satisfied ) )
		{
			RecordSimulatorControlExecutionDiagnostic(
				runtime, state, error );
			WriteSimulatorControlDiagnostics( state );
			return SimulatorControlErrorResponse( error );
		}
		if ( "assert-condition" == command && ! satisfied )
		{
			std::string response( "{\"ok\":false,\"error\":" );
			AppendSimulatorControlJsonString(
				response, std::string( "assertion failed: " ) + payload );
			response.append( ",\"result\":{\"satisfied\":false,\"values\":" );
			response.append( values );
			response.append( "}}" );
			return response;
		}
		std::string result( "{\"satisfied\":" );
		result.append( satisfied ? "true,\"values\":" : "false,\"values\":" );
		result.append( values );
		result.push_back( '}' );
		return SimulatorControlSuccessResponse( result );
	}

	if ( "evaluate-lua" == command || "execute-lua" == command ||
		"execute-lua-file" == command || "evaluate-lua-json" == command ||
		"execute-lua-json" == command || "execute-lua-file-json" == command )
	{
		if ( payload.empty() )
		{
			return SimulatorControlErrorResponse( command + " expects a non-empty payload" );
		}

		std::string jsonResult;
		std::string error;
		const bool fullJson = command.size() >= 5 && command.substr( command.size() - 5 ) == "-json";
		const bool isExpression = "evaluate-lua" == command || "evaluate-lua-json" == command;
		const bool isFile = "execute-lua-file" == command || "execute-lua-file-json" == command;
		const char *errorCode = "lua-error";
		const char *chunkName =
			isExpression ? "=@simulator-control/eval" : "=@simulator-control/exec";
		if ( ! ExecuteSimulatorControlChunk(
			L, payload, chunkName, isExpression, isFile, jsonResult, error, NULL, fullJson, & errorCode ) )
		{
			RecordSimulatorControlExecutionDiagnostic(
				runtime, state, error );
			WriteSimulatorControlDiagnostics( state );
			return SimulatorControlErrorResponse( error, errorCode );
		}
		return SimulatorControlSuccessResponse( jsonResult, fullJson );
	}

	if ( "inspect-lua-value" == command )
	{
		std::string path;
		std::string error;
		int cursor = 0;
		if ( ! ParseSimulatorControlInspectPayload( payload, path, cursor, error ) )
		{
			return SimulatorControlErrorResponse( error );
		}

		int originalTop = lua_gettop( L );
		if ( ! PushSimulatorControlInspectedValue( L, path, error ) )
		{
			lua_settop( L, originalTop );
			return SimulatorControlErrorResponse( error );
		}
		std::string jsonResult = BuildSimulatorControlInspection( L, -1, cursor );
		lua_settop( L, originalTop );
		return SimulatorControlSuccessResponse( jsonResult );
	}

	if ( "relaunch-project" == command )
	{
		const MSimulatorHost *host = runtime.Platform().GetSimulatorHost();
		if ( ! host || ! host->Relaunch() )
		{
			return SimulatorControlErrorResponse( "the Simulator could not relaunch the project" );
		}
		return SimulatorControlSuccessResponse( "true" );
	}

	if ( "quit-simulator" == command )
	{
		int exitCode = 0;
		if ( ! payload.empty() )
		{
			for ( size_t index = 0; index < payload.length(); index++ )
			{
				if ( ! isdigit( (unsigned char)payload[index] ) )
				{
					return SimulatorControlErrorResponse(
						"quit-simulator exit code must be an integer between 0 and 255" );
				}
			}
			errno = 0;
			char *exitCodeEnd = NULL;
			unsigned long parsedExitCode =
				strtoul( payload.c_str(), & exitCodeEnd, 10 );
			if ( ERANGE == errno || ! exitCodeEnd || exitCodeEnd[0] ||
				parsedExitCode > 255 )
			{
				return SimulatorControlErrorResponse(
					"quit-simulator exit code must be an integer between 0 and 255" );
			}
			exitCode = (int)parsedExitCode;
		}

		const MSimulatorHost *host = runtime.Platform().GetSimulatorHost();
		if ( ! host )
		{
			return SimulatorControlErrorResponse( "the Simulator cannot quit this runtime" );
		}
		pendingQuitExitCode = exitCode;
		return SimulatorControlSuccessResponse( "true" );
	}

	return SimulatorControlErrorResponse( std::string( "unknown control command '" ) + command + "'" );
}

static bool
ProcessSimulatorControlMailbox(
	Runtime& sender, SimulatorControlRuntimeState& state )
{
	std::string requestPath = SimulatorControlPath( state.directory, "request" );
	char processingFilename[96];
	snprintf(
		processingFilename, sizeof( processingFilename ),
		"request.processing.%lu",
		state.generation );
	std::string processingPath =
		SimulatorControlPath( state.directory, processingFilename );
	DeleteSimulatorControlFile( processingPath );
	if ( 0 != MoveSimulatorControlFile(
		requestPath, processingPath, true ) )
	{
		return false;
	}

	std::string contents;
	bool didRead = ReadSimulatorControlFile( processingPath, contents );
	DeleteSimulatorControlFile( processingPath );

	std::string identifier;
	std::string requestSessionId;
	std::string command;
	std::string payload;
	std::string error;
	std::string response;
	int pendingQuitExitCode = -1;
	if ( ! didRead )
	{
		response = SimulatorControlErrorResponse(
			"unable to read control request or request exceeded one megabyte" );
	}
	else if ( ! ParseSimulatorControlRequest(
		contents, identifier, requestSessionId, command, payload, error ) )
	{
		response = SimulatorControlErrorResponse( error );
	}
	else if ( requestSessionId != state.sessionId )
	{
		response = SimulatorControlErrorResponse(
			"control request belongs to a different Simulator session" );
	}
	else
	{
		response = ProcessSimulatorControlRequest(
			sender, state, command, payload, pendingQuitExitCode );
	}

	if ( identifier.empty() )
	{
		identifier = "invalid";
	}
	std::string responsePath = SimulatorControlPath(
		state.directory,
		( std::string( "response." ) + identifier + ".json" ).c_str() );
	WriteSimulatorControlFileAtomically( responsePath, response );

	if ( pendingQuitExitCode >= 0 )
	{
		const MSimulatorHost *host = sender.Platform().GetSimulatorHost();
		if ( host )
		{
			host->Quit( pendingQuitExitCode );
		}
	}
	return true;
}

void
SimulatorControl::SetDirectory( const char *directory )
{
	Rtt_SetLogCallback( NULL, NULL );
	ResetSimulatorControlLogs();
	GetSimulatorControlDebuggerConfiguration() =
		SimulatorControlDebuggerConfiguration();
	std::string& configuredDirectory = GetSimulatorControlDirectory();
	configuredDirectory.clear();
	if ( ! directory || ! directory[0] )
	{
		return;
	}

	std::string value( directory );
	std::string error;
	if ( ! SimulatorControlDirectoryExists( value ) )
	{
		error = "control directory does not exist";
	}
	else if ( PrepareSimulatorControlDirectory( value, error ) )
	{
		configuredDirectory = value;
		GetSimulatorControlOwnerPath() =
			SimulatorControlPath( value, ".simulator-control.owner" );
		static bool registeredExitHandler = false;
		if ( ! registeredExitHandler )
		{
			registeredExitHandler =
				0 == atexit( ReleaseSimulatorControlDirectory );
		}
		Rtt_SetLogCallback( RecordSimulatorControlLog, NULL );
		return;
	}

	fprintf(
		stderr, "Simulator control disabled: %s: %s\n",
		error.c_str(), directory );
}

void
SimulatorControl::InitializeDebugger( Runtime& sender, lua_State *L )
{
	if ( ! L )
	{
		return;
	}
	SimulatorControlRuntimeState *state =
		GetOrCreateSimulatorControlRuntimeState( sender );
	if ( ! state || state->debuggerHookInstalled )
	{
		return;
	}

	String resourceDirectory( sender.GetAllocator() );
	sender.Platform().PathForFile(
		NULL, MPlatform::kResourceDir,
		MPlatform::kDefaultPathFlags, resourceDirectory );
	std::string normalizedResourceDirectory =
		NormalizeSimulatorControlDebuggerPath(
			resourceDirectory.GetString() );
	state->resourceDirectory = CanonicalizeSimulatorControlDebuggerSource(
		*state, normalizedResourceDirectory );
	SimulatorControlDebuggerConfiguration& configuration =
		GetSimulatorControlDebuggerConfiguration();
	for ( std::vector< SimulatorControlBreakpoint >::iterator iterator =
			configuration.breakpoints.begin();
		configuration.breakpoints.end() != iterator; iterator++ )
	{
		iterator->hits = 0;
	}
	UpdateSimulatorControlLuaHook( sender, *state );
	WriteSimulatorControlSession( *state );
	WriteSimulatorControlDiagnostics( *state );
}

static void
UpdateSimulatorControlLuaHook(
	Runtime& runtime, SimulatorControlRuntimeState& state )
{
	lua_State *L = runtime.VMContext().L();
	if ( ! L )
	{
		return;
	}
	bool shouldInstall =
		! GetSimulatorControlDebuggerConfiguration().breakpoints.empty() ||
		! state.debuggerStepMode.empty();
	if ( shouldInstall != state.debuggerHookInstalled )
	{
		lua_sethook(
			L,
			shouldInstall ? SimulatorControlLuaHook : NULL,
			shouldInstall ? LUA_MASKLINE : 0,
			0 );
		state.debuggerHookInstalled = shouldInstall;
	}
}

static void
SimulatorControlLuaHook( lua_State *L, lua_Debug *information )
{
	if ( ! L || ! information || LUA_HOOKLINE != information->event ||
		! LuaContext::HasRuntime( L ) )
	{
		return;
	}
	Runtime *runtime = LuaContext::GetRuntime( L );
	SimulatorControlRuntimeState *state = runtime ?
		GetSimulatorControlRuntimeState( *runtime ) : NULL;
	if ( ! state || state->debuggerPaused || state->runtimeErrorHalted ||
		state->debuggerEvaluating )
	{
		return;
	}

	if ( ! lua_getinfo( L, "nS", information ) ||
		! information->source || '@' != information->source[0] )
	{
		return;
	}
	std::string source = CanonicalizeSimulatorControlDebuggerSource(
		*state,
		NormalizeSimulatorControlDebuggerPath( information->source ) );
	SimulatorControlBreakpoint *breakpoint = FindSimulatorControlBreakpoint(
		*state, source, information->currentline );
	bool breakpointMatched = false;
	std::string conditionError;
	if ( breakpoint )
	{
		bool conditionMatched = true;
		if ( ! breakpoint->condition.empty() )
		{
			std::string ignoredValues;
			lua_State *previousDebuggerThread = state->debuggerThread;
			state->debuggerThread = L;
			if ( ! EvaluateSimulatorControlDebuggerExpression(
				*state, 0, breakpoint->condition,
				ignoredValues, conditionError, & conditionMatched, false ) )
			{
				breakpointMatched = true;
			}
			state->debuggerThread = previousDebuggerThread;
		}
		if ( conditionError.empty() && conditionMatched )
		{
			if ( ULONG_MAX == breakpoint->hits )
			{
				breakpoint->hits = 0;
			}
			breakpoint->hits++;
			breakpointMatched = 0 == breakpoint->hitCount ||
				0 == breakpoint->hits % breakpoint->hitCount;
		}
	}
	unsigned long breakpointId = breakpointMatched ? breakpoint->id : 0;
	bool stepMatched = false;
	if ( ! state->debuggerStepMode.empty() &&
		state->debuggerStepThread == L )
	{
		int depth = GetSimulatorControlLuaStackDepth( L );
		stepMatched = "into" == state->debuggerStepMode ||
			( "over" == state->debuggerStepMode &&
				depth <= state->debuggerStepDepth ) ||
			( "out" == state->debuggerStepMode &&
				depth < state->debuggerStepDepth );
	}
	if ( 0 == breakpointId && ! stepMatched )
	{
		return;
	}

	state->debuggerPaused = true;
	state->debuggerPauseSequence++;
	state->debuggerBreakpointId = breakpointId;
	state->debuggerLine = information->currentline;
	state->debuggerThread = L;
	state->debuggerConditionError = conditionError;
	state->debuggerPauseReason.assign( ! conditionError.empty() ?
		"breakpoint-condition-error" :
		( breakpointId ? "breakpoint" : "step" ) );
	state->debuggerSource = source;
	state->debuggerProjectSource =
		GetSimulatorControlProjectSource( *state, source );
	state->debuggerFunction.assign(
		information->name ? information->name : "" );
	state->debuggerStepMode.clear();
	state->debuggerStepThread = NULL;
	state->debuggerStepDepth = 0;
	state->debuggerOwnsRuntimeClock = ! state->controlPaused;
	if ( state->debuggerOwnsRuntimeClock )
	{
		runtime->BeginSimulatorControlPause();
	}

	while ( state->debuggerPaused )
	{
		if ( ! ProcessSimulatorControlMailbox( *runtime, *state ) )
		{
			SleepSimulatorControlMilliseconds( 10 );
		}
	}
}

void
SimulatorControl::Process( Runtime& sender )
{
	SimulatorControlRuntimeState *statePointer =
		GetOrCreateSimulatorControlRuntimeState( sender );
	if ( ! statePointer )
	{
		return;
	}
	SimulatorControlRuntimeState& state = *statePointer;
	if ( ! state.rendererStatisticsRequested )
	{
		sender.GetDisplay().GetRenderer().SetStatisticsEnabled( true );
		state.rendererStatisticsRequested = true;
	}

	if ( ! state.sessionWritten )
	{
		WriteSimulatorControlSession( state );
	}
	if ( state.sessionWritten && ! state.diagnosticsWritten )
	{
		WriteSimulatorControlDiagnostics( state );
	}
	if ( state.stepFramesRemaining > 0 && ! state.stepStartPending )
	{
		if ( ! sender.IsSuspended() )
		{
			return;
		}
		state.stepFramesRemaining = 0;
	}

	ProcessSimulatorControlMailbox( sender, state );
}

void
SimulatorControl::RecordRuntimeError(
	Runtime& sender, lua_State *L, const char *errorType,
	const char *message, const char *stackTrace )
{
	SimulatorControlRuntimeState *state =
		GetOrCreateSimulatorControlRuntimeState( sender );
	if ( state )
	{
		RecordSimulatorControlDiagnostic(
			sender, *state, errorType, message, stackTrace );
		if ( L )
		{
			bool contextTruncated = false;
			state->diagnosticContext =
				BuildSimulatorControlDiagnosticContext(
					L, contextTruncated );
			state->diagnosticTruncated =
				state->diagnosticTruncated || contextTruncated;
		}
		WriteSimulatorControlSession( *state );
		WriteSimulatorControlDiagnostics( *state );
	}
}

void
SimulatorControl::HaltOnRuntimeError( Runtime& sender )
{
	SimulatorControlRuntimeState *state =
		GetOrCreateSimulatorControlRuntimeState( sender );
	if ( state && ! state->runtimeErrorHalted )
	{
		state->runtimeErrorHalted = true;
		if ( state->controlPaused || state->debuggerOwnsRuntimeClock )
		{
			sender.EndSimulatorControlPause( false );
		}
		state->controlPaused = false;
		state->debuggerPaused = false;
		state->debuggerOwnsRuntimeClock = false;
		state->debuggerThread = NULL;
		state->debuggerStepThread = NULL;
		state->debuggerStepMode.clear();
		state->debuggerStepDepth = 0;
		state->stepFramesRemaining = 0;
		state->stepStartPending = false;
		WriteSimulatorControlSession( *state );
		WriteSimulatorControlDiagnostics( *state );
	}
}

bool
SimulatorControl::IsRuntimeErrorHalted( Runtime& sender )
{
	SimulatorControlRuntimeState *state =
		GetSimulatorControlRuntimeState( sender );
	return state && state->runtimeErrorHalted;
}

static void
RecordSimulatorControlRuntimeFrameStart(
	Runtime& runtime, SimulatorControlRuntimeState& state )
{
	const double elapsedMilliseconds = runtime.GetElapsedMS();
	if ( state.hasLastFrameStart &&
		elapsedMilliseconds > state.lastFrameStartElapsedMilliseconds )
	{
		state.frameIntervalsMilliseconds.push_back(
			elapsedMilliseconds - state.lastFrameStartElapsedMilliseconds );
		while ( state.frameIntervalsMilliseconds.size() >
			kSimulatorControlMaximumFrameTimingSamples )
		{
			state.frameIntervalsMilliseconds.pop_front();
		}
	}
	state.lastFrameStartElapsedMilliseconds = elapsedMilliseconds;
	state.hasLastFrameStart = true;
}

bool
SimulatorControl::ShouldRunRuntimeFrame( Runtime& sender )
{
	SimulatorControlRuntimeState *state =
		GetSimulatorControlRuntimeState( sender );
	if ( ! state || ! state->controlPaused )
	{
		if ( state )
		{
			RecordSimulatorControlRuntimeFrameStart( sender, *state );
		}
		return true;
	}
	if ( state->stepStartPending )
	{
		state->stepStartPending = false;
		sender.WaitSimulatorControlPausedFrame();
		return false;
	}
	if ( state->stepFramesRemaining > 0 && ! sender.IsSuspended() )
	{
		state->stepFramesRemaining--;
		sender.AdvanceSimulatorControlPausedFrame();
		RecordSimulatorControlRuntimeFrameStart( sender, *state );
		return true;
	}
	state->stepFramesRemaining = 0;
	sender.WaitSimulatorControlPausedFrame();
	return false;
}

bool
SimulatorControl::CanDispatchApplicationEvent( Runtime& sender )
{
	SimulatorControlRuntimeState *state =
		GetSimulatorControlRuntimeState( sender );
	return ! state || ( ! state->runtimeErrorHalted &&
		! state->debuggerPaused &&
		( ! state->controlPaused || state->controlledInputDispatchDepth > 0 ) );
}

SimulatorControl::InputDispatchGuard::InputDispatchGuard( Runtime& sender )
:	fRuntime( & sender )
{
	SimulatorControlRuntimeState *state =
		GetSimulatorControlRuntimeState( fRuntime );
	if ( state )
	{
		state->controlledInputDispatchDepth++;
	}
}

SimulatorControl::InputDispatchGuard::~InputDispatchGuard()
{
	SimulatorControlRuntimeState *state =
		GetSimulatorControlRuntimeState( fRuntime );
	if ( state && state->controlledInputDispatchDepth > 0 )
	{
		state->controlledInputDispatchDepth--;
	}
}

void
SimulatorControl::Shutdown( Runtime& sender )
{
	SimulatorControlRuntimeStateMap& states = GetSimulatorControlRuntimeStates();
	SimulatorControlRuntimeStateMap::iterator iterator = states.find( & sender );
	if ( states.end() == iterator )
	{
		return;
	}
	SimulatorControlRuntimeState& state = iterator->second;
	if ( state.debuggerHookInstalled )
	{
		lua_State *L = sender.VMContext().L();
		if ( L )
		{
			lua_sethook( L, NULL, 0, 0 );
		}
		state.debuggerHookInstalled = false;
	}
	if ( state.controlPaused || state.debuggerOwnsRuntimeClock )
	{
		sender.EndSimulatorControlPause( false );
		state.controlPaused = false;
		state.debuggerOwnsRuntimeClock = false;
	}

	SimulatorControlSession currentSession;
	if ( ReadSimulatorControlSession(
			state.directory, currentSession ) &&
		currentSession.sessionId == state.sessionId &&
		currentSession.processId ==
			(unsigned long)GetSimulatorControlProcessId() )
	{
		DeleteSimulatorControlFile(
			SimulatorControlPath(
				state.directory, "session.json" ) );
		std::string diagnostics;
		std::string diagnosticsSessionId;
		std::string diagnosticsPath =
			SimulatorControlPath(
				state.directory, "diagnostics.json" );
		if ( ReadSimulatorControlFile( diagnosticsPath, diagnostics ) &&
			ParseSimulatorControlJsonString(
				diagnostics, "\"sessionId\":",
				diagnosticsSessionId ) &&
			diagnosticsSessionId == state.sessionId )
		{
			DeleteSimulatorControlFile( diagnosticsPath );
		}
	}
	states.erase( iterator );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // Rtt_AUTHORING_SIMULATOR

#ifdef Rtt_AUTHORING_SIMULATOR

extern "C" int
Rtt_RunSimulatorControlClient(
	int argc, const char * const argv[], int *outExitCode )
{
	if ( ! outExitCode )
	{
		return 0;
	}
	int exitCode = 0;
	if ( ! Rtt::RunSimulatorControlClientInternal( argc, argv, exitCode ) )
	{
		return 0;
	}
	*outExitCode = exitCode;
	return 1;
}

#else

extern "C" int
Rtt_RunSimulatorControlClient(
	int argc, const char * const argv[], int *outExitCode )
{
	return 0;
}

#endif
