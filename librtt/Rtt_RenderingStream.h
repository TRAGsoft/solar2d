//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_RenderingStream_H__
#define _Rtt_RenderingStream_H__

// ----------------------------------------------------------------------------

#include "Display/Rtt_Paint.h"
#include "Rtt_GPU.h"

#include "Core/Rtt_Geometry.h"
#include "Core/Rtt_Types.h"

#include "Display/Rtt_Display.h"
#include "Renderer/Rtt_RenderTypes.h"

#include "Rtt_SurfaceInfo.h"

// ----------------------------------------------------------------------------

namespace Rtt
{
class BitmapPaint;
class BufferBitmap;
class Paint;
class PlatformSurface;
class SurfaceProperties;
class VertexCache;
class VertexArray;

// ----------------------------------------------------------------------------

class RenderingStream
{
	public:
		typedef RenderingStream Self;

		typedef enum _Properties
		{
			kInitialized = 0x1,
			kFlipHorizontalAxis = 0x2,
			kFlipVerticalAxis = 0x4,
		}
		Properties;

	public:
		static int GetMaxTextureUnits();

	public:
		static GLenum GetDataType();

	public:
		RenderingStream( Rtt_Allocator* );
		virtual ~RenderingStream();

		// Call to initialize geometry properties. Should be called before SetOptimalContentSize().
		// 
		// To support dynamic content scaling, the rendering stream needs to know 
		// how to scale up the content so that it fills the window. The renderer
		// only needs to know the rendered content bounds
		void SetContentSizeRestrictions( S32 minContentWidth, S32 maxContentWidth, S32 minContentHeight, S32 maxContentHeight );

		const Rect& GetScreenContentBounds() const { return fScreenContentBounds; }
	protected:
		Rect& GetScreenContentBounds() { return fScreenContentBounds; }

	public:

		// Assumes you pass screen coordinates, not content coordinates (most code is in content coordinates)
		virtual void CaptureFrameBuffer( BufferBitmap& outBuffer, S32 xScreen, S32 yScreen, S32 wScreen, S32 hScreen );

	public:
		S32 DeviceWidth() const { return fDeviceWidth; }
		S32 DeviceHeight() const { return fDeviceHeight; }

		S32 ContentWidth() const { return fContentWidth; }
		S32 ContentHeight() const { return fContentHeight; }
		S32 MinContentWidth() const { return fMinContentWidth; }
		S32 MinContentHeight() const { return fMinContentHeight; }
		S32 MaxContentWidth() const { return fMaxContentWidth; }
		S32 MaxContentHeight() const { return fMaxContentHeight; }
		S32 ScaledContentWidth() const { return fScaledContentWidth; }
		S32 ScaledContentHeight() const { return fScaledContentHeight; }
	
		Real GetScreenToContentScale() const { return fScreenToContentScale; }
		S32 GetContentToScreenScale() const { return fContentToScreenScale; }

		S32 GetXScreenOffset() const { return fXScreenOffset; }
		S32 GetYScreenOffset() const { return fYScreenOffset; }

		void ContentToScreenUnrounded( float& x, float& y ) const;
		void ContentToScreenUnrounded( float& x, float& y, float& w, float& h ) const;

		void ContentToScreen( S32& x, S32& y ) const;
		void ContentToScreen( S32& x, S32& y, S32& w, S32& h ) const;
		void ContentToScreen( Rtt_Real& x, Rtt_Real& y, Rtt_Real& w, Rtt_Real& h ) const;
		void ContentToPixels( S32& x, S32& y ) const;
		void ContentToPixels( S32& x, S32& y, S32& w, S32& h ) const;

		// Call this method when the window size changes
		void SetOptimalContentSize( S32 deviceWidth, S32 deviceHeight, bool isPhone );

	public:
		Rtt_FORCE_INLINE bool IsProperty( U32 mask ) const { return (fProperties & mask) != 0; }
		Rtt_INLINE void ToggleProperty( U32 mask ) { fProperties ^= mask; }
		void SetProperty( U32 mask, bool value );

	private:
		GLint fDeviceWidth;
		GLint fDeviceHeight;
		S32 fContentWidth;
		S32 fContentHeight;
		S32 fScaledContentWidth;
		S32 fScaledContentHeight;
	
		S32 fMinContentWidth;
		S32 fMaxContentWidth;
		S32 fMinContentHeight;
		S32 fMaxContentHeight;

		Real fScreenToContentScale;
		S32 fContentToScreenScale;

		S32 fXScreenOffset;
		S32 fYScreenOffset;
		
		U8 fProperties;

		Rect fScreenContentBounds; // The rect of the screen in content coordinates

	protected:
		Rtt_Allocator* fAllocator;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_RenderingStream_H__
