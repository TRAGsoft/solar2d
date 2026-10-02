//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Assert_H__
#define _Rtt_Assert_H__

#include "Rtt_Macros.h"
#include <stddef.h>
#include <stdarg.h>

typedef void (*Rtt_LogCallback)(
	const char *message, size_t length, void *context );

Rtt_EXPORT void Rtt_LogEnable(void);
Rtt_EXPORT void Rtt_LogDisable(void);
Rtt_EXPORT int Rtt_LogIsEnabled(void);
Rtt_EXPORT void Rtt_SetLogCallback( Rtt_LogCallback callback, void *context );
Rtt_EXPORT void Rtt_InvokeLogCallback( const char *format, va_list arguments );

typedef struct Rtt_LogSink
{
	Rtt_LogCallback callback;
	void *context;
} Rtt_LogSink;

// Returns the previous sink so nested runtime calls can restore their owner.
Rtt_EXPORT Rtt_LogSink Rtt_SetThreadLogSink( Rtt_LogSink sink );

#ifdef __cplusplus
namespace Rtt
{
class ScopedLogSink
{
	public:
		ScopedLogSink( Rtt_LogCallback callback, void *context )
		{
			Rtt_LogSink sink = { callback, context };
			fPrevious = Rtt_SetThreadLogSink( sink );
		}
		~ScopedLogSink() { Rtt_SetThreadLogSink( fPrevious ); }
	private:
		ScopedLogSink( const ScopedLogSink& );
		ScopedLogSink& operator=( const ScopedLogSink& );
		Rtt_LogSink fPrevious;
};
}
#endif

#if defined( Rtt_MAC_ENV )
	// On OSX we can easily validate printf-style function arguments
	Rtt_EXPORT int Rtt_Log( const char *format, ... ) __attribute__ ((format (printf, 1, 2)));
	Rtt_EXPORT int Rtt_LogException( const char *format, ... ) __attribute__ ((format (printf, 1, 2)));
	Rtt_EXPORT int Rtt_VLogException( const char *format, va_list ap );
#else
	Rtt_EXPORT Rtt_DECL_API int Rtt_Log(const char *format, ...);
	Rtt_EXPORT int Rtt_LogException( const char *format, ... );
	Rtt_EXPORT int Rtt_VLogException( const char *format, va_list ap );
#endif // Rtt_MAC_ENV

#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )

	// A better way to write this is to use variadic macros:
	//
	//		http://en.wikipedia.org/wiki/Variadic_macro
	//
	// This would avoid having to use double parentheses
	// when using these macros.
	//
	//		"Rtt_TRACE_SIM( ( expr ) );"
	//
	// Example:
	//
	//		#define Rtt_TRACE_SIM( ... ) Rtt_Log( __VA_ARGS__ )
	//
	// The above allows us to use it this way:
	//
	//		"Rtt_TRACE_SIM( expr );"

	#define Rtt_TRACE_SIM( expr ) Rtt_Log expr
	#define Rtt_WARN_SIM( cond, expr ) (void)( 0 != (cond) ? (0) : Rtt_TRACE_SIM( expr ) )
#else
	#define Rtt_TRACE_SIM( expr )
	#define Rtt_WARN_SIM( cond, expr )
#endif

#if defined( Rtt_DEBUG ) 
	#define Rtt_TRACE( expr )		Rtt_Log expr
#else
	#define Rtt_TRACE( expr )
#endif

#define Rtt_PRINT( expr )			printf expr
#define Rtt_ERROR( expr )			Rtt_TRACE_SIM( expr )

// ----------------------------------------------------------------------------

#ifdef Rtt_DEBUG

	Rtt_EXPORT int Rtt_Verify( int cond, const char *reason, const char *func, const char *file, int line );

	#define Rtt_VERIFY( cond )	Rtt_Verify( 0 != (cond), #cond, Rtt_FUNCTION, __FILE__, __LINE__ )
	#define Rtt_ASSERT( cond )	(void)Rtt_VERIFY( cond )
	#define Rtt_ASSERT_MSG( cond, msg )	(void)Rtt_Verify( 0 != (cond), msg, Rtt_FUNCTION, __FILE__, __LINE__ )

#else

	#define Rtt_VERIFY( cond )	( cond )
	#define Rtt_ASSERT( cond )
	#define Rtt_ASSERT_MSG( cond, msg )

#endif

// ----------------------------------------------------------------------------

#if defined( Rtt_DEBUG )

	#define Rtt_STATIC_ASSERT( condition )	\
		typedef char Rtt_STATIC_ASSERT_##__FILE__##__LINE__[ (condition) ? 1 : -1 ]																\

#else

	#define Rtt_STATIC_ASSERT( condition )

#endif

// ----------------------------------------------------------------------------

#define Rtt_ASSERT_NOT_REACHED( )		Rtt_ASSERT_MSG( 0, "Code should NOT be reached" )
#define Rtt_ASSERT_NOT_IMPLEMENTED( )	Rtt_ASSERT_MSG( 0, "Code NOT implemented" )

// ----------------------------------------------------------------------------

#endif // _Rtt_Assert_H__
