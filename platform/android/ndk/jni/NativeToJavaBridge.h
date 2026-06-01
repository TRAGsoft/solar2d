//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _NativeToJavaBridge_H__
#define _NativeToJavaBridge_H__

#include <vector>

#include <jni.h>
#include "Core/Rtt_Data.h"
#include "Core/Rtt_String.h"
#include "Core/Rtt_Array.h"
#include "Core/Rtt_OperationResult.h"
#include "Rtt_Preference.h"
#include <map>

namespace Rtt
{
	class AndroidPlatform;
	class LuaResource;
	class PlatformBitmap;
	class PreferenceCollection;
	class Runtime;
};
struct lua_State;
class AndroidImageData;
class AndroidZipFileEntry;


class NativeToJavaBridge
{
	public:
		typedef void* DictionaryRef;
		typedef jint jpointer;

		NativeToJavaBridge(JavaVM * vm, Rtt::Runtime *runtime, jobject coronaRuntime);

	protected:
		static JNIEnv * GetJNIEnv();
		jobject GetCallbackBridge() const;
		void GetString( const char *method, Rtt::String *outValue );
		void GetStringWithInt( const char *method, int param, Rtt::String *outValue );
		void GetStringWithLong( const char *method, long param, Rtt::String *outValue );
		void HandleJavaException() const;
		void HandleJavaExceptionUsing( lua_State *L ) const;
		void CallVoidMethod( const char * method ) const;
		void CallIntMethod( const char * method, int param ) const;
		void CallLongMethod( const char * method, long param ) const;
		void CallDoubleMethod( const char * method, double param ) const;
		void CallFloatMethod( const char * method, float param ) const;
		void CallStringMethod( const char * method, const char * param ) const;
	
	public:
		static void* JavaToNative( jpointer p );
		static NativeToJavaBridge * InitInstance( JNIEnv *env, Rtt::Runtime *runtime, jobject coronaRuntime );
		// static NativeToJaaBridge *GetInstance();
		static DictionaryRef DictionaryCreate( lua_State *L, int t, NativeToJavaBridge *bridge );

		Rtt::Runtime * GetRuntime() const;
		void SetRuntime(Rtt::Runtime *runtime);
		Rtt::AndroidPlatform * GetPlatform() const;
		// bool HasLuaErrorOccurred() const { return fHasLuaErrorOccurred; }
		DictionaryRef DictionaryCreate();
		void DictionaryDestroy( DictionaryRef dict );
	
		bool RequestSystem( lua_State *L, const char *actionName, int optionsIndex );
		void Ping();
		
		int LoadClass( lua_State *L, const char *libName, const char *className );
		int LoadFile( lua_State *L, const char *fileName );
		void OnRuntimeLoaded(lua_State *L);
		void OnRuntimeWillLoadMain();
		void OnRuntimeStarted();
		void OnRuntimeSuspended();
		void OnRuntimeResumed();
		void OnRuntimeExiting();

		static int InvokeLuaErrorHandler(lua_State *L);

		void PushLaunchArgumentsToLuaTable(lua_State *L);
		void PushApplicationOpenArgumentsToLuaTable(lua_State *L);
	
		static bool GetRawAsset( const char * assetName, Rtt::Data<char> & result );
		bool GetRawAssetExists( const char * assetName );
		bool GetCoronaResourceFileExists( const char * assetName );
		static bool GetAssetFileLocation( const char * assetName, AndroidZipFileEntry &zipFileEntry );
		bool LoadImage(
				const char *filePath, AndroidImageData& imageData, bool convertToGrayscale,
				int maxWidth, int maxHeight, bool loadImageInfoOnly);
		void SaveBitmap( const Rtt::PlatformBitmap * bitmap, Rtt::Data<const char> & pngBytes );
	
		void SetTimer( int milliseconds );
		void CancelTimer();

		void HttpPost( const char* url, const char* key, const char* value );
	
		bool CanOpenUrl( const char* url );
		bool OpenUrl( const char * url );
		
		void SetIdleTimer( bool enabled );
		bool GetIdleTimer() const;
	
		void GetSafeAreaInsetsPixels(Rtt::Real &top, Rtt::Real &left, Rtt::Real &bottom, Rtt::Real &right);
		
		void ShowNativeAlert( const char * title, const char * msg, 
			const char ** labels, int numLabels, Rtt::LuaResource * resource );
		void CancelNativeAlert( int which );
		void AlertCallback( int which, bool cancelled );
		bool CanShowPopup( const char *name );
		void ShowSendMailPopup( NativeToJavaBridge::DictionaryRef dictionaryOfSettings, Rtt::LuaResource *resource );
		void ShowSendSmsPopup( NativeToJavaBridge::DictionaryRef dictionaryOfSettings, Rtt::LuaResource *resource );
		void ShowRequestPermissionsPopup( NativeToJavaBridge::DictionaryRef dictionaryOfSettings, Rtt::LuaResource *resource );
		void RaisePopupClosedEvent( const char *popupName, bool wasCanceled );
	
		void DisplayUpdate();
	
		void GetManufacturerName( Rtt::String *outValue );
		void GetModel( Rtt::String *outValue );
		void GetName( Rtt::String *outValue );
		void GetUniqueIdentifier( int t, Rtt::String *outValue );
		void GetPlatformVersion( Rtt::String *outValue );
		void GetProductName( Rtt::String *outValue );
		int GetApproximateScreenDpi();
		bool IsPhone();
		int PushSystemInfoToLua( lua_State *L, const char *key );
		void GetPreference( int category, Rtt::String *outValue );
		Rtt::Preference::ReadValueResult GetPreference( const char* keyName );
		Rtt::OperationResult SetPreferences( const Rtt::PreferenceCollection& collection );
		Rtt::OperationResult DeletePreferences( const char** keyNameArray, size_t keyNameCount );
		void GetSystemProperty( const char *name, Rtt::String *outValue );
		long GetUptimeInMilliseconds();
		void MakeLowerCase( Rtt::String *stringToConvert );
		
		void SetAccelerometerInterval( int frequency );
		void SetGyroscopeInterval( int frequency );
		void SetEventNotification( int eventType, bool enable );
		bool HasAccelerometer();
		bool HasGyroscope();
		void Vibrate(const char * hapticType = NULL, const char* hapticStyle = NULL);
		
		void DisplayObjectDestroy( int id );
		void DisplayObjectSetVisible( int id, bool visible );
		bool DisplayObjectGetVisible( int id );
		void DisplayObjectSetAlpha( int id, float alpha );
		float DisplayObjectGetAlpha( int id );
		void DisplayObjectSetBackground( int id, bool bg );
		bool DisplayObjectGetBackground( int id );
		void DisplayObjectSetFocus( int id, bool focus );
		void DisplayObjectUpdateScreenBounds( int id, int x, int y, int width, int height );
		int GetDefaultTextFieldPaddingInPixels();
		int TextFieldCreate( int id, int left, int top, int width, int height );
		void TextFieldSetReturnKey( int id, const char * imeType );
		void TextFieldSetSelection( int id, int startPosition, int endPosition );
		void TextFieldSetPlaceholder( int id, const char * placeholder );
		void TextFieldSetColor( int id, int r, int g, int b, int a );
		void TextFieldSetText( int id, const char * text );
		void TextFieldSetAlign( int id, const char * align );
		void TextFieldSetSecure( int id, bool isSecure );
		void TextFieldSetInputType( int id, const char * inputType );
		void TextFieldSetMaxLength( int id, int maxLength );
		void TextFieldGetColor( int id, int * r, int * g, int * b, int * a );
		void TextFieldGetText( int id, Rtt::String * text );
		void TextFieldGetPlaceholder( int id, Rtt::String * placeholder );
		void TextFieldGetAlign( int id, Rtt::String * align );
		bool TextFieldGetSecure( int id );
		void TextFieldGetInputType( int id, Rtt::String * inputType );
		int TextFieldGetMaxLength( int id );

	public:
		void WebViewCreate( int id, int left, int top, int width, int height, bool isPopup, bool autoCancelEnabled );
		void WebViewRequestLoadUrl( int id, const char * url, const char * header );
		void WebViewRequestReload( int id );
		void WebViewRequestStop( int id );
		void WebViewRequestGoBack( int id );
		void WebViewRequestGoForward( int id );
		void WebViewRequestDeleteCookies( int id );

		int CryptoGetDigestLength( const char * algorithm );
		void CryptoCalculateDigest( const char * algorithm, const Rtt::Data<const char> & data, U8 * digest );
		void CryptoCalculateHMAC( const char * algorithm, const Rtt::Data<const char> & key, const Rtt::Data<const char> & data, 
			U8 * digest );
	
		void ExternalizeResource( const char * assetName, Rtt::String * result );

	public:
		int NotificationSchedule( lua_State *L, int index );
		void NotificationCancel( int id );
		void NotificationCancelAll();
		void GooglePushNotificationsRegister( const char *projectNumber );
		void GooglePushNotificationsUnregister();

	public:
		void FetchInputDevice(int coronaDeviceId);
		void FetchAllInputDevices();
		void VibrateInputDevice(int coronaDeviceId);

	public:
		static bool DecodeBase64( const Rtt::Data<const char> & payload, Rtt::Data<char> & data );
		static bool Check(const Rtt::Data<const char> & publicKey, const Rtt::Data<const char> & signature, const Rtt::Data<const char> & payloadData);
		static void getAudioOutputSettings(std::vector<int>& settings);

	private:
		static JavaVM *fVM;
		Rtt::Runtime *fRuntime;
		// bool fHasLuaErrorOccurred;
		Rtt::LuaResource *fAlertCallbackResource;
		Rtt::LuaResource *fPopupClosedEventResource;
		jobject fCoronaRuntime;
};


// ----------------------------------------------------------------------------

#endif // _NativeToJavaBridge_H__
