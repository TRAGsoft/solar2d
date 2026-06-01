//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_IPhoneDevice.h"

#include "Rtt_LuaContext.h"
#include "Rtt_MPlatform.h"

#import "Rtt_AppleCallback.h"

// TODO: Remove dependency
#import "AppDelegate.h"

#import "CoronaSystemResourceManager.h"
#import "CoronaViewPrivate.h"

#import <Foundation/NSNotification.h>
#import <Foundation/NSString.h>
#import <UIKit/UIAccelerometer.h>
#import <UIKit/UIApplication.h>
#import <UIKit/UIDevice.h>
#import <AudioToolbox/AudioServices.h>
#import <CoreMotion/CoreMotion.h>
#import <CommonCrypto/CommonDigest.h>

#include <sys/types.h> // for sysctlbyname
#include <sys/sysctl.h> // for sysctlbyname
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------
IPhoneDevice::IPhoneDevice( Rtt_Allocator &allocator, CoronaView *view )
:	fAllocator( allocator ),
	fView( view ),
	fTracker(),
	fInputDeviceManager( &allocator )
{
}

IPhoneDevice::~IPhoneDevice()
{
	const EventType kEvents[] =
	{
		kAccelerometerEvent,
		kGyroscopeEvent,
		kMultitouchEvent,
	};
	const int kEventsLen = sizeof( kEvents ) / sizeof( kEvents[0] );
	for ( int i = 0; i < kEventsLen; i++ )
	{
		EventType t = kEvents[i];
		if ( DoesNotify( t ) )
		{
			EndNotifications( t );
		}
	}
	
	fView = nil; // Weak ref, so no release.
}

const char*
IPhoneDevice::GetName() const
{
	return [[[UIDevice currentDevice] name] UTF8String];
}

const char*
IPhoneDevice::GetManufacturer() const
{
	return [@"Apple" UTF8String];
}

const char*
IPhoneDevice::GetModel() const
{
	return [[[UIDevice currentDevice] model] UTF8String];
}

bool
IPhoneDevice::IsPhone() const
{
	return [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone;
}

MPlatformDevice::EnvironmentType
IPhoneDevice::GetEnvironment() const
{
	return kDeviceEnvironment;
}

const char*
IPhoneDevice::GetPlatformName() const
{
	return [@"iPhone OS" UTF8String];
}

const char*
IPhoneDevice::GetPlatform() const
{
	return [@"ios" UTF8String];
}

const char*
IPhoneDevice::GetPlatformVersion() const
{
	return [[[UIDevice currentDevice] systemVersion] UTF8String];
}

const char*
IPhoneDevice::GetArchitectureInfo() const
{
	size_t size;
	sysctlbyname("hw.machine", NULL, &size, NULL, 0);
	char* machine = (char*)malloc(size);
	sysctlbyname("hw.machine", machine, &size, NULL, 0);
	/* 
	   Possible values: 
	   "iPhone1,1" = iPhone 1G 
	   "iPhone1,2" = iPhone 3G 
	   "iPhone2,1" = iPhone 3GS 
	   "iPod1,1"   = iPod touch 1G 
	   "iPod2,1"   = iPod touch 2G 
	 */  
	NSString* platform = [NSString stringWithUTF8String:machine];  

	free(machine);
	return [platform UTF8String];
}

PlatformInputDeviceManager&
IPhoneDevice::GetInputDeviceManager()
{
	return fInputDeviceManager;
}

static NSString *
MD5Hash( NSString *value )
{
	const char *str = [value UTF8String];

	// Create byte array of unsigned chars
	unsigned char md5Buffer[CC_MD5_DIGEST_LENGTH];

	// Create 16 byte MD5 hash value, store in buffer
	CC_MD5( str, (CC_LONG)strlen(str), md5Buffer );

	// Convert MD5 value in the buffer to NSString of hex values
	NSMutableString *output = [NSMutableString stringWithCapacity:CC_MD5_DIGEST_LENGTH * 2];
	for(int i = 0; i < CC_MD5_DIGEST_LENGTH; i++)
	{
		[output appendFormat:@"%02x",md5Buffer[i]];
	}

	return output;
}

#define UUID_USER_DEFAULTS_KEY @"CoronaID"

static NSString *
GetApprovedIdentifier()
{
    if ([[UIDevice currentDevice] respondsToSelector:@selector(identifierForVendor)])
    {
        // Return the MD5 hash of the identifierForVendor (hashed to make it backwards compatible)
        return MD5Hash([[[UIDevice currentDevice] identifierForVendor] UUIDString] );
    }
    else
    {
        // No "identifierForVendor", return remembered UUID or generate a new one
        NSString *uuidString = nil;
        
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        
        if ((uuidString = [defaults objectForKey:UUID_USER_DEFAULTS_KEY]) == nil)
        {
            // No saved UUID, generate one
            CFUUIDRef uuid = CFUUIDCreate(NULL);
            
            if (uuid)
            {
                uuidString = (NSString *)CFUUIDCreateString(NULL, uuid);
                CFRelease(uuid);
                
                [defaults setObject:uuidString forKey:UUID_USER_DEFAULTS_KEY];
                [defaults synchronize];

                [uuidString autorelease];
            }
            else
            {
                // Not much we can do
                return nil;
            }
        }
        
        return MD5Hash(uuidString);
    }
}

	
const char*
IPhoneDevice::GetUniqueIdentifier( IdentifierType t ) const
{
	const char *result = NULL;

	switch ( t )
	{
        case MPlatformDevice::kDeviceIdentifier:
        case MPlatformDevice::kHardwareIdentifier:
            result = [GetApprovedIdentifier() UTF8String];
            break;
        case MPlatformDevice::kMacIdentifier:
            // Apple doesn't allow this anymore 2013-08-22: result = [GetMacAddress() UTF8String];
            break;
        case MPlatformDevice::kUdidIdentifier:
            //result = "98765432109876543210";
            break;
        case MPlatformDevice::kIOSIdentifierForVendor:
            if ([[UIDevice currentDevice] respondsToSelector:@selector(identifierForVendor)])
            {
                result = [[[[UIDevice currentDevice] identifierForVendor] UUIDString] UTF8String];
            }
            break;
		default:
			break;
	}
    
	return result;
}
	
void
IPhoneDevice::Vibrate(const char * hapticType, const char* hapticStyle) const
{
    NSString * type = nil;
    if(hapticType){
       type = [[NSString alloc] initWithUTF8String:hapticType];
    }
    NSString * style = nil;
    if(hapticStyle){
       style = [[NSString alloc] initWithUTF8String:hapticStyle];
    }
   if([type isEqualToString:@"impact"]){
       UIImpactFeedbackStyle feedbackStyle = UIImpactFeedbackStyleMedium; // default
         if (style != nil) {
           if ([style isEqualToString:@"light"]) {
               feedbackStyle = UIImpactFeedbackStyleLight;
           } else if ([style isEqualToString:@"heavy"]) {
               feedbackStyle = UIImpactFeedbackStyleHeavy;
           } else if ([style isEqualToString:@"rigid"]) {
               if (@available(iOS 13.0, *)) {
                feedbackStyle = UIImpactFeedbackStyleRigid;
               }//else we use medium
           } else if ([style isEqualToString:@"soft"]) {
               if (@available(iOS 13.0, *)) {
                   feedbackStyle = UIImpactFeedbackStyleSoft;
               }//else we use medium
           }
         }
       UIImpactFeedbackGenerator *feedback = [[UIImpactFeedbackGenerator alloc] initWithStyle:feedbackStyle];
       [feedback prepare];
       [feedback impactOccurred];
       feedback = nil;
   }else if([type isEqualToString:@"selection"]){
       UISelectionFeedbackGenerator *generator = [UISelectionFeedbackGenerator new];
       [generator selectionChanged];
   }else if([type isEqualToString:@"notification"]){
       UINotificationFeedbackType feedbackType = UINotificationFeedbackTypeSuccess; // default
       if (style != nil) {
           if ([style isEqualToString:@"warning"]) {
             feedbackType = UINotificationFeedbackTypeWarning;
           } else if ([style isEqualToString:@"error"]) {
             feedbackType = UINotificationFeedbackTypeError;
           }
       }
       UINotificationFeedbackGenerator *feedback = [UINotificationFeedbackGenerator new];
       [feedback prepare];
       [feedback notificationOccurred:feedbackType];
       feedback = nil;
   }else{
       if(type != nil){Rtt_Log("WARNING: invalid hapticType");} //just in case user misspells or puts a wrong type
       AudioServicesPlaySystemSound( kSystemSoundID_Vibrate );
   }
}

bool
IPhoneDevice::HasEventSource( EventType type ) const
{
	bool hasEventSource = false;
	
	switch (type)
	{
		case MPlatformDevice::kGyroscopeEvent:
		{
			hasEventSource = [[CoronaSystemResourceManager sharedInstance] resourceAvailableForKey:CoronaGyroscopeResourceKey()];
			break;
		}
		case MPlatformDevice::kAccelerometerEvent:
		case MPlatformDevice::kMultitouchEvent:
		case MPlatformDevice::kInputDeviceStatusEvent:
			hasEventSource = true;
			break;
		case MPlatformDevice::kKeyEvent:
			if (@available(iOS 13.4, *)) { //Key Events only work on iOS 13.4 >=
				hasEventSource = true;
			}
			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
	}
	return hasEventSource;
}

void
IPhoneDevice::BeginNotifications( EventType type ) const
{
	fTracker.BeginNotifications( type );

	CoronaSystemResourceManager *resourceManager = [CoronaSystemResourceManager sharedInstance];

	switch( type )
	{
		case MPlatformDevice::kAccelerometerEvent:
		{
			// TODO: Remove dependency on AppDelegate. Move to CoronaSystemResourceManager.
			[UIAccelerometer sharedAccelerometer].delegate = (id<UIAccelerometerDelegate>)[UIApplication sharedApplication].delegate;
			break;
		}
		case MPlatformDevice::kGyroscopeEvent:
		{
			[resourceManager addObserver:fView.gyroscopeObserver forKey:CoronaGyroscopeResourceKey()];
			break;
		}
		case MPlatformDevice::kMultitouchEvent:
		{
			fView.multipleTouchEnabled = YES;
			break;
		}
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
	}
}

void
IPhoneDevice::EndNotifications( EventType type ) const
{
	fTracker.EndNotifications( type );

	CoronaSystemResourceManager *resourceManager = [CoronaSystemResourceManager sharedInstance];

	switch( type )
	{
		case MPlatformDevice::kAccelerometerEvent:
		{
			[UIAccelerometer sharedAccelerometer].delegate = nil;

			// TODO: Remove dependency on AppDelegate. Move to CoronaSystemResourceManager.
			AppDelegate* appdelegate = (AppDelegate*)[UIApplication sharedApplication].delegate;
			appdelegate.lastAccelerometerTimeStamp = 0.0;
			break;
		}
		case MPlatformDevice::kGyroscopeEvent:
		{
			[resourceManager removeObserver:fView.gyroscopeObserver forKey:CoronaGyroscopeResourceKey()];
			break;
		}
		case MPlatformDevice::kMultitouchEvent:
		{
			fView.multipleTouchEnabled = NO;
			break;
		}
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
	}
}

bool
IPhoneDevice::DoesNotify( EventType type ) const
{
	return fTracker.DoesNotify( type );
}

void
IPhoneDevice::SetAccelerometerInterval( U32 frequency ) const
{
	Rtt_WARN_SIM( frequency >= 10 && frequency <= 100, ( "WARNING: Accelerometer frequency on iPhone must be in the range [10,100] Hz" ) );
	NSTimeInterval interval = 1.0 / frequency;
	[UIAccelerometer sharedAccelerometer].updateInterval = interval;
}

void
IPhoneDevice::SetGyroscopeInterval( U32 frequency ) const
{
	if( ! HasEventSource( kGyroscopeEvent ) )
	{
		return;
	}
	CMMotionManager* motion = [CoronaSystemResourceManager sharedInstance].motionManager;
	if ( nil == motion )
	{
		return;
	}
	double period = 1.0 / frequency;
	motion.gyroUpdateInterval = period;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

