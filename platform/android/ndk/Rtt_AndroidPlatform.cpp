//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////


#include "Core/Rtt_Build.h"
#include "Core/Rtt_String.h"
#include "Corona/CoronaLua.h"
#include "Display/Rtt_PlatformBitmap.h"
#include "Rtt_RenderingStream.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaLibNative.h"
#include "Rtt_PreferenceCollection.h"
#include "Rtt_Runtime.h"

#include "Rtt_AndroidPlatform.h"
#include "Rtt_AndroidBitmap.h"
#include "Rtt_AndroidDisplayObject.h"
#include "Rtt_PlatformExitCallback.h"
#include "Rtt_AndroidScreenSurface.h"
#include "Rtt_AndroidTextFieldObject.h"
#include "Rtt_AndroidTimer.h"
#include "Rtt_AndroidWebViewObject.h"

#include "AndroidDisplayObjectRegistry.h"
#include "AndroidGLView.h"
#include "NativeToJavaBridge.h"
#include "CoronaLua.h"
#include "jniUtils.h"

#include <stdio.h>
#include <ctype.h>

// ----------------------------------------------------------------------------

#if !defined( Rtt_CUSTOM_CODE )
Rtt_EXPORT const luaL_Reg* Rtt_GetCustomModulesList()
{
	return NULL;
}
#endif

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

AndroidPlatform::AndroidPlatform(
	AndroidGLView * pView, const char * package, const char * documentsDir, const char * applicationSupportDir, const char * temporaryDir,
	const char * cachesDir, const char * systemCachesDir, NativeToJavaBridge *ntjb )
  :	fAllocator( Rtt_AllocatorCreate() ),
	fView( pView ),
	fDevice( *fAllocator, ntjb ),
	fPackage( fAllocator ),
	fDocumentsDir( fAllocator ),
	fApplicationSupportDir( fAllocator ),
	fTemporaryDir( fAllocator ),
	fCachesDir( fAllocator ),
	fSystemCachesDir( fAllocator ),
	fDisplayObjectRegistry( NULL ),
	fCrypto( ntjb ),
	fNativeToJavaBridge( ntjb )
{
	// Initialize member variables.
	fPackage.Set( package );
	fDocumentsDir.Set( documentsDir );
	fApplicationSupportDir.Set( applicationSupportDir );
	fTemporaryDir.Set( temporaryDir );
	fCachesDir.Set( cachesDir );
	fSystemCachesDir.Set( systemCachesDir );
	fDisplayObjectRegistry = Rtt_NEW( fAllocator, AndroidDisplayObjectRegistry() );

	// Set up SQLite to create its temp files to a valid writable directory, if not done already.
	// Works-around an issue where SQLite defaults to a Unix style /tmp directory that doesn't exist on Android.
	// This is needed or else SQL transactions will fail due to SQLite being unable to create a journal file.
	if (!Rtt_StringIsEmpty(systemCachesDir) && !getenv("SQLITE_TMPDIR"))
	{
		setenv("SQLITE_TMPDIR", systemCachesDir, 1);
	}
}

AndroidPlatform::~AndroidPlatform()
{
	Rtt_DELETE( fDisplayObjectRegistry );
}

MPlatformDevice&
AndroidPlatform::GetDevice() const
{
	return const_cast< AndroidDevice& >( fDevice );
}

PlatformSurface*
AndroidPlatform::CreateScreenSurface() const
{
	int approximateScreenDpi = fNativeToJavaBridge->GetApproximateScreenDpi();
	return Rtt_NEW( fAllocator, AndroidScreenSurface( fView, approximateScreenDpi ) );
}

PlatformSurface* 
AndroidPlatform::CreateOffscreenSurface( const PlatformSurface& parent ) const
{
	// Note: This function assumes that offscreen framebuffers are only created for screenshots.

	// Most Android devices such as Droids and Galaxy Tabs crash when calling glReadPixels()
	// on a framebuffer object, even if the GPU supports that extenions. Because of this,
	// we should avoid framebuffer objects for screenshots unless the following is true.
	if (AndroidOffscreenSurface::IsSupported() &&
	    fView && (fView->HasAlphaChannel() == false) &&
	    (Rtt_StringCompare((const char*)glGetString(GL_RENDERER), "PowerVR SGX 540") == 0))
	{
		return Rtt_NEW( fAllocator, AndroidOffscreenSurface( parent ) );
	}

	// The current Android device cannot use offscreen framebuffers reliably.
	// Returning NULL tells the caller to do a screen capture from the main display instead.
	return NULL;
}

bool
AndroidPlatform::OpenURL( const char* url ) const
{
	return fNativeToJavaBridge->OpenUrl( url );
}

int
AndroidPlatform::CanOpenURL( const char* url ) const
{
	return fNativeToJavaBridge->CanOpenUrl( url ) ? 1 : 0;
}

void
AndroidPlatform::SetIdleTimer( bool enabled ) const
{
	fNativeToJavaBridge->SetIdleTimer( enabled );
}

bool
AndroidPlatform::GetIdleTimer() const
{
	return fNativeToJavaBridge->GetIdleTimer();
}


NativeAlertRef
AndroidPlatform::ShowNativeAlert(
	const char *title,
	const char *msg,
	const char **buttonLabels,
	U32 numButtons,
	LuaResource* resource ) const
{
	fNativeToJavaBridge->ShowNativeAlert( title, msg, buttonLabels, numButtons, resource );

	return (NativeAlertRef) 0x1234;
}

void
AndroidPlatform::CancelNativeAlert( NativeAlertRef alert, S32 index ) const
{
	fNativeToJavaBridge->CancelNativeAlert( index );
}

Rtt_Allocator& AndroidPlatform::GetAllocator() const
{
    return * fAllocator;
}

RenderingStream* AndroidPlatform::CreateRenderingStream() const
{
	RenderingStream* result = Rtt_NEW( fAllocator, RenderingStream( fAllocator ) );
	
	result->SetProperty( RenderingStream::kFlipHorizontalAxis, true );

	return result;
}

PlatformTimer* AndroidPlatform::CreateTimerWithCallback( MCallback& callback ) const
{
    return Rtt_NEW( fAllocator, AndroidTimer( callback, fNativeToJavaBridge ) );
}

PlatformBitmap* AndroidPlatform::CreateBitmap( const char* path, bool convertToGrayscale ) const
{
	PlatformBitmap *result = NULL;
	if ( path )
	{
		if ( convertToGrayscale )
		{
			result = Rtt_NEW( & GetAllocator(), AndroidMaskAssetBitmap( GetAllocator(), path, fNativeToJavaBridge ) );
		}
		else
		{
			result = Rtt_NEW( & GetAllocator(), AndroidAssetBitmap( GetAllocator(), path, fNativeToJavaBridge ) );
		}
	}

	return result;
}

bool 
AndroidPlatform::FileExists( const char *filename ) const
{
	// Validate.
	if (Rtt_StringIsEmpty(filename))
	{
		return false;
	}

	// Determine if the given file name exists.
	bool fileExists = false;
	if (filename[0] == '/')
	{
		// The given file name is an absolute path.
		// Since we have direct file access, we can check for its existence here.
		FILE *file = fopen(filename, "r");
		if (file)
		{
			fileExists = true;
			fclose(file);
		}
	}
	else
	{
		// The given file name is likely a relative path to an asset file or a URL to a local file.
		// Check for its existence via Android's APIs on the Java side.
		fileExists = fNativeToJavaBridge->GetRawAssetExists(filename);
		if (!fileExists)
		{
			fileExists = fNativeToJavaBridge->GetCoronaResourceFileExists(filename);
		}

	}
	return fileExists;
}

bool
AndroidPlatform::ValidateAssetFile(const char *assetFilename, const int assetSize) const
{
	Rtt_TRACE(("ValidateAssetFile: checking %s is %d bytes\n", assetFilename, assetSize));
	
	if (assetFilename != NULL && assetSize >= 0)
	{
		int assetFileBytes = 0;
		Rtt::Data<char> assetFileData(NULL);

		if (NativeToJavaBridge::GetRawAsset(assetFilename, assetFileData))
		{
			Rtt_TRACE(("ValidateAssetFile: %s is %d bytes\n", assetFilename, assetFileData.GetLength()));

			return (assetFileData.GetLength() == assetSize);
		}
		else
		{
			Rtt_TRACE(("ValidateAssetFile: %s not found\n", assetFilename));

			return false;
		}
	}

	return false;
}

void 
AndroidPlatform::PathForFile( const char *filename, MPlatform::Directory baseDir, U32 flags, String& result ) const
{
	// Initialize result to an empty string in case the file was not found.
	result.Set(NULL);

	// Fetch the path for the given file.
	if (filename && strstr(filename, "://"))
	{
		// The given file name is a URL.
		// Accept it as is and ignore the base directory.
		result.Set(filename);
	}
	else
	{
		// Create an absolute path by appending the file name to the base directory.
		switch( baseDir )
		{
			case MPlatform::kResourceDir:
				PathForResourceFile( filename, result );
				if (FileExists( result.GetString() ) == false)
				{
					// See if the file exists in the plugin asset directory
					PathForPluginFile( filename, result );
				}

				break;
				
			case MPlatform::kSystemResourceDir:
				result.Set( filename );
				break;
				
			case MPlatform::kCachesDir:
				PathForFile( filename, fCachesDir.GetString(), result );
				break;
				
			case MPlatform::kSystemCachesDir:
				PathForFile( filename, fSystemCachesDir.GetString(), result );
				break;
				
			case MPlatform::kTmpDir:
				PathForFile( filename, fTemporaryDir.GetString(), result );
				break;

			case MPlatform::kPluginsDir:
				break;
				
			case MPlatform::kApplicationSupportDir:
				PathForFile( filename, fApplicationSupportDir.GetString(), result );
				break;

			case MPlatform::kDocumentsDir:
			default:
				PathForFile( filename, fDocumentsDir.GetString(), result );
				break;
		}
	}
	
	// Check if the file exists, if enabled.
	// Result will be set to an empty string if the file could not be found.
	if (flags & MPlatform::kTestFileExists)
	{
		if (FileExists( result.GetString() ) == false)
		{
			result.Set(NULL);
		}
	}
}


// These files can be extracted natively from the asset directory by java code
// and thus do not require externalization
static const char * assetFormats[] = {
	".3gp",
	".m4v",
	".mp4",
//	".ogg", // Needs Review: ALmixer backend needs this externalized.
//	".mp3", // Needs Review: ALmixer backend needs this externalized.
	".png",
	".jpg",
	".ttf",
	".htm",
	".html",
	NULL
};

void
AndroidPlatform::PathForResourceFile( const char * filename, String & result ) const
{
	if ( filename == NULL ) 
	{
		result.Set( NULL );
		return;
	}
	
	int format = 0;
	while ( assetFormats[format] != NULL )
	{
		if ( Rtt_StringEndsWithNoCase( filename, assetFormats[format] ) ) {
			result.Set( filename );
			return;
		}
		format++;
	}

	// Did not find a valid format. Externalize the file.
	fNativeToJavaBridge->ExternalizeResource( filename, & result );
}

void
AndroidPlatform::PathForPluginFile( const char * filename, String & result ) const
{
	if ( filename == NULL ) 
	{
		result.Set( NULL );
		return;
	}
	
	String pluginFilename;

	pluginFilename.Set( ".corona-plugins" );
	pluginFilename.AppendPathComponent( filename );

	int format = 0;
	while ( assetFormats[format] != NULL )
	{
		if ( Rtt_StringEndsWithNoCase( pluginFilename.GetString(), assetFormats[format] ) ) {
			result.Set( pluginFilename.GetString() );
			return;
		}
		format++;
	}

	// Did not find a valid format. Externalize the file.
	fNativeToJavaBridge->ExternalizeResource( pluginFilename.GetString(), & result );
}

void
AndroidPlatform::PathForFile( const char * filename, const char * baseDir, String & result ) const
{
	if ( filename )
	{
		// Append directory.
		result.Append( baseDir );
		
		// Append directory separator, if not already there.
		size_t npl = strlen( result.GetString() );
		if ( npl > 0 && result.GetString()[npl - 1] != '/' )
		{
			result.Append( "/" );
		}

		// Append the file name.
		result.Append( filename );
	}
	else
	{
		// File name not provided. So, just provide the directory path.
		result.Set( baseDir );
	}
}

void AndroidPlatform::RaiseError( MPlatform::Error e, const char * reason ) const
{
    const char kNull[] = "(null)";

    if ( ! reason ) { reason = kNull; }
    Rtt_TRACE( ( "MPlatformFactory error(%d): %s\n", e, kNull ) );
}

void
AndroidPlatform::SaveBitmap( PlatformBitmap * bitmap, Rtt::Data<const char> & pngBytes ) const
{
	// TODO: This is a horrible, horrible hack, and must die
	bitmap->SwapRGB();

	fNativeToJavaBridge->SaveBitmap( bitmap, pngBytes );

	// Swap the bytes back
	bitmap->SwapRGB();
}

int
AndroidPlatform::SetSync( lua_State* L ) const
{
	return 0;
}

int
AndroidPlatform::GetSync( lua_State* L ) const
{
	return 0;
}

void
AndroidPlatform::BeginRuntime( const Runtime& runtime ) const
{
}

void 
AndroidPlatform::EndRuntime( const Runtime& runtime ) const
{
}

PlatformExitCallback*
AndroidPlatform::GetExitCallback()
{
	return NULL;
}

bool
AndroidPlatform::RequestSystem( lua_State *L, const char *actionName, int optionsIndex ) const
{
	return fNativeToJavaBridge->RequestSystem(L, actionName, optionsIndex);
}

void
AndroidPlatform::RuntimeErrorNotification( const char *errorType, const char *message, const char *stacktrace ) const
{
	fNativeToJavaBridge->ShowRuntimeError( message, stacktrace );
}

const MCrypto& 
AndroidPlatform::GetCrypto() const
{
	// return const_cast< AndroidCrypto& >( fCrypto );
	return fCrypto;
}

void 
AndroidPlatform::GetPreference( Category category, Rtt::String * value ) const
{
	fNativeToJavaBridge->GetPreference( (int) category, value );
}

static OperationResult
IsPreferenceCategorySupported(const char* categoryName)
{
	if (Rtt_StringCompare(categoryName, Preference::kCategoryNameApp) != 0)
	{
		std::string message;
		message = "Category name '";
		message += categoryName ? categoryName : "";
		message += "' is not supported on this platform.";
		return OperationResult::FailedWith(message.c_str());
	}
	return OperationResult::kSucceeded;
}

Preference::ReadValueResult
AndroidPlatform::GetPreference( const char* categoryName, const char* keyName ) const
{
	OperationResult result = IsPreferenceCategorySupported(categoryName);
	if (result.HasFailed())
	{
		return Preference::ReadValueResult::FailedWith(result.GetUtf8MessageAsSharedPointer());
	}
	return fNativeToJavaBridge->GetPreference(keyName);
}

OperationResult
AndroidPlatform::SetPreferences( const char* categoryName, const PreferenceCollection& collection ) const
{
	OperationResult result = IsPreferenceCategorySupported(categoryName);
	if (result.HasFailed())
	{
		return result;
	}
	return fNativeToJavaBridge->SetPreferences(collection);
}

OperationResult
AndroidPlatform::DeletePreferences( const char* categoryName, const char** keyNameArray, U32 keyNameCount ) const
{
	OperationResult result = IsPreferenceCategorySupported(categoryName);
	if (result.HasFailed())
	{
		return result;
	}
	return fNativeToJavaBridge->DeletePreferences(keyNameArray, (size_t)keyNameCount);
}

bool
AndroidPlatform::CanShowPopup( const char *name ) const
{
	return fNativeToJavaBridge->CanShowPopup( name );
}

bool
AndroidPlatform::ShowPopup( lua_State *L, const char *name, int optionsIndex ) const
{
	bool result = false;

	// Validate.
	if (!L || !name || (strlen(name) <= 0))
	{
		return false;
	}
	
	// Display the requested window.
	if (Rtt_StringCompareNoCase(name, "mail") == 0)
	{
		// Display a "Send Mail" window.
		NativeToJavaBridge::DictionaryRef dictionaryOfSettings =
						NativeToJavaBridge::DictionaryCreate(L, optionsIndex, fNativeToJavaBridge);
		fNativeToJavaBridge->ShowSendMailPopup(dictionaryOfSettings, NULL);
		result = true;
	}
	else if (Rtt_StringCompareNoCase(name, "sms") == 0)
	{
		// Display a "Send SMS" window.
		NativeToJavaBridge::DictionaryRef dictionaryOfSettings =
						NativeToJavaBridge::DictionaryCreate(L, optionsIndex, fNativeToJavaBridge);
		fNativeToJavaBridge->ShowSendSmsPopup(dictionaryOfSettings, NULL);
		result = true;
	}
	else if ((Rtt_StringCompareNoCase(name, "requestAppPermission") == 0) 
		|| (Rtt_StringCompareNoCase(name, "requestAppPermissions") == 0))
	{
		// Display the request permission window.
		NativeToJavaBridge::DictionaryRef dictionaryOfSettings = 
			NativeToJavaBridge::DictionaryCreate(L, optionsIndex, fNativeToJavaBridge);
		if (optionsIndex != 0)
		{	
			// An options table was actually provided.
			// Add the listener to the registry on the C-side if applicable.
			int luaRegistryId = LUA_REFNIL;
			lua_getfield(L, optionsIndex, "listener");
				
			// If a listener is provided.
			if (CoronaLuaIsListener(L, -1, "popup"))
			{
				// Add an entry to the lua registry for this listener
				// Do this manually instead of using CoronaLuaNewRef to avoid problem with 
				// it returning a void* on the C-side, but the registry Id in Java.
				lua_pushvalue( L, -1 );
				luaRegistryId = luaL_ref( L, LUA_REGISTRYINDEX );

				// Add the registry Id to our settings dictionary
				((jHashMapParam*)dictionaryOfSettings)->put( "listener", luaRegistryId );
			}

			// Pop the listener from the lua stack
			lua_pop(L, 1);
		}

		fNativeToJavaBridge->ShowRequestPermissionsPopup(dictionaryOfSettings, NULL);
		result = true;
	}
	else
	{
		// Unknown window/popup name. Abort.
		result = false;
	}
	
	// Assume the window was displayed at this point.
	return result;
}

bool
AndroidPlatform::HidePopup( const char *name ) const
{
	return false;
}

PlatformDisplayObject *
AndroidPlatform::CreateNativeWebView( const Rect& bounds ) const
{
	return Rtt_NEW( & GetAllocator(), AndroidWebViewObject( bounds, fDisplayObjectRegistry, fNativeToJavaBridge ) );
}

PlatformDisplayObject *
AndroidPlatform::CreateNativeVideo( const Rect& bounds ) const
{
	return NULL;
}

PlatformDisplayObject * 
AndroidPlatform::CreateNativeTextField( const Rect & bounds ) const
{
	return Rtt_NEW( & GetAllocator(), AndroidTextFieldObject( bounds, fDisplayObjectRegistry, fNativeToJavaBridge ) );
}

void 
AndroidPlatform::SetKeyboardFocus( PlatformDisplayObject * textObject ) const
{
	if ( textObject )
	{
		((AndroidDisplayObject*)textObject)->SetFocus();
	}
	else
	{
		fNativeToJavaBridge->DisplayObjectSetFocus(AndroidDisplayObjectRegistry::INVALID_ID, false);
	}
}

PlatformDisplayObject *
AndroidPlatform::GetNativeDisplayObjectById( const int objectId ) const
{
	if (fDisplayObjectRegistry)
	{
		return fDisplayObjectRegistry->GetById(objectId);
	}
	return NULL;
}

// ----------------------------------------------------------------------------

void*
AndroidPlatform::CreateAndScheduleNotification( lua_State *L, int index ) const
{
	return (void*)fNativeToJavaBridge->NotificationSchedule(L, index);
}

void
AndroidPlatform::ReleaseNotification( void *notificationId ) const
{
}

void
AndroidPlatform::CancelNotification( void *notificationId ) const
{
	if (notificationId == NULL)
	{
		fNativeToJavaBridge->NotificationCancelAll();
	}
	else
	{
		fNativeToJavaBridge->NotificationCancel((uintptr_t)notificationId);
	}
}

// ----------------------------------------------------------------------------

void
AndroidPlatform::SetNativeProperty( lua_State *L, const char *key, int valueIndex ) const
{
	// Validate arguments.
	if (!L || Rtt_StringIsEmpty(key) || !valueIndex)
	{
		return;
	}

	// Handle the given property value.
	if (Rtt_StringCompare(key, "applicationIconBadgeNumber") == 0)
	{
		// Clear all notifications if the application badge number was set to zero.
		// Note: This is to match iOS' behavior.
		if (lua_type(L, valueIndex) == LUA_TNUMBER)
		{
			int badgeNumber = lua_tointeger(L, valueIndex);
			if (badgeNumber <= 0)
			{
				fNativeToJavaBridge->NotificationCancelAll();
			}
		}
	}
	else if (Rtt_StringCompare(key, "mouseCursorVisible") == 0)
	{
		CoronaLuaWarning(L, "native.setProperty(\"%s\") is not supported on Android.", key);
	}
}

int
AndroidPlatform::PushNativeProperty( lua_State *L, const char *key ) const
{
	// Validate.
	if (!L || Rtt_StringIsEmpty(key))
	{
		return 0;
	}

	// Push the requested native property information to Lua.
	int pushedValues = 0;
	if (Rtt_StringCompare(key, "mouseCursorVisible") == 0)
	{
		lua_pushboolean(L, 1);
		pushedValues = 1;
	}

	// Push nil if given a key that is unknown on this platform.
	if (pushedValues <= 0)
	{
		lua_pushnil(L);
		pushedValues = 1;
	}

	// Return the number of values pushed into Lua.
	return pushedValues;
}

int
AndroidPlatform::PushSystemInfo( lua_State *L, const char *key ) const
{
	return fNativeToJavaBridge->PushSystemInfoToLua(L, key);
}

NativeToJavaBridge*
AndroidPlatform::GetNativeToJavaBridge() const
{
	return fNativeToJavaBridge;
}

void
AndroidPlatform::Suspend( ) const
{
}

void
AndroidPlatform::Resume( ) const
{
}

void AndroidPlatform::GetSafeAreaInsetsPixels(Rtt_Real &top, Rtt_Real &left, Rtt_Real &bottom, Rtt_Real &right) const
{
	fNativeToJavaBridge->GetSafeAreaInsetsPixels(top, left, bottom, right);
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
