//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Display.h"

#include "Core/Rtt_Geometry.h"
#include "Display/Rtt_CPUResourcePool.h"
#include "Display/Rtt_DisplayDefaults.h"
#include "Display/Rtt_BitmapPaint.h"
#include "Display/Rtt_Paint.h"
#include "Display/Rtt_Scene.h"
#include "Display/Rtt_ShaderFactory.h"
#include "Display/Rtt_SpritePlayer.h"
#include "Display/Rtt_TextureFactory.h"
#include "Display/Rtt_TextureResource.h"

#include "Rtt_BufferBitmap.h"
#include "Rtt_Lua.h"
#include "Rtt_LuaContext.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_PlatformSurface.h"
#include "Rtt_Profiling.h"
#include "CoronaLua.h"

#include "Renderer/Rtt_GLRenderer.h"
#include "Renderer/Rtt_VulkanExports.h"
#include "Renderer/Rtt_FrameBufferObject.h"
#include "Renderer/Rtt_Matrix_Renderer.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_Texture.h"

// TODO: Remove when we replace TemporaryHackStream
#include "Rtt_RenderingStream.h"

// TODO: Remove dependency on Runtime's MCachedResourceLibrary interface
#include "Rtt_Runtime.h"

#define ENABLE_DEBUG_PRINT    0


// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Display::Display( Runtime& owner )
:	fOwner( owner ),
	fDefaults( Rtt_NEW( owner.Allocator(), DisplayDefaults ) ),
	fDeltaTimeInSeconds( 0.0f ),
	fPreviousTime( owner.GetElapsedTime() ),
	fRenderer( NULL ),
	fShaderFactory( NULL ),
	fSpritePlayer( Rtt_NEW( owner.Allocator(), SpritePlayer( owner.Allocator() ) ) ),
	fTextureFactory( Rtt_NEW( owner.Allocator(), TextureFactory( * this ) ) ),
	fScene( Rtt_NEW( & owner.GetAllocator(), Scene( owner.Allocator(), * this ) ) ),
    fProfilingState( Rtt_NEW( owner.GetAllocator(), ProfilingState( owner.GetAllocator() ) ) ),
	fStream( Rtt_NEW( owner.GetAllocator(), RenderingStream( owner.GetAllocator() ) ) ),
	fScreenSurface( owner.Platform().CreateScreenSurface() ),
    fObjectFactories( LUA_REFNIL ),
    fFactoryFunc( NULL ),
	fIsCollecting( false )
{
}

Display::~Display()
{
	Paint::Finalize();

	lua_State *L = GetL();
	if ( L )
	{
        luaL_unref( L, LUA_REGISTRYINDEX, fObjectFactories );
	}

    //Needs to be done before deletes, because it uses scene etc
    fTextureFactory->ReleaseByType( TextureResource::kTextureResource_Any );
    
	Rtt_DELETE( fScreenSurface );
	Rtt_DELETE( fStream );
	Rtt_DELETE( fScene );
    Rtt_DELETE( fProfilingState );
	Rtt_DELETE( fTextureFactory );
	Rtt_DELETE( fSpritePlayer );
	Rtt_DELETE( fShaderFactory );
	Rtt_DELETE( fRenderer );
	Rtt_DELETE( fDefaults );
}

static void
InvalidateDisplay( void * display )
{
	static_cast< Display * >( display )->GetScene().Invalidate();
}

bool
Display::Initialize( lua_State *L, int configIndex, const char * backend, void * backendContext )
{
    bool result = false;

	// Only initialize once
	if ( Rtt_VERIFY( ! fRenderer ) )
	{
		Rtt_Allocator *allocator = GetRuntime().GetAllocator();

#if defined( Rtt_WIN_ENV )
		if (strcmp( backend, "glBackend" ) == 0)
		{
			fRenderer = Rtt_NEW( allocator, GLRenderer( allocator ) );
		}

		else if (strcmp( backend, "vulkanBackend" ) == 0)
		{
			fRenderer = VulkanExports::CreateVulkanRenderer( allocator, backendContext, &InvalidateDisplay, this );
		}
		else
		{
			Rtt_ASSERT_NOT_REACHED();
		}
#else
		fRenderer = Rtt_NEW( allocator, GLRenderer( allocator ) );
#endif

		fRenderer->Initialize();
		
		CPUResourcePool *resourcePoolObserver = Rtt_NEW(allocator,CPUResourcePool());
		fRenderer->SetCPUResourceObserver(resourcePoolObserver);

		ProgramHeader programHeader;
		
		if ( configIndex > 0 )
		{
			ReadRenderingConfig( L, configIndex, programHeader );
		}
		fStream->SetOptimalContentSize( fScreenSurface->Width(), fScreenSurface->Height(), IsPhone() );

		result = true;

		fShaderFactory = Rtt_NEW( allocator, ShaderFactory( *this, programHeader, backend ) );
	}

    return result;
}
    
void
Display::Teardown()
{
    GetTextureFactory().Teardown();
}

void
Display::ReadRenderingConfig( lua_State *L, int index, ProgramHeader& programHeader )
{
	Rtt_ASSERT( 1 == index );	
	Rtt_ASSERT( lua_istable( L, index ) );

    lua_getfield( L, index, "shaderPrecision" );
    const char *shaderPrecision = lua_tostring( L, -1 );
    if ( shaderPrecision )
    {
        // This is safer to do than strcpy( outShaderPrecision, shaderPrecision, );
        ProgramHeader::Precision p = ProgramHeader::PrecisionForString( shaderPrecision );
        programHeader.SetPrecision( p );
    }
    else if ( lua_type( L, -1 ) == LUA_TTABLE )
    {
        int index = lua_gettop( L );

        lua_pushnil( L );
        while ( lua_next( L, index ) )
        {
            const char *key = lua_tostring( L, -2 );
            ProgramHeader::Type t = ProgramHeader::TypeForString( key );

            const char *value = lua_tostring( L, -1 );
            ProgramHeader::Precision p = ProgramHeader::PrecisionForString( value );

            programHeader.SetPrecision( t, p );

            lua_pop( L, 1 );
        }
    }
    lua_pop( L, 1 );

	lua_getfield( L, index, "minContentWidth" );
	int minContentWidth = (int) lua_tointeger( L, -1 );
	lua_pop( L, 1 );

	lua_getfield( L, index, "maxContentWidth" );
	int maxContentWidth = (int) lua_tointeger( L, -1 );
	lua_pop( L, 1 );

	lua_getfield( L, index, "minContentHeight" );
	int minContentHeight = (int) lua_tointeger( L, -1 );
	lua_pop( L, 1 );
	
	lua_getfield( L, index, "maxContentHeight" );
	int maxContentHeight = (int) lua_tointeger( L, -1 );
	lua_pop( L, 1 );

	fStream->SetContentSizeRestrictions( minContentWidth, maxContentWidth, minContentHeight, maxContentHeight );

	Rtt_ASSERT( 1 == lua_gettop( L ) );	
}

bool
Display::IsPhone() const
{
	return GetRuntime().Platform().GetDevice().IsPhone();
}

lua_State *
Display::GetL() const
{
    const Runtime& runtime = GetRuntime();
    lua_State *L = ( runtime.IsVMContextValid() ? runtime.VMContext().L() : NULL );
    return L;
}

void
Display::Start()
{
	// The call to SetOptimalContentSize requires a current OpenGL context which we can't assume yet.
	// RuntimeGuard will ensure that an OpenGL context is current and that locks are in place for multithreaded conditions.
	// We must do this inside a scope because the RuntimeGuard destructor is relied on to do unlocking and restoring of OpenGL state as necessary.
	// The overloaded operator()() below uses this same RuntimeGuard technique.
	// TODO: We may not need this scope guard.
	// The runtime guard is re-entrant safe so it should be safe to leave guarded when we hit the operator()().
	RuntimeGuard guard( GetRuntime() );

	RenderingStream& stream = * fStream;
	stream.SetOptimalContentSize( fScreenSurface->Width(), fScreenSurface->Height(), IsPhone() );
}
void
Display::Restart()
{
	RenderingStream& stream = * fStream;
	stream.SetOptimalContentSize( fScreenSurface->Width(), fScreenSurface->Height(), IsPhone() );
}
	
void
Display::Update()
{
    PROFILING_BEGIN( *GetProfilingState(), up, Update );

    up.Add( "Display::Update Begin" );
    
    // Calculate delta time per logic tick, not per render tick.
    // Prior to render/logic decoupling, this lived in Render() because both
    // fired together in the same tick — there was no distinction between them.
    // With fFrameSync enabled on high-refresh displays, Render() now fires
    // at the monitor refresh rate (e.g. 120Hz) while Update() fires at the
    // configured logic rate (e.g. 60fps). Calculating delta time in Render()
    // caused fDeltaTimeInSeconds to reflect the render interval (~8.33ms at
    // 120Hz) rather than the logic interval (~16.67ms at 60fps), making
    // emitter particles live and move at half speed on high-refresh displays.
    // Moving it here ensures fDeltaTimeInSeconds always reflects the time
    // between logic ticks regardless of render rate.
    {
        Rtt_AbsoluteTime elapsedTime = GetRuntime().GetElapsedTime();

        const Rtt::Real kMillisecondsPerSecond = 1000.0f;
        // NOT _USED: Rtt::Real totalTime = Rtt_AbsoluteToMilliseconds( elapsedTime ) / kMillisecondsPerSecond;
        fDeltaTimeInSeconds = Rtt_AbsoluteToMilliseconds(elapsedTime - fPreviousTime) / kMillisecondsPerSecond;

        fPreviousTime = elapsedTime;

        // Use this when debugging:
        //fDeltaTimeInSeconds = ( 1.0f / 30.0f );
    }


    Runtime& runtime = fOwner;
    lua_State *L = fOwner.VMContext().L();
    fSpritePlayer->Run( L, Rtt_AbsoluteToMilliseconds(runtime.GetElapsedTime()) );

	up.Add( "Prepare for frame event" );

    const FrameEvent& fe = FrameEvent::Constant();
    fe.Dispatch( L, runtime );
    
    up.Add( "FrameEvent" );
    
	const RenderEvent& re = RenderEvent::Constant();
	re.Dispatch( L, runtime );

    up.Add( "LateUpdate" );
    
	Profiling::ResetSums();

    up.Add( "Display::Update End" );
}

void
Display::Render()
{
    PROFILING_BEGIN( *GetProfilingState(), rp, Render );

    rp.Add( "Display::Render Begin" );
    // fDeltaTimeInSeconds is now calculated in Update().
	GetScene().Render( * fRenderer, * fScreenSurface, &rp );

    rp.Add( "Display::Render End" );
}

void
Display::Invalidate()
{
    GetScene().Invalidate();
}

StageObject *
Display::GetStage()
{
    return & GetScene().CurrentStage();
}

StageObject *
Display::GetStageOffscreen()
{
    return & GetScene().OffscreenStage();
}

BitmapPaint *
Display::CaptureScreen()
{
	return Capture( NULL,
					NULL,
					false,
					false,
					NULL,
					NULL );
}

BitmapPaint *
Display::CaptureBounds( Rect *screenBounds )
{
	return Capture( NULL,
					screenBounds,
					false,
					false,
					NULL,
					NULL );
}

BitmapPaint *
Display::CaptureDisplayObject( DisplayObject *object, bool crop_object_to_screen_bounds )
{
	return Capture( object,
					NULL,
					false,
					crop_object_to_screen_bounds,
					NULL,
					NULL );
}

BitmapPaint *
Display::CaptureSave( DisplayObject *object,
						bool crop_object_to_screen_bounds,
						const ColorUnion *optionalBackgroundColor )
{
	return Capture( object,
					NULL,
					true,
					crop_object_to_screen_bounds,
					optionalBackgroundColor,
					NULL );
}

void
Display::ColorSample( float pos_x,
                        float pos_y,
                        RGBA &output_color )
{
    Rect screenBounds;
    screenBounds.Initialize( pos_x, pos_y, 1.0f, 1.0f );

	BitmapPaint *paint = Capture( NULL,
									&screenBounds,
									true,
									false,
									NULL,
									&output_color );
	if( ! paint )
	{
		Rtt_TRACE( ( "Unable to capture screen. The platform or device might not be supported." ) );
		return;
	}

    Rtt_DELETE( paint );
}

BitmapPaint *
Display::Capture( DisplayObject *object,
					Rect *screenBounds,
					bool will_be_saved_to_file,
					bool crop_object_to_screen_bounds,
					const ColorUnion *optionalBackgroundColor,
					RGBA *optional_output_color )
{
    // Do not continue if given invalid screen bounds.
    if( screenBounds )
    {
        if ((screenBounds->xMin >= screenBounds->xMax) ||
            (screenBounds->yMin >= screenBounds->yMax))
        {
            return NULL;
        }
    }

	fRenderer->BeginFrame( 0.1f, 0.1f, GetDefaults().GetTimeTransform(), GetScreenToContentScale(), true );

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    Runtime& runtime = GetRuntime();
    Rtt_Allocator *allocator = runtime.GetAllocator();

    // Save current state so we can restore it later
    FrameBufferObject *previous_fbo = fRenderer->GetFrameBufferObject();
    FrameBufferObject *fbo = NULL;

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////
    //FBO SETUP

    // Calculate pixel dimensions for texture
    // The source position to capture from.
    Real x_in_content_units = 0.0f;
    Real y_in_content_units = 0.0f;
    Real w_in_content_units = 0.0f;
    Real h_in_content_units = 0.0f;

	Rtt::Real offscreenViewMatrix[16];
	Rtt::CreateViewMatrix( 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, offscreenViewMatrix );

	Rtt::Real offscreenProjMatrix[16];
	if ( object )
	{
		// Calculate the bounds of the given display object in pixels.
		// Clip the object to the screen's bounds.
		Rect objectBounds = object->StageBounds();
		if (screenBounds)
		{
			objectBounds.Intersect(*screenBounds);
		}
		else
		{
			if( crop_object_to_screen_bounds )
			{
				objectBounds.Intersect(GetScreenContentBounds());
			}
		}

		x_in_content_units = objectBounds.xMin;
		y_in_content_units = objectBounds.yMin;
		w_in_content_units = ( objectBounds.xMax - x_in_content_units );
		h_in_content_units = ( objectBounds.yMax - y_in_content_units );
		Rtt::CreateOrthoMatrix( x_in_content_units, objectBounds.xMax, y_in_content_units, objectBounds.yMax, 0.0f, 1.0f, offscreenProjMatrix );
	}
	else // Full screen or screenBounds.
	{
		Rect *full_screen = const_cast< Rect * >( &GetScreenContentBounds() );

        Rect *bounds_to_use = NULL;

        if( screenBounds )
        {
            // Subregion of the screen.
            screenBounds->Intersect( *full_screen );
            bounds_to_use = screenBounds;
        }
        else
        {
            // Full screen.
            bounds_to_use = full_screen;
        }

        x_in_content_units = bounds_to_use->xMin;
        y_in_content_units = bounds_to_use->yMin;
        w_in_content_units = ( bounds_to_use->xMax - x_in_content_units );
        h_in_content_units = ( bounds_to_use->yMax - y_in_content_units );

		Rtt::CreateOrthoMatrix( x_in_content_units, bounds_to_use->xMax, y_in_content_units, bounds_to_use->yMax, 0.0f, 1.0f, offscreenProjMatrix );
	}

    // The source position to capture from.
    Rtt_Real x_in_pixels = 0;
    Rtt_Real y_in_pixels = 0;
    Rtt_Real w_in_pixels = w_in_content_units;
    Rtt_Real h_in_pixels = h_in_content_units;
    if(!optional_output_color){

        ContentToScreen( x_in_pixels,
                            y_in_pixels,
                            w_in_pixels,
                            h_in_pixels );

        // We ONLY want the w_in_pixels and h_in_pixels.
        // Using anything but 0 here breaks Android.
        x_in_pixels = 0;
        y_in_pixels = 0;
    }
#    if defined( Rtt_OPENGLES )
        const Texture::Format kFormat = Texture::kRGBA;
#    else
        const Texture::Format kFormat = Texture::kBGRA;
#    endif

    // Using Texture::kNearest and Texture::kClampToEdge here is absolutely
    // mandatory. See:
    //
    //    http://www.khronos.org/registry/gles/extensions/APPLE/APPLE_texture_2D_limited_npot.txt
    //
    //        "A NPOT 2D texture with a wrap mode that is not CLAMP_TO_EDGE or
    //        a minfilter that is not NEAREST or LINEAR is considered incomplete."
    TextureFactory &factory = GetTextureFactory();
    SharedPtr< TextureResource > tex = factory.Create( w_in_pixels,
                                                        h_in_pixels,
                                                        kFormat,
                                                        Texture::kNearest,
                                                        Texture::kClampToEdge,
                                                        will_be_saved_to_file );

    BitmapPaint *paint = Rtt_NEW( allocator,
                                    BitmapPaint( tex ) );

    fbo = Rtt_NEW( allocator,
                    FrameBufferObject( allocator,
                                        paint->GetTexture() ) );

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////
    //FROM SnapshotObject::Draw()
    //PREPARE TO DRAW!!!!!

    //// Save these so we can restore them later.
    //
    Rtt::Real previous_viewMatrix[16];
    Rtt::Real previous_projMatrix[16];
    fRenderer->GetFrustum( previous_viewMatrix, previous_projMatrix );

    S32 previous_viewport_x, previous_viewport_y, previous_viewport_width, previous_viewport_height;
    fRenderer->GetViewport( previous_viewport_x, previous_viewport_y, previous_viewport_width, previous_viewport_height );
    //
    ////

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////
    //SCENE RENDER

    Scene& scene = GetScene();

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////
    //FROM SnapshotObject::Draw()
    //PREPARE TO DRAW!!!!!
    Real texW = Rtt_IntToReal( fbo->GetTexture()->GetWidth() );
    Real texH = Rtt_IntToReal( fbo->GetTexture()->GetHeight() );

    fRenderer->SetFrameBufferObject( fbo );
    fRenderer->PushMaskCount();
    fRenderer->SetViewport( 0, 0, texW, texH );

    Renderer::ExtraClearOptions extra;
    
    extra.clearDepth = GetDefaults().GetEnableDepthInScene();
    extra.clearStencil = GetDefaults().GetEnableStencilInScene();
    extra.depthClearValue = GetDefaults().GetSceneDepthClearValue();
    extra.stencilClearValue = GetDefaults().GetSceneStencilClearValue();

    fRenderer->Clear( 0.0f, 0.0f, 0.0f, 0.0f, &extra );

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////
    //FROM THIS ORIGINAL FUNCTION MADE BUILDABLE!!!!!!!
    //DRAW!!!!!

    // Fetch the bounds of the portion of the screen that we are
    // going to capture and re-render that part of the screen.
    if ( object )
    {
        // Set the background color to transparent.
        ColorUnion previousClearColor;
        ColorUnion transparentClearColor;

		if( optionalBackgroundColor )
		{
			transparentClearColor.rgba = optionalBackgroundColor->rgba;
		}
		else
		{
			transparentClearColor.rgba.r = 0;
			transparentClearColor.rgba.g = 0;
			transparentClearColor.rgba.b = 0;
			transparentClearColor.rgba.a = 0;
		}

        previousClearColor.pixel = GetDefaults().GetClearColor();
        GetDefaults().SetClearColor( transparentClearColor.pixel );

        fRenderer->SetFrustum( offscreenViewMatrix, offscreenProjMatrix );

		const Rect* snb = GetStage()->GetSnapshotBounds();
		Rect empty;
		if (!screenBounds && object && !crop_object_to_screen_bounds)
		{
			// this will disable culling of ofscreen children
			GetStage()->SetSnapshotBounds(&empty);
		}
		// Render only the given object and its children, if any, to be captured down below.
		scene.Render( *fRenderer, *fScreenSurface, *object );
		if (!screenBounds && object && !crop_object_to_screen_bounds)
		{
			GetStage()->SetSnapshotBounds(snb);
		}

        // Restore background back to its previous color.
        GetDefaults().SetClearColor( previousClearColor.pixel );
    }
    else // Full screen or screenBounds.
    {
        fRenderer->SetFrustum( offscreenViewMatrix, offscreenProjMatrix );

		scene.Render( *fRenderer, *fScreenSurface, scene.CurrentStage() );
	}

    fRenderer->PopMaskCount();

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////
    //FBO CLEANUP

    fRenderer->EndFrame();
    fRenderer->Swap();
    fRenderer->Render();

    if( will_be_saved_to_file ||
        optional_output_color )
    {
        // Create screen capture.
        BufferBitmap *bitmap = static_cast< BufferBitmap * >( tex->GetBitmap() );

		// This function requires coordinates in pixels.
		fRenderer->CaptureFrameBuffer( *fStream, *bitmap,
										x_in_pixels,
										y_in_pixels,
										w_in_pixels,
										h_in_pixels );
		
#if !defined(Rtt_NXS_ENV)
		bitmap->UndoPremultipliedAlpha();
#endif

        if( optional_output_color )
        {
            // We want the RGBA value of the first pixel.

            const unsigned char *bytes = static_cast< const unsigned char * >( bitmap->ReadAccess() );

#            ifdef Rtt_OPENGLES

                // IMPORTANT: We're assuming the format is GL_RGBA and GL_UNSIGNED_BYTE.
                optional_output_color->r = bytes[ 0 ];
                optional_output_color->g = bytes[ 1 ];
                optional_output_color->b = bytes[ 2 ];
                optional_output_color->a = bytes[ 3 ];

#            else // Not Rtt_OPENGLES

                // IMPORTANT: We're assuming the format is GL_ARGB and GL_UNSIGNED_BYTE.
                optional_output_color->r = bytes[ 1 ];
                optional_output_color->g = bytes[ 2 ];
                optional_output_color->b = bytes[ 3 ];
                optional_output_color->a = bytes[ 0 ];

#            endif // Rtt_OPENGLES
        }
    }

    // Restore state so further rendering is unaffected
    fRenderer->SetViewport( previous_viewport_x, previous_viewport_y, previous_viewport_width, previous_viewport_height );
    fRenderer->SetFrustum( previous_viewMatrix, previous_projMatrix );
    fRenderer->SetFrameBufferObject( previous_fbo );

#    if ENABLE_DEBUG_PRINT

        printf( "capture bounds in content units: x: %d y: %d w: %d h: %d\n",
                x_in_content_units,
                y_in_content_units,
                w_in_content_units,
                h_in_content_units );
        printf( "capture bounds in pixels: x: %d y: %d w: %d h: %d\n",
                x_in_pixels,
                y_in_pixels,
                w_in_pixels,
                h_in_pixels );

#	endif // ENABLE_DEBUG_PRINT
		
	fRenderer->EndCapture();

	Rtt_DELETE( fbo );

    // If object was just created this will draw it to main scene as well, not only to FBO
    scene.Invalidate();

    return paint;
}

void
Display::UnloadResources()
{
	GetRenderer().ReleaseGPUResources();
}

void
Display::ReloadResources()
{
	GetRenderer().ReleaseGPUResources();
	GetRenderer().Initialize();
}


void
Display::GatherObjectFactories( const luaL_Reg funcs[], void * library )
{
    lua_State *L = GetL();

    if ( L && LUA_REFNIL == fObjectFactories )
    {
        lua_newtable( L );

        for (int i = 0; funcs[i].func; ++i)
        {
            if (strncmp(funcs[i].name, "new", 3U) == 0)
            {
                lua_pushlightuserdata( L, library );
                lua_pushnil( L );
                lua_pushcclosure( L, funcs[i].func, 2 );
                lua_setfield( L, -2, funcs[i].name );
            }
        }

        fObjectFactories = luaL_ref( L, LUA_REGISTRYINDEX );
    }
}

bool
Display::PushObjectFactories() const
{
    bool wasPushed = false;
    if (LUA_REFNIL != fObjectFactories)
    {
        lua_State *L = GetL();
        if (L)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, fObjectFactories);
            wasPushed = true;
        }
    }
    return wasPushed;
}

GroupObject *
Display::Orphanage()
{
    return & GetScene().Orphanage();
}

GroupObject *
Display::HitTestOrphanage()
{
    return & GetScene().SnapshotOrphanage();
}

S32
Display::DeviceWidth() const
{
	return fScreenSurface->Width();
}

S32
Display::DeviceHeight() const
{
	return fScreenSurface->Height();
}

S32
Display::ContentWidth() const
{
	return fStream->ContentWidth();
}

S32
Display::ContentHeight() const
{
	return fStream->ContentHeight();
}

S32
Display::PointsWidth() const
{
	return fScreenSurface->PointsWidth();
}

S32
Display::PointsHeight() const
{
	return fScreenSurface->PointsHeight();
}

S32
Display::MinContentWidth() const
{
	return fStream->MinContentWidth();
}

S32
Display::MinContentHeight() const
{
	return fStream->MinContentHeight();
}

S32
Display::MaxContentWidth() const
{
	return fStream->MaxContentWidth();
}

S32
Display::MaxContentHeight() const
{
	return fStream->MaxContentHeight();
}


S32
Display::ScaledContentWidth() const
{
    return fStream->ScaledContentWidth();
}

S32
Display::ScaledContentHeight() const
{
    return fStream->ScaledContentHeight();
}


Real
Display::GetScreenToContentScale() const
{
	return fStream->GetScreenToContentScale();
}

S32
Display::GetContentToScreenScale() const
{
	return fStream->GetContentToScreenScale();
}

S32
Display::GetXScreenOffset() const
{
	return fStream->GetXScreenOffset();
}

S32
Display::GetYScreenOffset() const
{
	return fStream->GetYScreenOffset();
}

void
Display::ContentToScreenUnrounded( float& x, float& y ) const
{
    float w = 0;
    float h = 0;
    ContentToScreenUnrounded( x, y, w, h );
}

void
Display::ContentToScreenUnrounded( float& x, float& y, float& w, float& h ) const
{
    fStream->ContentToScreenUnrounded( x, y, w, h );
}

void
Display::ContentToScreen( S32& x, S32& y ) const
{
    S32 w = 0;
    S32 h = 0;
    fStream->ContentToScreen( x, y, w, h );
}

void
Display::ContentToScreen( Rtt_Real& x, Rtt_Real& y, Rtt_Real& w, Rtt_Real& h ) const
{
    fStream->ContentToScreen( x, y, w, h );
}

void
Display::ContentToScreen( S32& x, S32& y, S32& w, S32& h ) const
{
    fStream->ContentToScreen( x, y, w, h );
}

void
Display::ContentToPixels( S32& x, S32& y, S32& w, S32& h ) const
{
    fStream->ContentToPixels( x, y, w, h );
}

void
Display::GetContentRect( Rect& outRect ) const
{
    const Vertex2 minCorner = { Rtt_REAL_0, Rtt_REAL_0 };
    const Vertex2 maxCorner = { Rtt_IntToReal( ContentWidth() ), Rtt_IntToReal( ContentHeight() ) };
    const Rect windowRect( minCorner, maxCorner );
    outRect = windowRect;
}

const Rect&
Display::GetScreenContentBounds() const
{
    return ((const RenderingStream *)fStream)->GetScreenContentBounds();
}

void
Display::WindowSizeChanged()
{
	const Runtime& runtime = GetRuntime();
	runtime.Begin();
	{
		RenderingStream *stream = fStream;
		if ( stream ) // Is this check neccesary?
		{
			stream->SetOptimalContentSize( fScreenSurface->Width(), fScreenSurface->Height(), IsPhone() );
		}
	}
	runtime.End();
}

bool
Display::HasWindowSizeChanged() const
{
	if (!fStream || !fScreenSurface)
	{
		return false;
	}
	return ((fStream->DeviceWidth() != fScreenSurface->Width()) || (fStream->DeviceHeight() != fScreenSurface->Height()));
}

Rtt_Allocator *
Display::GetAllocator() const
{
    return fOwner.GetAllocator();
}

Rtt_AbsoluteTime
Display::GetElapsedTime() const
{
    return fOwner.GetElapsedTime();
}

U32
Display::GetMaxTextureSize()
{
    U32 result = 1024;
    result = Renderer::GetMaxTextureSize();
    Rtt_ASSERT( result > 0 );
    return result;
}

const char *
Display::GetGlString( const char *s )
{
    return Renderer::GetGlString( s );
}

bool
Display::GetGpuSupportsHighPrecisionFragmentShaders()
{
    return Renderer::GetGpuSupportsHighPrecisionFragmentShaders();
}

U32
Display::GetMaxUniformVectorsCount()
{
    return Renderer::GetMaxUniformVectorsCount();
}

U32
Display::GetMaxVertexTextureUnits()
{
    return Renderer::GetMaxVertexTextureUnits();
}

bool
Display::HasFramebufferBlit( bool * canScale ) const
{
    return fRenderer->HasFramebufferBlit( canScale );
}

void
Display::GetVertexAttributes( VertexAttributeSupport & support ) const
{
    fRenderer->GetVertexAttributes( support );
}

void
Display::Collect( lua_State *L )
{
    // Protect against re-entrancy (not thread safe)
    if ( ! fIsCollecting )
    {
        fIsCollecting = true;
        GroupObject::CollectUnreachables( L, GetScene(), * Orphanage() );
        fIsCollecting = false;
    }
}


// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

