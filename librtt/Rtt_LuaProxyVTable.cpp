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

#include "Rtt_LuaProxyVTable.h"

#include "Rtt_FilePath.h"
#include "Display/Rtt_BitmapMask.h"
#include "Display/Rtt_BitmapPaint.h"
#include "Display/Rtt_ClosedPath.h"
#include "Display/Rtt_ContainerObject.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_DisplayDefaults.h"
#include "Display/Rtt_DisplayObject.h"
#include "Display/Rtt_GradientPaint.h"
#include "Display/Rtt_LuaLibDisplay.h"
#include "Display/Rtt_Paint.h"
#include "Display/Rtt_RectPath.h"
#include "Display/Rtt_Shader.h"
#include "Display/Rtt_ShaderFactory.h"
#include "Display/Rtt_ShapeObject.h"
#include "Display/Rtt_SnapshotObject.h"
#include "Display/Rtt_SpriteObject.h"
#include "Display/Rtt_StageObject.h"
#include "Display/Rtt_TextureFactory.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaProxy.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_PlatformDisplayObject.h"
#include "Rtt_Runtime.h"
#include "CoronaLua.h"

#include "Core/Rtt_StringHash.h"

#include <string.h>

#include "Rtt_Lua.h"
#include "Rtt_Profiling.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------
bool
LuaProxyVTable::SetValueForKey( lua_State *, MLuaProxyable&, const char [], int ) const
{
    return false;
}

const LuaProxyVTable&
LuaProxyVTable::Parent() const
{
    return * this;
}

// ----------------------------------------------------------------------------

#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
// Proxy's delegate or an ancestor must match expected
bool
LuaProxyVTable::IsProxyUsingCompatibleDelegate( const LuaProxy* proxy, const Self& expected )
{
    // if proxy is NULL, skip the check
    bool result = ( NULL == proxy );

    if ( ! result )
    {
        for( const LuaProxyVTable *child = & proxy->Delegate(), *parent = & child->Parent();
             ! result;
             child = parent, parent = & child->Parent() )
        {
            result = ( child == & expected );
            if ( child == parent ) { break; }
        }
    }

    return result;
}
#endif // Rtt_DEBUG

// This implements introspection
bool
LuaProxyVTable::DumpObjectProperties( lua_State *L, const MLuaProxyable& object, const char **keys, const int numKeys, String& result ) const
{
    Rtt_LUA_STACK_GUARD( L );
    const int bufLen = 10240;
    char buf[bufLen];

    // JSON encode the value of each key
    for (int k = 0; k < numKeys; k++)
    {
        Rtt_LUA_STACK_GUARD( L );

        if (strchr(keys[k], '#'))
        {
            // Deprecated property, skip it
            continue;
        }

        int res = ValueForKey(L, object, keys[k]);
        if (res > 0)
        {
            buf[0] = '\0';

            CoronaLuaPropertyToJSON(L, -1, keys[k], buf, bufLen, 0);

            if (! result.IsEmpty() && strlen(buf) > 0)
            {
                result.Append(", ");
            }

            result.Append(buf);

            lua_pop( L, res );
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
//
// LuaDisplayObjectProxyVTable
//
// ----------------------------------------------------------------------------
const LuaDisplayObjectProxyVTable&
LuaDisplayObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

int
LuaDisplayObjectProxyVTable::translate( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );
    if ( o )
    {
        Real x = luaL_checkreal( L, 2 );
        Real y = luaL_checkreal( L, 3 );

        o->Translate( x, y );
    }

    return 0;
}

int
LuaDisplayObjectProxyVTable::scale( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( o )
    {
        Real sx = luaL_checkreal( L, 2 );
        Real sy = luaL_checkreal( L, 3 );

        o->Scale( sx, sy, false );
    }

    return 0;
}

int
LuaDisplayObjectProxyVTable::rotate( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( o )
    {
        Real deltaTheta = luaL_checkreal( L, 2 );

        o->Rotate( deltaTheta );
    }

    return 0;
}

static int
getParent( lua_State *L )
{
#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
    CoronaLuaWarning(L, "[Deprecated display object API] Replace object:getParent() with object.parent");
#endif

    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    int result = o ? 1 : 0;
    if ( o )
    {
        // Orphans do not have parents
        GroupObject* parent = ( ! o->IsOrphan() ? o->GetParent() : NULL );
        if ( parent )
        {
            parent->GetProxy()->PushTable( L );
        }
        else
        {
            Rtt_ASSERT( o->GetStage() == o || o->IsOrphan() );
            lua_pushnil( L );
        }
    }

    return result;
}

// object:removeSelf()
static int
removeSelf( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );
    int result = 0;

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( o )
    {
        if ( ! o->IsRenderedOffScreen() )
        {
            GroupObject* parent = o->GetParent();

            if (parent != NULL)
            {
                S32 index = parent->Find( *o );

                LuaDisplayObjectProxyVTable::PushAndRemove( L, parent, index );

                result = 1;
            }
            else
            {
#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
                CoronaLuaWarning(L, "object:removeSelf() cannot be called on objects with no parent" );
#endif
            }
        }
        else
        {
#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
            CoronaLuaWarning(L, "object:removeSelf() can only be called on objects in the scene graph. Objects that are not directly in the scene, such as a snapshot's group, cannot be removed directly" );
#endif
        }
    }
    else
    {
        lua_pushnil( L );
        result = 1;
    }

    return result;
}

static int
localToContent( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    int result = 0;
    if ( o )
    {
        Real x = luaL_checkreal( L, 2 );
        Real y = luaL_checkreal( L, 3 );

        Vertex2 v = { x, y };
        o->LocalToContent( v );

        lua_pushnumber( L, v.x );
        lua_pushnumber( L, v.y );
        result = 2;
    }

    return result;
}

static int
contentToLocal( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( Rtt_VERIFY( o ) )
    {
        Vertex2 v = { luaL_toreal( L, 2 ), luaL_toreal( L, 3 ) };
        o->ContentToLocal( v );
        lua_pushnumber( L, Rtt_RealToFloat( v.x ) );
        lua_pushnumber( L, Rtt_RealToFloat( v.y ) );
    }

    return 2;
}

// object:toFront()
static int
toFront( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( Rtt_VERIFY( o ) )
    {
        GroupObject *parent = o->GetParent();

        if (parent != NULL)
        {
            parent->Insert( -1, o, false );
        }
#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
        else
        {
            CoronaLuaWarning(L, "DisplayObject:toFront() cannot be used on a snapshot group or texture canvas cache");
        }
#endif
    }
    return 0;
}

// object:toBack()
static int
toBack( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( Rtt_VERIFY( o ) )
    {
        GroupObject *parent = o->GetParent();

        if (parent != NULL)
        {
            parent->Insert( 0, o, false );
        }
#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
        else
        {
            CoronaLuaWarning(L, "DisplayObject:toBack() cannot be used on a snapshot group or texture canvas cache");
        }
#endif
    }
    return 0;
}

// object:setMask( mask )
static int
setMask( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( Rtt_VERIFY( o ) )
    {
        Runtime *runtime = LuaContext::GetRuntime( L ); Rtt_ASSERT( runtime );

        BitmapMask *mask = NULL;

        if ( lua_isuserdata( L, 2 ) )
        {
            FilePath **ud = (FilePath **)luaL_checkudata( L, 2, FilePath::kMetatableName );
            if ( ud )
            {
                FilePath *maskData = *ud;
                if ( maskData )
                {
                    mask = BitmapMask::Create( * runtime, * maskData );
                }
            }
        }

        o->SetMask( runtime->Allocator(), mask );
    }
    return 0;
}

// object:_setHasListener( name, value )
static int
setHasListener( lua_State *L )
{
    DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    if ( Rtt_VERIFY( o ) )
    {
        const char *name = lua_tostring( L, 2 );
        DisplayObject::ListenerMask mask = DisplayObject::MaskForString( name );
        if ( DisplayObject::kUnknownListener != mask )
        {
            bool value = lua_toboolean( L, 3 );
            o->SetHasListener( mask, value );
        }
    }
    return 0;
}

static int
PushTouchMargins( lua_State *L, const DisplayObject& o )
{
	Real left = Rtt_REAL_0;
	Real right = Rtt_REAL_0;
	Real top = Rtt_REAL_0;
	Real bottom = Rtt_REAL_0;
	o.GetTouchMargins( left, right, top, bottom );

	lua_createtable( L, 0, 4 );
	lua_pushnumber( L, left );
	lua_setfield( L, -2, "left" );
	lua_pushnumber( L, right );
	lua_setfield( L, -2, "right" );
	lua_pushnumber( L, top );
	lua_setfield( L, -2, "top" );
	lua_pushnumber( L, bottom );
	lua_setfield( L, -2, "bottom" );

	return 1;
}

static Real
GetTouchMarginValueForKey( lua_State *L, int index, const char key[], Real defaultValue )
{
	Real result = defaultValue;

	lua_getfield( L, index, key );
	if ( lua_isnumber( L, -1 ) )
	{
		result = luaL_toreal( L, -1 );
	}
	else if ( ! lua_isnil( L, -1 ) )
	{
		luaL_error( L, "ERROR: o.touchMargins.%s can only be set to a number.\n", key );
	}
	lua_pop( L, 1 );

	return result;
}

int
LuaDisplayObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[]) const
{
    if ( ! key ) { return 0; }

    if ( 0 == strcmp( key, "touchMargins" ) )
    {
        const DisplayObject& o = static_cast< const DisplayObject& >( object );
        Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );
        return PushTouchMargins( L, o );
    }
    
    int result = 1;

    // deprecated properties have a trailing '#'
    static const char * keys[] = 
    {
        "translate",            // 0
        "scale",                // 1
        "rotate",                // 2
        "getParent",            // 3
        "setReferencePoint",    // 4
        "removeSelf",            // 5
        "localToContent",        // 6
        "contentToLocal",        // 7
        "isVisible",            // 8
        "isHitTestable",        // 9
        "alpha",                // 10
        "parent",                // 11
        "stage",                // 12
        "x",                    // 13
        "y",                    // 14
        "anchorX",                // 15
        "anchorY",                // 16
        "contentBounds",        // 17
        "contentWidth",         // 18
        "contentHeight",        // 19
        "toFront",                // 20
        "toBack",                // 21
        "setMask",                // 22
        "maskX",                // 23
        "maskY",                // 24
        "maskScaleX",            // 25
        "maskScaleY",            // 26
        "maskRotation",         // 27
        "isHitTestMasked",        // 28
        "_setHasListener",        // 29
    };
    const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 30, 29, 6, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    int index = hash->Lookup( key );
    switch ( index )
    {
    case 0:
        {
            Lua::PushCachedFunction( L, Self::translate );
        }
        break;
    case 1:
        {
            Lua::PushCachedFunction( L, Self::scale );
        }
        break;
    case 2:
        {
            Lua::PushCachedFunction( L, Self::rotate );
        }
        break;
    case 3:
        {
            Lua::PushCachedFunction( L, getParent );
        }
        break;
    case 5:
        {
            Lua::PushCachedFunction( L, removeSelf );
        }
        break;
    case 6:
        {
            Lua::PushCachedFunction( L, localToContent );
        }
        break;
    case 7:
        {
            Lua::PushCachedFunction( L, contentToLocal );
        }
        break;
    case 20:
        {
            Lua::PushCachedFunction( L, toFront );
        }
        break;
    case 21:
        {
            Lua::PushCachedFunction( L, toBack );
        }
        break;
    case 22:
        {
            Lua::PushCachedFunction( L, setMask );
        }
        break;
    case 29:
        {
            Lua::PushCachedFunction( L, setHasListener );
        }
        break;
    default:
        {
            // DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );
            const DisplayObject& o = static_cast< const DisplayObject& >( object );
            Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

            switch ( index )
            {
            case 17:
                {
                    const Rect& r = o.StageBounds();

                    lua_createtable( L, 0, 4 );

                    const char xMin[] = "xMin";
                    const char yMin[] = "yMin";
                    const char xMax[] = "xMax";
                    const char yMax[] = "yMax";
                    const size_t kLen = sizeof( xMin ) - 1;

                    Rtt_STATIC_ASSERT( sizeof(char) == 1 );
                    Rtt_STATIC_ASSERT( sizeof(xMin) == sizeof(yMin) );
                    Rtt_STATIC_ASSERT( sizeof(xMin) == sizeof(xMax) );
                    Rtt_STATIC_ASSERT( sizeof(xMin) == sizeof(yMax) );

                    Real xMinRect = r.xMin;
                    Real yMinRect = r.yMin;
                    Real xMaxRect = r.xMax;
                    Real yMaxRect = r.yMax;

                    if ( r.IsEmpty() )
                    {
                        xMinRect = yMinRect = xMaxRect = yMaxRect = Rtt_REAL_0;
                    }

                    setProperty( L, xMin, kLen, xMinRect );
                    setProperty( L, yMin, kLen, yMinRect );
                    setProperty( L, xMax, kLen, xMaxRect );
                    setProperty( L, yMax, kLen, yMaxRect );
                }
                break;
            case 18:
                {
                    const Rect& r = o.StageBounds();
                    lua_pushinteger( L, Rtt_RealToInt( r.xMax - r.xMin ) );
                }
                break;
            case 19:
                {
                    const Rect& r = o.StageBounds();
                    lua_pushinteger( L, Rtt_RealToInt( r.yMax - r.yMin ) );
                }
                break;
            case 8:
                {
                    lua_pushboolean( L, o.IsVisible() );
                }
                break;
            case 9:
                {
                    lua_pushboolean( L, o.IsHitTestable() );
                }
                break;
            case 10:
                {
                    lua_Number alpha = (float)o.Alpha() / 255.0;
                    lua_pushnumber( L, alpha );
                }
                break;
            case 11:
                {
                    const StageObject *stage = o.GetStage();

                    // Only onscreen objects have a parent
                    if ( stage
                         && ( stage->IsOnscreen() || stage->IsRenderedOffScreen() ) )
                    {
                        GroupObject* parent = o.GetParent();
                        if ( parent )
                        {
                            parent->GetProxy()->PushTable( L );
                        }
                        else
                        {
                            // Stage objects and objects rendered offscreen have no parent,
                            // so push nil
                            Rtt_ASSERT( o.IsRenderedOffScreen() || o.GetStage() == & o );
                            lua_pushnil( L );
                        }
                    }
                    else
                    {
                        // Objects that have been removed effectively have no parent,
                        // so push nil. Do NOT push the offscreen parent.
                        lua_pushnil( L );
                    }
                }
                break;
            case 12:
                {
                    const StageObject* stage = o.GetStage();
                    if ( stage && stage->IsOnscreen() )
                    {
                        stage->GetProxy()->PushTable( L );
                    }
                    else
                    {
                        lua_pushnil( L );
                    }
                }
                break;
            case 13:
                {
                    lua_pushnumber( L, o.GetGeometricProperty( kOriginX ) );
                }
                break;
            case 14:
                {
                    lua_pushnumber( L, o.GetGeometricProperty( kOriginY ) );
                }
                break;
            case 15:
                {
                    Real anchorX = o.GetAnchorX();
                    lua_pushnumber( L, anchorX );
                }
                break;
            case 16:
                {
                    Real anchorY = o.GetAnchorY();
                    lua_pushnumber( L, anchorY );
                }
                break;
            case 23:
                {
                    Rtt_Real value = o.GetMaskGeometricProperty( kOriginX );
                    lua_pushnumber( L, value );
                }
                break;
            case 24:
                {
                    Rtt_Real value = o.GetMaskGeometricProperty( kOriginY );
                    lua_pushnumber( L, value );
                }
                break;
            case 25:
                {
                    Rtt_Real value = o.GetMaskGeometricProperty( kScaleX );
                    lua_pushnumber( L, value );
                }
                break;
            case 26:
                {
                    Rtt_Real value = o.GetMaskGeometricProperty( kScaleY );
                    lua_pushnumber( L, value );
                }
                break;
            case 27:
                {
                    Rtt_Real value = o.GetMaskGeometricProperty( kRotation );
                    lua_pushnumber( L, value );
                }
                break;
            case 28:
                {
                    lua_pushboolean( L, o.IsHitTestMasked() );
                }
                break;

            default:
                {
                    GeometricProperty p = DisplayObject::PropertyForKey( LuaContext::GetAllocator( L ), key );
                    if ( p < kNumGeometricProperties )
                    {
                        lua_pushnumber( L, Rtt_RealToFloat( o.GetGeometricProperty( p ) ) );
                    }
                    else
                    {
                        result = 0;
                    }
                }
                break;
            }
        }
        break;
    }

    // We handle this outside the switch statement (and thus keys[]) so we can enumerate all the keys[] and not include it
    if (result == 0 && strncmp(key, "_properties", strlen(key)) == 0)
    {
        String displayProperties(LuaContext::GetRuntime( L )->Allocator());
        String geometricProperties(LuaContext::GetRuntime( L )->Allocator());
        const char **geometricKeys = NULL;
        const int geometricNumKeys = DisplayObject::KeysForProperties(geometricKeys);

        // "GeometricProperties" are derived from the object's geometry and thus handled separately from other properties
        const DisplayObject& o = static_cast< const DisplayObject& >( object );
        for ( int i = 0; i < geometricNumKeys; i++ )
        {
            const int bufLen = 10240;
            char buf[bufLen];

            GeometricProperty p = DisplayObject::PropertyForKey( LuaContext::GetAllocator( L ), geometricKeys[i] );

            if (strchr(geometricKeys[i], '#'))
            {
                // Deprecated property, skip it
                continue;
            }

            if ( p < kNumGeometricProperties )
            {
                snprintf(buf, bufLen, "\"%s\": %g", geometricKeys[i], Rtt_RealToFloat( o.GetGeometricProperty( p ) ) );

                if (! geometricProperties.IsEmpty() && strlen(buf) > 0)
                {
                    geometricProperties.Append(", ");
                }
                
                geometricProperties.Append(buf);
            }
        }

        DumpObjectProperties( L, object, keys, numKeys, displayProperties );

        lua_pushfstring( L, "%s, %s", geometricProperties.GetString(), displayProperties.GetString() );

        result = 1;
    }
    else if ( result == 0 && strcmp( key, "_type" ) == 0 )
    {
        const DisplayObject& o = static_cast< const DisplayObject& >( object );

        lua_pushstring( L, o.GetObjectDesc() );

        result = 1;
    }
    else if ( result == 0 && strcmp( key, "_defined" ) == 0 )
    {
        const DisplayObject& o = static_cast< const DisplayObject& >( object );

        lua_pushstring( L, o.fWhereDefined );

        result = 1;
    }
    else if ( result == 0 && strcmp( key, "_lastChange" ) == 0 )
    {
        const DisplayObject& o = static_cast< const DisplayObject& >( object );

        lua_pushstring( L, o.fWhereChanged );

        result = 1;
    }

    return result;
}

bool
LuaDisplayObjectProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
    if ( ! key ) { return false; }

    // DisplayObject* o = (DisplayObject*)LuaProxy::GetProxyableObject( L, 1 );
    DisplayObject& o = static_cast< DisplayObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, DisplayObject );

    bool result = true;
    bool wasHandledBySpecialCase = false;

    if ( 0 == strcmp( key, "touchMargins" ) )
    {
        wasHandledBySpecialCase = true;

        if ( lua_isnil( L, valueIndex ) )
        {
            o.SetTouchMargins( Rtt_REAL_0, Rtt_REAL_0, Rtt_REAL_0, Rtt_REAL_0 );
        }
        else if ( lua_istable( L, valueIndex ) )
        {
            int tableIndex = valueIndex;
            if ( tableIndex < 0 )
            {
                tableIndex = lua_gettop( L ) + tableIndex + 1;
            }

            Real left = Rtt_REAL_0;
            Real right = Rtt_REAL_0;
            Real top = Rtt_REAL_0;
            Real bottom = Rtt_REAL_0;
            o.GetTouchMargins( left, right, top, bottom );
            left = GetTouchMarginValueForKey( L, tableIndex, "left", left );
            right = GetTouchMarginValueForKey( L, tableIndex, "right", right );
            top = GetTouchMarginValueForKey( L, tableIndex, "top", top );
            bottom = GetTouchMarginValueForKey( L, tableIndex, "bottom", bottom );

            o.SetTouchMargins( left, right, top, bottom );
        }
        else
        {
            luaL_error( L, "ERROR: o.touchMargins can only be set to a table or nil.\n" );
        }
    }

    static const char * keys[] =
    {
        "isVisible",            // 0
        "isHitTestable",        // 1
        "alpha",                // 2
        "parent",                // 3
        "stage",                // 4
        "x",                    // 5
        "y",                    // 6
        "anchorX",                // 7
        "anchorY",                // 8
        "contentBounds",        // 9
        "maskX",                // 10
        "maskY",                // 11
        "maskScaleX",            // 12
        "maskScaleY",            // 13
        "maskRotation",         // 14
        "isHitTestMasked",        // 15
    };
    const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 16, 20, 6, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    if ( ! wasHandledBySpecialCase )
    {
        int index = hash->Lookup( key );
        switch ( index )
        {
        case 0:
            {
                o.SetVisible( lua_toboolean( L, valueIndex ) != 0 );
            }
            break;
        case 1:
            {
                o.SetHitTestable( lua_toboolean( L, valueIndex ) != 0 );
            }
            break;
        case 2:
            {
                /* too verbose:
                Rtt_WARN_SIM(
                    lua_tonumber( L, valueIndex ) >= 0. && lua_tonumber( L, valueIndex ) <= 1.0,
                    ( "WARNING: Attempt to set object.alpha to %g which is outside valid range. It will be clamped to the range [0,1]\n", lua_tonumber( L, valueIndex ) ) );
                 */

                // Explicitly declare T b/c of crappy gcc compiler used by Symbian
                lua_Integer alpha = (lua_Integer)(lua_tonumber( L, valueIndex ) * 255.0f);
                lua_Integer value = Min( (lua_Integer)255, alpha );
                U8 newValue = Max( (lua_Integer)0, value );

                o.SetAlpha( newValue );
            }
            break;
        case 3:
            {
                // No-op for read-only property
            }
            break;
        case 4:
            {
                // No-op for read-only property
            }
            break;
        case 5:
            {
                o.SetGeometricProperty( kOriginX, luaL_toreal( L, valueIndex ) );
            }
            break;
        case 6:
            {
                o.SetGeometricProperty( kOriginY, luaL_toreal( L, valueIndex ) );
            }
            break;
        case 7:
            {
                if ( lua_type( L, valueIndex ) == LUA_TNUMBER )
                {
                    Real newValue = luaL_toreal( L, valueIndex );
                    if ( o.GetStage()->GetDisplay().GetDefaults().IsAnchorClamped() )
                    {
                        newValue = Clamp( newValue, Rtt_REAL_0, Rtt_REAL_1 );
                    }
                    o.SetAnchorX( newValue );
                }
                else
                {
                    luaL_error( L, "ERROR: o.anchorX can only be set to a number.\n" );
                }

            }
            break;
        case 8:
            {
                if ( lua_type( L, valueIndex) == LUA_TNUMBER )
                {
                    Real newValue = luaL_toreal( L, valueIndex );
                    if ( o.GetStage()->GetDisplay().GetDefaults().IsAnchorClamped() )
                    {
                        newValue = Clamp( newValue, Rtt_REAL_0, Rtt_REAL_1 );
                    }
                    o.SetAnchorY( newValue );
                }
                else
                {
                    luaL_error( L, "ERROR: o.anchorY can only be set to a number.\n" );
                }

            }
            break;
        case 9:
            {
                // No-op for read-only keys
            }
            break;
        case 10:
            {
                Real newValue = luaL_toreal( L, valueIndex );
                o.SetMaskGeometricProperty( kOriginX, newValue );
            }
            break;
        case 11:
            {
                Real newValue = luaL_toreal( L, valueIndex );
                o.SetMaskGeometricProperty( kOriginY, newValue );
            }
            break;
        case 12:
            {
                Real newValue = luaL_toreal( L, valueIndex );
                o.SetMaskGeometricProperty( kScaleX, newValue );
            }
            break;
        case 13:
            {
                Real newValue = luaL_toreal( L, valueIndex );
                o.SetMaskGeometricProperty( kScaleY, newValue );
            }
            break;
        case 14:
            {
                Real newValue = luaL_toreal( L, valueIndex );
                o.SetMaskGeometricProperty( kRotation, newValue );
            }
            break;
        case 15:
            {
                o.SetHitTestMasked( lua_toboolean( L, valueIndex ) != 0 );
            }
            break;
        default:
            {
                GeometricProperty p = DisplayObject::PropertyForKey( LuaContext::GetAllocator( L ), key );
                if ( p < kNumGeometricProperties )
                {
                    Real newValue = luaL_toreal( L, valueIndex );
                    o.SetGeometricProperty( p, newValue );
                }
                else if ( ! lua_isnumber( L, 2 ) )
                {
                    result = false;
                }
            }
            break;
        }
    }

    // We changed a property so record where we are so that "_lastChange" will be available later to say where it happened
    // (this is a noop on non-debug builds because lua_where returns an empty string)
    if (result)
    {
        luaL_where(L, 1);
        const char *where = lua_tostring( L, -1 );

        if (where[0] != 0)
        {
            if (o.fWhereChanged != NULL)
            {
                free((void *) o.fWhereChanged);
            }

            // If this fails, the pointer will be NULL and that's handled gracefully
            o.fWhereChanged = strdup(where);
        }

        lua_pop(L, 1);
    }

    return result;
}

// ----------------------------------------------------------------------------

bool
ClosedPath::ExtensionAdapter::IsFillPaint( const DisplayObject* object, const Paint* paint )
{
    return static_cast< const ShapeObject* >( object )->GetPath().GetFill() == paint;
}

Geometry*
ClosedPath::ExtensionAdapter::GetGeometry( DisplayObject* object )
{
    ClosedPath& path = static_cast< ShapeObject* >( object )->GetPath();
    ShapePath& shapePath = static_cast< ShapePath& >( path );
    return shapePath.GetFillGeometry();
}

const Geometry *
ClosedPath::ExtensionAdapter::GetGeometry( const DisplayObject* object )
{
    const ClosedPath& path = static_cast< const ShapeObject* >( object )->GetPath();
    const ShapePath& shapePath = static_cast< const ShapePath& >( path );
    return shapePath.GetFillGeometry();
}

static const ClosedPath::ExtensionAdapter&
ExtensionAdapterFillConstant()
{
    static const ClosedPath::ExtensionAdapter kAdapter;
    return kAdapter;
}

const LuaShapeObjectProxyVTable&
LuaShapeObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

int
LuaShapeObjectProxyVTable::setFillColor( lua_State *L )
{
    ShapeObject* o = (ShapeObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, ShapeObject );

    if ( o )
    {
        if ( lua_istable( L, 2 ) )
        {
            GradientPaint *gradient = LuaLibDisplay::LuaNewGradientPaint( L, 2 );
            if ( gradient )
            {
                o->SetFill( gradient );

                // Early return
                return 0;
            }
        }

        if ( ! o->GetPath().GetFill() )
        {
            Paint* p = LuaLibDisplay::LuaNewColor( L, 2 );
            o->SetFill( p );
        }
        else
        {
            Color c = LuaLibDisplay::toColor( L, 2 );
            o->SetFillColor( c );
        }
    }

    return 0;
}

// object.fill
int
LuaShapeObjectProxyVTable::setFill( lua_State *L, int valueIndex )
{
    ShapeObject* o = (ShapeObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, ShapeObject );

    if ( Rtt_VERIFY( o ) )
    {
        Paint *paint = LuaLibDisplay::LuaNewPaint( L, valueIndex );
        o->SetFill( paint );
    }
    return 0;
}

int
LuaShapeObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[] ) const
{
    if ( ! key ) { return 0; }
    
    int result = 1;

    static const char * keys[] =
    {
        "path",                // 0
        "fill",                // 1
        "blendMode",           // 2
        "setFillColor",        // 3
        // "fillExtendedData",    // 4
    };
    const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 4, 1, 1, __FILE__, __LINE__ );
    StringHash *hash = &sHash;
    int index = hash->Lookup( key );

    // ShapeObject* o = (ShapeObject*)LuaProxy::GetProxyableObject( L, 1 );
    const ShapeObject& o = static_cast< const ShapeObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, ShapeObject );

    switch ( index )
    {
    case 0:
        {
            o.GetPath().PushProxy( L );
        }
        break;
    case 1:
        {
            const Paint *paint = o.GetPath().GetFill();
            if ( paint )
            {
                paint->PushProxy( L );
            }
            else
            {
                lua_pushnil( L );
            }
        }
        break;
    case 2:
        {
            RenderTypes::BlendType blend = o.GetBlend();
            lua_pushstring( L, RenderTypes::StringForBlendType( blend ) );
        }
        break;
    case 3:
        {
            Lua::PushCachedFunction( L, Self::setFillColor );
        }
        break;
    case 4:
        {
            const ShapePath& path = static_cast< const ShapePath& >( o.GetPath() );
            Geometry* geometry = path.GetFillGeometry();
            Geometry::ExtensionBlock* block = geometry->GetExtensionBlock();
                   
            if ( block )
            {
                if (!block->fProxy)
                {
                    block->fProxy = LuaUserdataProxy::New( L, const_cast< ShapeObject* >( &o ) );
                    block->fProxy->SetAdapter( &ExtensionAdapterFillConstant() );
                }
            
                block->fProxy->Push( L );
            }
            else
            {
                lua_pushnil( L );
            }
        }
        break;
    default:
        {
             result = Super::ValueForKey( L, object, key );
        }
        break;
    }

    // Because this is effectively a derived class, we will have successfully gotten a value
    // for the "_properties" key from the parent and we now need to combine that with the
    // properties of the child
    if (result == 1 && strcmp( key, "_properties" ) == 0 )
    {
        String properties(LuaContext::GetRuntime( L )->Allocator());
        const char *prefix = "";
        const char *postfix = "";

        DumpObjectProperties( L, object, keys, numKeys, properties );

        // Some objects are derived from "ShapeObjects" but some are just "ShapeObjects and
        // we need to emit complete JSON in those cases so we add the enclosing braces if
        // this is a "ShapeObject"
        if (strcmp(o.GetObjectDesc(), "ShapeObject") == 0 || strcmp(o.GetObjectDesc(), "ImageObject") == 0)
        {
            prefix = "{ ";
            postfix = " }";
        }

        // Combine this object's properties with those of the super that were pushed above
        lua_pushfstring( L, "%s%s, %s%s", prefix, properties.GetString(), lua_tostring( L, -1 ), postfix );

        lua_remove( L, -2 ); // pop super properties
        result = 1;
    }

    return result;
}

bool
LuaShapeObjectProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
    if ( ! key ) { return false; }

    // ShapeObject* o = (ShapeObject*)LuaProxy::GetProxyableObject( L, 1 );
    ShapeObject& o = static_cast< ShapeObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, ShapeObject );

    bool result = true;

    static const char * keys[] = 
    {
        "fill",             // 0
        "blendMode",        // 1
        // "fillExtension"     // 2
    };
    const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 2, 1, 1, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    int index = hash->Lookup( key );
    switch ( index )
    {
    case 0:
        {
            setFill( L, valueIndex );
        }
        break;
    case 1:
        {
            const char *v = lua_tostring( L, valueIndex );
            RenderTypes::BlendType blend = RenderTypes::BlendTypeForString( v );
            o.SetBlend( blend );
        }
        break;
    case 2:
        {
            ShapePath& path = static_cast< ShapePath& >( o.GetPath() );
            Geometry* geometry = path.GetFillGeometry();
            const char* name = lua_tostring( L, valueIndex );
            SharedPtr<FormatExtensionList>* extensionList = LuaContext::GetRuntime( L )->GetDisplay().GetShaderFactory().GetExtensionList( name );
            
            if (extensionList)
            {
                if (!geometry->GetExtensionList())
                {
                    Geometry::ExtensionBlock* block = geometry->EnsureExtension();
                    
                    block->SetExtensionList( *extensionList );
                    block->UpdateData( geometry->GetStoredOnGPU(), 1 ); // TODO, return GetFillVertexCount when we want to use this?
                    path.GetObserver()->Invalidate( DisplayObject::kGeometryFlag );
                }
                
                else
                {
                    CoronaLuaWarning( L, "Shape object already has a %s extension.\n", "fill" );
                }
            }
            
            else
            {
                CoronaLuaWarning( L, "Unable to find attribute `%s`", name );
            }
        }
        break;
    default:
        {
            result = Super::SetValueForKey( L, object, key, valueIndex );
        }
        break;
    }

    return result;
}

const LuaProxyVTable&
LuaShapeObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

const LuaSnapshotObjectProxyVTable&
LuaSnapshotObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

int
LuaSnapshotObjectProxyVTable::Invalidate( lua_State *L )
{
    SnapshotObject* o = (SnapshotObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SnapshotObject );

    if ( o )
    {
        const char *value = lua_tostring( L, 2 );
        SnapshotObject::RenderFlag flag = SnapshotObject::RenderFlagForString( value );
        o->SetDirty( flag );
    }

    return 0;
}

static StringHash *
GetSnapshotHash( lua_State *L )
{
    static const char *keys[] =
    {
        "group",            // 0 (read-only)
        "invalidate",        // 1 (read-only)
        "textureFilter",    // 2
        "textureWrapX",        // 3
        "textureWrapY",        // 4
        "clearColor",        // 5
        "canvas",            // 6 (read-only)
        "canvasMode",        // 7
    };
    const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 8, 6, 1, __FILE__, __LINE__ );
    return &sHash;
}

int
LuaSnapshotObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[] ) const
{
    if ( ! key )
    {
        return 0;
    }
    
    int result = 1;

    StringHash *sHash = GetSnapshotHash( L );

    const SnapshotObject& o = static_cast< const SnapshotObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SnapshotObject );

    int index = sHash->Lookup( key );

    switch ( index )
    {
    case 0:
        {
            o.GetGroup().GetProxy()->PushTable( L );
            result = 1;
        }
        break;
    case 1:
        {
            Lua::PushCachedFunction( L, Self::Invalidate );
        }
        break;
    case 2:
        {
            const char *str = RenderTypes::StringForTextureFilter( o.GetTextureFilter() );
            lua_pushstring( L, str );
         }
        break;
    case 3:
        {
            const char *str = RenderTypes::StringForTextureWrap( o.GetTextureWrapX() );
            lua_pushstring( L, str );
        }
        break;
    case 4:
        {
            const char *str = RenderTypes::StringForTextureWrap( o.GetTextureWrapY() );
            lua_pushstring( L, str );
        }
        break;
    case 5:
        {
            result = LuaLibDisplay::PushColorChannels( L, o.GetClearColor() );
        }
        break;
    case 6:
        {
            o.GetCanvas().GetProxy()->PushTable( L );
        }
        break;
    case 7:
        {
            const char *str = SnapshotObject::StringForCanvasMode( o.GetCanvasMode() );
            lua_pushstring( L, str );
        }
        break;
    default:
        {
            result = Super::ValueForKey( L, object, key );
        }
        break;
    }

    // Because this is effectively a derived class, we will have successfully gotten a value
    // for the "_properties" key from the parent and we now need to combine that with the
    // properties of the child
    if ( result == 1 && strcmp( key, "_properties" ) == 0 )
    {
        String snapshotProperties(LuaContext::GetRuntime( L )->Allocator());
        const char **keys = NULL;
        const int numKeys = sHash->GetKeys(keys);

        DumpObjectProperties( L, object, keys, numKeys, snapshotProperties );

        lua_pushfstring( L, "{ %s, %s }", snapshotProperties.GetString(), lua_tostring( L, -1 ) );
        lua_remove( L, -2 ); // pop super properties
        result = 1;
    }

    return result;
}

bool
LuaSnapshotObjectProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
    if ( ! key ) { return false; }

    bool result = true;

    StringHash *sHash = GetSnapshotHash( L );

    SnapshotObject& o = static_cast< SnapshotObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SnapshotObject );

    int index = sHash->Lookup( key );

    switch ( index )
    {
    case 0:
    case 1:
    case 6:
        {
            // No-op for read-only property
            CoronaLuaWarning(L, "the '%s' property of snapshot objects is read-only", key);
        }
        break;
    case 2:
        {
            const char *str = lua_tostring( L, valueIndex );
            o.SetTextureFilter( RenderTypes::TextureFilterForString( str ) );
        }
        break;
    case 3:
        {
            const char *str = lua_tostring( L, valueIndex );
            o.SetTextureWrapX( RenderTypes::TextureWrapForString( str ) );
        }
        break;
    case 4:
        {
            const char *str = lua_tostring( L, valueIndex );
            o.SetTextureWrapY( RenderTypes::TextureWrapForString( str ) );
        }
        break;
    case 5:
        {
            Color c = ColorZero();
            LuaLibDisplay::ArrayToColor( L, valueIndex, c );
            o.SetClearColor( c );
        }
        break;
    case 7:
        {
            const char *str = lua_tostring( L, valueIndex );
            o.SetCanvasMode( SnapshotObject::CanvasModeForString( str ) );
        }
        break;
    default:
        {
            result = Super::SetValueForKey( L, object, key, valueIndex );
        }
        break;
    }

    return result;
}

const LuaProxyVTable&
LuaSnapshotObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

const LuaGroupObjectProxyVTable&
LuaGroupObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

int
LuaGroupObjectProxyVTable::Insert( lua_State *L, GroupObject *parent )
{
    int index = (int) lua_tointeger( L, 2 );

    ENABLE_SUMMED_TIMING( true );

    int childIndex = 3; // index of child object (table owned by proxy)
    if ( 0 == index )
    {
        // Optional index arg missing, so insert at end
        --childIndex;
        index = parent->NumChildren();
    }
    else
    {
        // Map Lua indices to C++ indices
        --index;
    }
    Rtt_ASSERT( index >= 0 );
    Rtt_ASSERT( lua_istable( L, childIndex ) );

    // Default to false if no arg specified
    bool resetTransform = lua_toboolean( L, childIndex + 1 ) != 0;

    DisplayObject* child = (DisplayObject*)LuaProxy::GetProxyableObject( L, childIndex );
    if ( child != parent )
    {
        if ( ! child->IsRenderedOffScreen() )
        {
            GroupObject* oldParent = child->GetParent();

            // Display an error if they're indexing beyond the array (bug http://bugs.coronalabs.com/?18838 )
            const S32 maxIndex = parent->NumChildren();
            if ( index > maxIndex || index < 0 )
            {
                CoronaLuaWarning(L, "group index %d out of range (should be 1 to %d)", (index+1), maxIndex );
            }
            
            SUMMED_TIMING( pi, "Group: Insert (into parent)" );
            
            parent->Insert( index, child, resetTransform );

            SUMMED_TIMING( ai, "Group: Insert (post-parent insert)" );

            if ( oldParent != parent )
            {
                // A freshly inserted object starts a new removal lifecycle, so allow
                // prefinalize to fire again on a later display.remove()/removeSelf().
                child->ResetPreFinalizeTree();
            }
        }
        else
        {
            CoronaLuaWarning( L, "Insertion failed: display objects that are owned by offscreen resources cannot be inserted into groups" );
        }
    }
    else
    {
        luaL_error( L, "ERROR: attempt to insert display object into itself" );
    }

    ENABLE_SUMMED_TIMING( false );

    return 0;
}

int
LuaGroupObjectProxyVTable::insert( lua_State *L )
{
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, GroupObject );
    GroupObject *parent = (GroupObject*)LuaProxy::GetProxyableObject( L, 1 );
    return Insert( L, parent );
}

int
LuaGroupObjectProxyVTable::setAsSizedGroup( lua_State *L )
{
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, GroupObject );
	GroupObject *parent = (GroupObject*)LuaProxy::GetProxyableObject( L, 1 );

	if ( parent )
	{
		if ( parent == parent->GetStage() )
		{
			CoronaLuaWarning( L, "stage:setAsSizedGroup() is not supported" );
			return 0;
		}

		Real width = luaL_checkreal( L, 2 );
		Real height = luaL_checkreal( L, 3 );

		parent->SetAsSizedGroup( width, height );
	}

	return 0;
}

int
LuaGroupObjectProxyVTable::setAsSizedGroupWithCurrentSize( lua_State *L )
{
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, GroupObject );
	GroupObject *parent = (GroupObject*)LuaProxy::GetProxyableObject( L, 1 );

	if ( parent )
	{
		if ( parent == parent->GetStage() )
		{
			CoronaLuaWarning( L, "stage:setAsSizedGroupWithCurrentSize() is not supported" );
			return 0;
		}

		parent->SetAsSizedGroupWithCurrentSize();
	}

	return 0;
}

// Removes child at index from parent and pushes onto the stack. Pushes nil
// if index is invalid.
void
LuaDisplayObjectProxyVTable::PushAndRemove( lua_State *L, GroupObject* parent, S32 index )
{
    if ( index >= 0 )
    {
        Rtt_ASSERT( parent );

        // Offscreen objects (i.e. ones in the orphanage) do have a stage
        StageObject *stage = parent->GetStage();
        if ( stage )
        {
            Rtt_ASSERT( LuaContext::GetRuntime( L )->GetDisplay().HitTestOrphanage() != parent
                        && LuaContext::GetRuntime( L )->GetDisplay().Orphanage() != parent );

            SUMMED_TIMING( par1, "Object: PushAndRemove (release)" );

            DisplayObject* child = parent->Release( index );

            if (child != NULL)
            {
                SUMMED_TIMING( par2, "Object: PushAndRemove (rest)" );

                // If child is the same as global focus, clear global focus
                DisplayObject *globalFocus = stage->GetFocus();
                if ( globalFocus == child )
                {
                    stage->SetFocus( NULL );
                }

                // Always the per-object focus
                stage->SetFocus( child, NULL );
                child->SetFocusId( NULL ); // Defer removal from the focus object array

                child->RemovedFromParent( L, parent );
                child->PreFinalizeTree( L );

                // We need to return table, so push it on stack
                Rtt_ASSERT( child->IsReachable() );
                LuaProxy* proxy = child->GetProxy();
                proxy->PushTable( L );

                // Rtt_TRACE( ( "release table ref(%x)\n", lua_topointer( L, -1 ) ) );

                Display& display = LuaContext::GetRuntime( L )->GetDisplay();

                // PreFinalizeTree() can synchronously reinsert the object (for example,
                // pooling code may move it into another group). Only actually move the child into
                // the orphanage if it is still parentless after listener callbacks.
                if ( ! child->GetParent() )
                {
                    GroupObject& offscreenGroup =
                    * ( child->IsUsedByHitTest() ? display.HitTestOrphanage() : display.Orphanage() );
                    offscreenGroup.Insert( -1, child, false );
                    child->DidMoveOffscreen();
                }
            }
        }
        else
        {
            luaL_error( L, "ERROR: attempt to remove an object that's already been removed from the stage or whose parent/ancestor group has already been removed" );

            // Rtt_ASSERT( LuaContext::GetRuntime( L )->GetDisplay().HitTestOrphanage() == parent
            //             || LuaContext::GetRuntime( L )->GetDisplay().Orphanage() == parent );
        }
    }
    else
    {
        lua_pushnil( L );
    }
}

int
LuaGroupObjectProxyVTable::Remove( lua_State *L, GroupObject *parent )
{
    Rtt_ASSERT( ! lua_isnil( L, 1 ) );

    S32 index = -1;
    if ( lua_istable( L, 2 ) )
    {
        DisplayObject* child = (DisplayObject*)LuaProxy::GetProxyableObject( L, 2 );
        if ( child )
        {
            index = parent->Find( * child );

#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
            if (index < 0)
            {
                CoronaLuaWarning(L, "objectGroup:remove(): invalid object reference (most likely object is not in group)");
            }
#endif
        }
    }
    else
    {
        // Lua indices start at 1
        index = (int) lua_tointeger( L, 2 ) - 1;

#if defined( Rtt_DEBUG ) || defined( Rtt_AUTHORING_SIMULATOR )
        if (index < 0 || index > parent->NumChildren())
        {
            CoronaLuaWarning(L, "objectGroup:remove(): index of %ld out of range (should be 1 to %d)", lua_tointeger( L, 2 ), parent->NumChildren());
        }
#endif
    }

    PushAndRemove( L, parent, index );

    return 1;
}

// group:remove( indexOrChild )
int
LuaGroupObjectProxyVTable::Remove( lua_State *L )
{
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, GroupObject );
    GroupObject *parent = (GroupObject*)LuaProxy::GetProxyableObject( L, 1 );
    return Remove( L, parent );
}

int
LuaGroupObjectProxyVTable::PushChild( lua_State *L, const GroupObject& o )
{
    int result = 0;

    int index = (int) lua_tointeger( L, 2 ) - 1; // Map Lua index to C index
    if ( index >= 0 )
    {
        // GroupObject* o = (GroupObject*)LuaProxy::GetProxyableObject( L, 1 );

        if ( index < o.NumChildren() )
        {
            const DisplayObject& child = o.ChildAt( index );
            LuaProxy* childProxy = child.GetProxy();

            if (childProxy != NULL)
            {
                result = childProxy->PushTable( L );
            }
        }
    }

    return result;
}

int
LuaGroupObjectProxyVTable::PushMethod( lua_State *L, const GroupObject& o, const char *key ) const
{
    int result = 1;

    static const char * keys[] =
    {
        "insert",            // 0
        "remove",            // 1
        "setAsSizedGroup",    // 2
        "setAsSizedGroupWithCurrentSize",    // 3
        "numChildren",        // 4
        "anchorChildren",    // 5
        "isSizedGroup",        // 6
    };
    static const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 7, 3, 1, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    int index = hash->Lookup( key );
    switch ( index )
    {
    case 0:
        {
            Lua::PushCachedFunction( L, Self::insert );
            result = 1;
        }
        break;
    case 1:
        {
            Lua::PushCachedFunction( L, Self::Remove );
            result = 1;
        }
        break;
    case 2:
        {
            Lua::PushCachedFunction( L, Self::setAsSizedGroup );
            result = 1;
        }
        break;
    case 3:
        {
            Lua::PushCachedFunction( L, Self::setAsSizedGroupWithCurrentSize );
            result = 1;
        }
        break;
    case 4:
        {
            // GroupObject* o = (GroupObject*)LuaProxy::GetProxyableObject( L, 1 );
            lua_pushinteger( L, o.NumChildren() );
            result = 1;
        }
        break;
    case 5:
        {
            lua_pushboolean( L, o.IsAnchorChildren() );
            result = 1;
        }
        break;
    case 6:
        {
            lua_pushboolean( L, o.IsSizedGroup() );
            result = 1;
        }
        break;

    default:
        {
            result = 0;
        }
        break;
    }

    if ( result == 0 && strcmp( key, "_properties" ) == 0 )
    {
        String snapshotProperties(LuaContext::GetRuntime( L )->Allocator());
        const char **keys = NULL;
        const int numKeys = hash->GetKeys(keys);

        DumpObjectProperties( L, o, keys, numKeys, snapshotProperties );
        Super::ValueForKey( L, o, "_properties" );

        lua_pushfstring( L, "{ %s, %s }", snapshotProperties.GetString(), lua_tostring( L, -1 ) );
        lua_remove( L, -2 ); // pop super properties
        result = 1;
    }

    return result;
}

int
LuaGroupObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[] ) const
{
    int result = 0;

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, GroupObject );
    const GroupObject& o = static_cast< const GroupObject& >( object );

    if ( lua_type( L, 2 ) == LUA_TNUMBER )
    {
        result = PushChild( L, o );
    }
    else if ( key )
    {
        result = PushMethod( L, o, key );

        if ( 0 == result )
        {
            result = Super::ValueForKey( L, object, key );
        }
    }

    return result;
}

bool
LuaGroupObjectProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
    if ( ! key ) { return false; }

    bool result = true;

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, GroupObject );

    if ( 0 == strcmp( key, "anchorChildren" ) )
    {
        GroupObject& o = static_cast< GroupObject& >( object );
        o.SetAnchorChildren( !! lua_toboolean( L, valueIndex ) );
    }
    else
    {
        result = Super::SetValueForKey( L, object, key, valueIndex );
    }

    return result;
}

const LuaProxyVTable&
LuaGroupObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

// [OLD] stage:setFocus( object )
// [NEW] stage:setFocus( object [, touchId] )
int
LuaStageObjectProxyVTable::setFocus( lua_State *L )
{
    StageObject* o = (StageObject*)LuaProxy::GetProxyableObject( L, 1 );

    Rtt_WARN_SIM_PROXY_TYPE( L, 1, StageObject );

    if ( o )
    {
        // By default, assume this is a call to set global focus (i.e. old behavior)
        bool isGlobal = true;
        DisplayObject* focus = NULL;

        if ( lua_istable( L, 2 ) )
        {
            focus = (DisplayObject*)LuaProxy::GetProxyableObject( L, 2 );
            Rtt_WARN_SIM_PROXY_TYPE( L, 2, DisplayObject );

            // If the optional touchId arg exists, then we are using the new behavior
            // If it doesn't, then isGlobal remains true and we use the old behavior
            if ( lua_type( L, 3 ) != LUA_TNONE )
            {
                const void *touchId = lua_touserdata( L, 3 );

                const MPlatformDevice& device = LuaContext::GetRuntime( L )->Platform().GetDevice();
                if ( device.DoesNotify( MPlatformDevice::kMultitouchEvent ) )
                {
                    // If optional parameter supplied, set per-object focus instead of global
                    isGlobal = false;
                    o->SetFocus( focus, touchId );
                }
                else
                {
                    // The new API maps to old behavior when we are *not* multitouch
                    if ( ! touchId )
                    {
                        focus = NULL;
                    }
                }
            }
        }

        if ( isGlobal )
        {
            o->SetFocus( focus );
        }
    }

    return 0;
}

const LuaStageObjectProxyVTable&
LuaStageObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

int
LuaStageObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[] ) const
{
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, StageObject );

    if ( ! key )
    {
        // If there's no key, we'll may have a table index to look up which is handled
        // by LuaGroupObjectProxyVTable::ValueForKey()
        return Super::ValueForKey( L, object, key );
    }

    int result = 1;

    static const char * keys[] =
    {
        "setFocus",            // 0
    };
    static const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 1, 0, 1, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    int index = hash->Lookup( key );
    switch ( index )
    {
        case 0:
            {
                Lua::PushCachedFunction( L, Self::setFocus );
            }
            break;
        default:
            {
                result = Super::ValueForKey( L, object, key );
            }
            break;
    }
    
    // If we retrieved the "_properties" key from the super, merge it with the local properties
    if ( result == 1 && strcmp( key, "_properties" ) == 0 )
    {
        String stageProperties(LuaContext::GetRuntime( L )->Allocator());

        DumpObjectProperties( L, object, keys, numKeys, stageProperties );

        lua_pushfstring( L, "{ %s, %s }", stageProperties.GetString(), lua_tostring( L, -1 ) );
        lua_remove( L, -2 ); // pop super properties
        result = 1;
    }

    return result;
}

const LuaProxyVTable&
LuaStageObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

int
LuaPlatformDisplayObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[] ) const
{
    if ( ! key ) { return 0; }

    // PlatformDisplayObject* o = (PlatformDisplayObject*)LuaProxy::GetProxyableObject( L, 1 );
    const PlatformDisplayObject& o = static_cast< const PlatformDisplayObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, PlatformDisplayObject );

    int result = o.ValueForKey( L, key ) || Super::ValueForKey( L, object, key );

    if ( 0 == result )
    {
        if ( strcmp( "getNativeProperty", key ) == 0 )
        {
            lua_pushlightuserdata( L, const_cast< PlatformDisplayObject * >( & o ) );
            lua_pushcclosure( L, PlatformDisplayObject::getNativeProperty, 1 );
            result = 1;
        }
        else if ( strcmp( "setNativeProperty", key ) == 0 )
        {
            lua_pushlightuserdata( L, const_cast< PlatformDisplayObject * >( & o ) );
            lua_pushcclosure( L, PlatformDisplayObject::setNativeProperty, 1 );
            result = 1;
        }
    }

    // If we retrieved the "_properties" key from the super, merge it with the local properties
    if ( result == 1 && strcmp( key, "_properties" ) == 0 )
    {
        String textProperties(LuaContext::GetRuntime( L )->Allocator());

        // TODO: Implement DumpObjectProperties for PlatformDisplayObjects
        // DumpObjectProperties( L, object, keys, numKeys, textProperties );

        // lua_pushfstring( L, "{ %s, %s }", textProperties.GetString(), lua_tostring( L, -1 ) );
        lua_pushfstring( L, "{ %s }", lua_tostring( L, -1 ) );
        lua_remove( L, -2 ); // pop super properties
        result = 1;
    }

    return result;
}

bool
LuaPlatformDisplayObjectProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
    if ( ! key ) { return false; }

    // PlatformDisplayObject* o = (PlatformDisplayObject*)LuaProxy::GetProxyableObject( L, 1 );
    PlatformDisplayObject& o = static_cast< PlatformDisplayObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, PlatformDisplayObject );

    bool result =
        o.SetValueForKey( L, key, valueIndex )
        || Super::SetValueForKey( L, object, key, valueIndex );

    return result;
}

// ----------------------------------------------------------------------------

// Need explicit default constructor for const use by C++ spec
LuaPlatformTextFieldObjectProxyVTable::LuaPlatformTextFieldObjectProxyVTable()
	: LuaPlatformDisplayObjectProxyVTable()
{
}

const LuaPlatformTextFieldObjectProxyVTable&
LuaPlatformTextFieldObjectProxyVTable::Constant()
{
	static const Self kVTable;
	return kVTable;
}

const LuaProxyVTable&
LuaPlatformTextFieldObjectProxyVTable::Parent() const
{
	return Super::Constant();
}

// ----------------------------------------------------------------------------
// Need explicit default constructor for const use by C++ spec
LuaPlatformWebViewObjectProxyVTable::LuaPlatformWebViewObjectProxyVTable()
    : LuaPlatformDisplayObjectProxyVTable()
{
}

const LuaPlatformWebViewObjectProxyVTable&
LuaPlatformWebViewObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

const LuaProxyVTable&
LuaPlatformWebViewObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

// Need explicit default constructor for const use by C++ spec
LuaPlatformVideoObjectProxyVTable::LuaPlatformVideoObjectProxyVTable()
    : LuaPlatformDisplayObjectProxyVTable()
{
}

const LuaPlatformVideoObjectProxyVTable&
LuaPlatformVideoObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

const LuaProxyVTable&
LuaPlatformVideoObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

const LuaSpriteObjectProxyVTable&
LuaSpriteObjectProxyVTable::Constant()
{
    static const Self kVTable;
    return kVTable;
}

int
LuaSpriteObjectProxyVTable::play( lua_State *L )
{
    SpriteObject *o = (SpriteObject*)LuaProxy::GetProxyableObject( L, 1 );
    
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );
    
    if ( o )
    {
        o->Play( L );
    }

    return 0;
}

int
LuaSpriteObjectProxyVTable::pause( lua_State *L )
{
    SpriteObject *o = (SpriteObject*)LuaProxy::GetProxyableObject( L, 1 );
    
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );
    
    if ( o )
    {
        o->Pause();
    }

    return 0;
}

int
LuaSpriteObjectProxyVTable::setSequence( lua_State *L )
{
    SpriteObject *o = (SpriteObject*)LuaProxy::GetProxyableObject( L, 1 );
    
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );
    
    if ( o )
    {
        const char *name = lua_tostring( L, 2 );
        o->SetSequence( name, true );
    }

    return 0;
}

int
LuaSpriteObjectProxyVTable::setSequenceAndPlay( lua_State *L )
{
    SpriteObject *o = (SpriteObject*)LuaProxy::GetProxyableObject( L, 1 );
    
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );
    
    if ( o )
    {
        const char *name = lua_tostring( L, 2 );
        o->SetSequence( name, true );
        o->Play( L );
    }

    return 0;
}

int
LuaSpriteObjectProxyVTable::setSequenceWithoutReset( lua_State *L )
{
    SpriteObject *o = (SpriteObject*)LuaProxy::GetProxyableObject( L, 1 );
    
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );
    
    if ( o )
    {
        const char *name = lua_tostring( L, 2 );
        o->SetSequence( name, false );
    }

    return 0;
}

int
LuaSpriteObjectProxyVTable::setFrame( lua_State *L )
{
    SpriteObject *o = (SpriteObject*)LuaProxy::GetProxyableObject( L, 1 );
    
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );
    
    if ( o )
    {
        int effectiveFrameIndex = (int) lua_tointeger( L, 2 );
        o->SetEffectiveFrame( effectiveFrameIndex - 1 ); // Lua is 1-based
    }

    return 0;
}

int
LuaSpriteObjectProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[] ) const
{
    if ( ! key ) { return 0; }
    
    int result = 1;

    static const char * keys[] =
    {
        // Read-write properties
        "timeScale",            // 0

        // Read-only properties
        "frame",                    // 1
        "frameInSequence",            // 2
        "frameInSheet",                // 3
        "loopIndex",                // 4
        "sequence",                    // 5
        "numFrames",                // 6
        "isPlaying",                // 7

        // Methods    
        "play",                        // 8
        "pause",                    // 9
        "setSequence",                // 10
        "setSequenceAndPlay",        // 11
        "setSequenceWithoutReset",    // 12
        "setFrame"                    // 13
    };
    static const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 14, 18, 2, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    int index = hash->Lookup( key );

    const SpriteObject& o = static_cast< const SpriteObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );

    switch ( index )
    {
    case 0:
        {
            lua_pushnumber( L, Rtt_RealToFloat( o.GetTimeScale() ) );
        }
        break;
    case 1:
        {
            lua_pushinteger( L, o.GetCurrentEffectiveFrameIndex() + 1 ); // Lua is 1-based
        }
        break;
    case 2:
        {
            lua_pushinteger( L, o.GetCurrentFrameIndex() + 1 ); // Lua is 1-based
        }
        break;
    case 3:
        {
            lua_pushinteger( L, o.GetCurrentSequence()->GetSheetFrameIndexForFrameIndex(o.GetCurrentFrameIndex()) + 1 ); // Lua is 1-based
        }
        break;
    case 4:
        {
            lua_pushinteger( L, o.GetCurrentLoopIndex() );
        }
        break;
    case 5:
        {
            lua_pushstring( L, o.GetCurrentSequence()->GetName() );
        }
        break;
    case 6:
        {
            lua_pushinteger( L, o.GetCurrentSequence()->GetNumFrames() );
        }
        break;
    case 7:
        {
            lua_pushboolean( L, o.IsPlaying() );
        }
        break;
    case 8:
        {
            Lua::PushCachedFunction( L, Self::play );
        }
        break;
    case 9:
        {
            Lua::PushCachedFunction( L, Self::pause );
        }
        break;
    case 10:
        {
            Lua::PushCachedFunction( L, Self::setSequence );
        }
        break;
    case 11:
        {
            Lua::PushCachedFunction( L, Self::setSequenceAndPlay );
        }
        break;
    case 12:
        {
            Lua::PushCachedFunction( L, Self::setSequenceWithoutReset );
        }
        break;
    case 13:
        {
            Lua::PushCachedFunction( L, Self::setFrame );
        }
        break;
    default:
        {
            result = Super::ValueForKey( L, object, key );
        }
        break;
    }

    // If we retrieved the "_properties" key from the super, merge it with the local properties
    if ( result == 1 && strcmp( key, "_properties" ) == 0 )
    {
        String spriteProperties(LuaContext::GetRuntime( L )->Allocator());

        DumpObjectProperties( L, object, keys, numKeys, spriteProperties );

        lua_pushfstring( L, "{ %s, %s }", spriteProperties.GetString(), lua_tostring( L, -1 ) );
        lua_remove( L, -2 ); // pop super properties
        result = 1;
    }

    return result;
}

bool
LuaSpriteObjectProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
    if ( ! key ) { return false; }

    SpriteObject& o = static_cast< SpriteObject& >( object );
    Rtt_WARN_SIM_PROXY_TYPE( L, 1, SpriteObject );

    bool result = true;

    static const char * keys[] =
    {
        // Read-write properties
        "timeScale",    // 0
    
        // Read-only properties
        "frame",        // 1
        "numFrames",    // 2
        "isPlaying",    // 3
        "sequence",        // 4
    };
    static const int numKeys = sizeof( keys ) / sizeof( const char * );
    static StringHash sHash( *LuaContext::GetAllocator( L ), keys, numKeys, 5, 1, 1, __FILE__, __LINE__ );
    StringHash *hash = &sHash;

    int index = hash->Lookup( key );

    switch ( index )
    {
    case 0:
        {
            Real timeScale = Rtt_FloatToReal( (float)lua_tonumber( L, valueIndex ) );
            Real min = Rtt_FloatToReal( 0.05f );
            Real max = Rtt_FloatToReal( 20.0f );
            if ( timeScale < min )
            {
                CoronaLuaWarning(L, "sprite.timeScale must be >= %g. Using %g", min, min);
                timeScale = min;
            }
            else if ( timeScale < min )
            {
                CoronaLuaWarning(L, "sprite.timeScale must be <= %g. Using %g", max, max);
                timeScale = max;
            }
            o.SetTimeScale( timeScale );
        }
        break;

    case 1:
    case 2:
    case 3:
    case 4:
        {
            // Read-only properties
            // no-op
        }
        break;

    default:
        {
            result = Super::SetValueForKey( L, object, key, valueIndex );
        }
        break;
    }

    return result;
}

const LuaProxyVTable&
LuaSpriteObjectProxyVTable::Parent() const
{
    return Super::Constant();
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
