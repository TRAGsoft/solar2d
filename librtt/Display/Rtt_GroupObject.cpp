//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_BitmapMask.h"
#include "Display/Rtt_BitmapPaint.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_Scene.h"
#include "Display/Rtt_StageObject.h"
#include "Renderer/Rtt_Renderer.h"
#include "Rtt_LuaProxyVTable.h"

#include "Rtt_Profiling.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

static void
MakeUnreachable( lua_State *L, Scene& scene, DisplayObject& object )
{
    // For objects that are groups, make the children unreachable
    GroupObject *group = object.AsGroupObject();
    if ( group )
    {
        for ( int i = group->NumChildren(); --i >= 0; )
        {
            DisplayObject& child = group->ChildAt( i );
            MakeUnreachable( L, scene, child );
        }
    }

    // Clear the focus from the now unreachable object
    StageObject& stage = scene.CurrentStage();
    DisplayObject *focus = stage.GetFocus();
    if ( & object == focus )
    {
        stage.SetFocus(NULL);
    }
    stage.SetFocus( & object, NULL ); // Explicit removal

    object.FinalizeSelf( L );
}

void
GroupObject::CollectUnreachables( lua_State *L, Scene& scene, GroupObject& group )
{
    for ( int i = group.NumChildren(); --i >= 0; )
    {
        DisplayObject& child = group.ChildAt( i );

        MakeUnreachable( L, scene, child );

        Rtt_ASSERT( ! child.IsReachable() );
        group.Remove( i );
    }
}

void
GroupObject::ReleaseChildrenLuaReferences( lua_State *L )
{
    for ( int i = NumChildren(); --i >= 0; )
    {
        DisplayObject& child = ChildAt( i );

        if ( child.GetParent() != this )
        {
            continue;
        }

        GroupObject *group = child.AsGroupObject();
        if ( group )
        {
            group->ReleaseChildrenLuaReferences( L );
        }
        child.FinalizeSelf( L );
    }
}

// ----------------------------------------------------------------------------

GroupObject::GroupObject( Rtt_Allocator* pAllocator, StageObject* canvas )
:    Super(),
    fStage( canvas ),
    fSelfBoundsXMin( Rtt_REAL_0 ),
    fSelfBoundsYMin( Rtt_REAL_0 ),
    fSelfBoundsXMax( Rtt_REAL_0 ),
    fSelfBoundsYMax( Rtt_REAL_0 ),
    fHasFixedSelfBounds( false ),
    fChildrenNeedTransformUpdate( false ),
    fChildren( pAllocator )
{
    SetObjectDesc("GroupObject"); // for introspection
}

GroupObject::GroupObject( Rtt_Allocator* pAllocator, StageObject* canvas, Real width, Real height )
:    Super(),
    fStage( canvas ),
    fSelfBoundsXMin( -Rtt_RealDiv2( width > Rtt_REAL_0 ? width : Rtt_REAL_0 ) ),
    fSelfBoundsYMin( -Rtt_RealDiv2( height > Rtt_REAL_0 ? height : Rtt_REAL_0 ) ),
    fSelfBoundsXMax( Rtt_RealDiv2( width > Rtt_REAL_0 ? width : Rtt_REAL_0 ) ),
    fSelfBoundsYMax( Rtt_RealDiv2( height > Rtt_REAL_0 ? height : Rtt_REAL_0 ) ),
    fHasFixedSelfBounds( true ),
    fChildrenNeedTransformUpdate( false ),
    fChildren( pAllocator )
{
    SetObjectDesc( "GroupObject" ); // for introspection
}

GroupObject*
GroupObject::AsGroupObject()
{
    return this;
}

const GroupObject*
GroupObject::AsGroupObject() const
{
    return this;
}

bool
GroupObject::UpdateTransform( const Matrix& parentToDstSpace )
{
    bool shouldUpdate = Super::UpdateTransform( parentToDstSpace );
    bool shouldUpdateChildren = shouldUpdate || fChildrenNeedTransformUpdate;

    if ( ShouldHitTest() )
    {
		SUMMED_TIMING( gut, "Group: post-Super::UpdateTransform" );

        Rect screenBounds;

        // Ensure receiver points to same stage as its parent
        GroupObject *parent = GetParent();
        StageObject *stage = ( parent ? parent->GetStage() : GetStage() );
        SetStage( stage );

        // Get cull bounds
        if ( stage )
        {
			SUMMED_TIMING( gcb, "Group: Get Cull Bounds" );

            const Rect *snapshotBounds = stage->GetSnapshotBounds();
            screenBounds = ( snapshotBounds
                ? (* snapshotBounds)
                : stage->GetDisplay().GetScreenContentBounds() );
        }

        if ( stage && ! SkipsCull() && CanCull() )
        {
            BuildStageBounds();
            CullOffscreen( screenBounds );
        }

        if ( IsOffScreen() )
        {
            if ( shouldUpdateChildren )
            {
                // Defer child transform invalidation until this group becomes visible again.
                fChildrenNeedTransformUpdate = true;
            }
            return shouldUpdate;
        }

        const Matrix& xform = GetSrcToDstMatrix();

        U8 alphaCumulativeFromAncestors = AlphaCumulative();

		SUMMED_TIMING( ed, "Group: Visit Children" );

        for ( S32 i = 0, iMax = fChildren.Length(); i < iMax; i++ )
        {
            DisplayObject *child = fChildren[i];

            child->UpdateAlphaCumulative( alphaCumulativeFromAncestors );

            if ( shouldUpdateChildren )
            {
                // If receiver's matrix is out of date, then so are the children's
                child->Invalidate( kGeometryFlag | kTransformFlag );
            }

            child->UpdateTransform( xform );

            // Only cull objects that are hit-testable and allow culling
            if ( NULL == child->AsGroupObject()
                 && child->ShouldHitTest()
                 && ( ! child->SkipsCull() && child->CanCull() ) )
            {
                // Only leaf nodes are culled, so we only need to build stage bounds
                // of leaf nodes to determine if they should be culled.
// TODO: BuildStageBounds is expensive --- accumulate iteratively if numChildren is large
				{
					SUMMED_TIMING( bsb, "Group: Build Child Stage Bounds" );
				child->BuildStageBounds();
				}
				{
					SUMMED_TIMING( co, "Group: Cull Offscreen" );
				child->CullOffscreen( screenBounds );
				}
            }
        }

        fChildrenNeedTransformUpdate = false;
    }

    return shouldUpdate;
}

void
GroupObject::Prepare( const Display& display )
{
    Super::Prepare( display );

    // Only build if is visible in the hittest sense
    if ( ShouldHitTest() )
    {
		SUMMED_TIMING( gp, "Group: post-Super::Prepare" );

        // A child's build can be invalidated, so always traverse children

        // Propagate certain flags to children
        DirtyFlags flags = kGroupPropagationMask & GetDirtyFlags();

        for ( S32 i = 0, iMax = fChildren.Length(); i < iMax; i++ )
        {
            DisplayObject* child = fChildren[i];

            // At least one of the following must be true:
            // 1. child is not a group
            // 2. (or if it's a group then), child is onscreen
            // 3. (or if it's offscreen then), child is cullable, e.g. containers
            Rtt_ASSERT( NULL == child->AsGroupObject() || ! child->IsOffScreen() || (!child->SkipsCull() && child->CanCull()) );

            if ( ! child->IsOffScreen() )
            {
                // If the parent's build was invalidated, then we need to rebuild the children
                if ( flags > 0 )
                {
                    child->Invalidate( flags );
                }

                child->Prepare( display );
            }
        }

        SetValid();
    }
}

void
GroupObject::Draw( Renderer& renderer ) const
{
    if ( ShouldDraw() )
    {
        Rtt_ASSERT( ! IsDirty() );
        Rtt_ASSERT( ! IsOffScreen() );

		SUMMED_TIMING( gd, "Group: Draw" );

        // TODO: This needs to be done in the Build stage...
///        U8 oldAlpha = renderer.SetAlpha( Alpha(), true );

        const BitmapMask *mask = GetMask();

        if ( mask )
        {
            Texture *texture = const_cast< BitmapPaint * >( mask->GetPaint() )->GetTexture();
            Uniform *uniform = const_cast< Self * >( this )->GetMaskUniform();
            renderer.PushMask( texture, uniform );
        }

        for ( S32 i = 0, iMax = fChildren.Length(); i < iMax; i++ )
        {
            const DisplayObject *child = fChildren[i];

			if ( ! child->IsOffScreen() )
			{
				child->Draw( renderer );
			}
		}

        if ( mask )
        {
            renderer.PopMask();
        }

///        renderer.SetAlpha( oldAlpha, false );
    }
}

void
GroupObject::GetSelfBounds( Rect& rect ) const
{
    if ( fHasFixedSelfBounds )
    {
        rect.xMin = fSelfBoundsXMin;
        rect.yMin = fSelfBoundsYMin;
        rect.xMax = fSelfBoundsXMax;
        rect.yMax = fSelfBoundsYMax;
        return;
    }

    rect.SetEmpty();

    for ( S32 i = 0, iMax = fChildren.Length(); i < iMax; i++ )
    {
        Rect childRect;
        const DisplayObject* child = fChildren[i];
        child->GetSelfBounds( childRect );

		// Ensure childRect is in the same coordinate space as rect, 
		// i.e. the parent's (this's) coordinate space.  The child's transform
		// is relative to the parent's.
		child->GetMatrix().Apply( childRect ); // Super::ApplyTransform( * child, childRect );

        rect.Union( childRect );
    }
}

void
GroupObject::SetFixedSelfBounds( const Rect& rect )
{
	if ( ! fHasFixedSelfBounds
		 || ! Rtt_RealEqual( rect.xMin, fSelfBoundsXMin )
		 || ! Rtt_RealEqual( rect.yMin, fSelfBoundsYMin )
		 || ! Rtt_RealEqual( rect.xMax, fSelfBoundsXMax )
		 || ! Rtt_RealEqual( rect.yMax, fSelfBoundsYMax ) )
	{
		fSelfBoundsXMin = rect.xMin;
		fSelfBoundsYMin = rect.yMin;
		fSelfBoundsXMax = rect.xMax;
		fSelfBoundsYMax = rect.yMax;
		fHasFixedSelfBounds = true;

		Invalidate( kTransformFlag | kStageBoundsFlag );
		const_cast< Transform& >( GetTransform() ).Invalidate();
	}
}

void
GroupObject::SetSelfBounds( Real width, Real height )
{
    if ( fHasFixedSelfBounds )
    {
        // Sized groups keep the construction-time dimensions.
        return;
    }

    Super::SetSelfBounds( width, height );
}

void
GroupObject::SetAsSizedGroup( Real width, Real height )
{
	Real newWidth = ( width > Rtt_REAL_0 ? width : Rtt_REAL_0 );
	Real newHeight = ( height > Rtt_REAL_0 ? height : Rtt_REAL_0 );
	Rect currentBounds;
	GetSelfBounds( currentBounds );
	Rect newBounds;

	if ( currentBounds.NotEmpty() )
	{
		Real anchorX = GetAnchorX();
		Real anchorY = GetAnchorY();
		Real xAnchor = currentBounds.xMin + Rtt_RealMul( anchorX, currentBounds.Width() );
		Real yAnchor = currentBounds.yMin + Rtt_RealMul( anchorY, currentBounds.Height() );

		newBounds.xMin = xAnchor - Rtt_RealMul( anchorX, newWidth );
		newBounds.yMin = yAnchor - Rtt_RealMul( anchorY, newHeight );
		newBounds.xMax = newBounds.xMin + newWidth;
		newBounds.yMax = newBounds.yMin + newHeight;
	}
	else
	{
		newBounds.Initialize( Rtt_REAL_0, Rtt_REAL_0, newWidth, newHeight );
	}

	SetFixedSelfBounds( newBounds );
}

void
GroupObject::SetAsSizedGroupWithCurrentSize()
{
	Rect currentBounds;
	GetSelfBounds( currentBounds );

	if ( currentBounds.NotEmpty() )
	{
		SetAsSizedGroup( currentBounds.Width(), currentBounds.Height() );
	}
	else
	{
		SetAsSizedGroup( Rtt_REAL_0, Rtt_REAL_0 );
	}
}

bool
GroupObject::HitTest( Real contentX, Real contentY )
{
    return true;
}

bool
GroupObject::CanCull() const
{
    return fHasFixedSelfBounds;
}

bool
GroupObject::StageBoundsDependsOnChildren() const
{
    return ! fHasFixedSelfBounds;
}

const LuaProxyVTable&
GroupObject::ProxyVTable() const
{
    return LuaGroupObjectProxyVTable::Constant();
}

bool
GroupObject::ShouldOffsetWithAnchor() const
{
    return fHasFixedSelfBounds || IsProperty( kIsAnchorChildren );
}

void
GroupObject::DidInsert( bool childParentChanged )
{
    if ( childParentChanged && StageBoundsDependsOnChildren() )
    {
        Invalidate( kStageBoundsFlag );
    }
    else
    {
        InvalidateDisplay();
    }
}

void
GroupObject::DidRemove()
{
    if ( StageBoundsDependsOnChildren() )
    {
        Invalidate( kStageBoundsFlag );
    }
    else
    {
        InvalidateDisplay();
    }
}

// resetTransform only has an effect when newChild is moved to a different parent
void
GroupObject::Insert( S32 index, DisplayObject* newChild, bool resetTransform )
{
    // StageObjects cannot be inserted into groups
    if ( Rtt_VERIFY( newChild )
         && Rtt_VERIFY( newChild != (DisplayObject*)newChild->GetStage() ) )
    {
        GroupObject* oldParent = newChild->GetParent();

        // Make sure we aren't indexing beyond the array (bug http://bugs.coronalabs.com/?18838 )
        const S32 maxIndex = NumChildren();
        if ( index > maxIndex || index < 0 )
        {
            index = maxIndex;
        }
        
        if ( oldParent != this )
        {
			SUMMED_TIMING( np, "Group: Insert (new parent)" );

            // Leave it up to the caller to decide the semantics of insertion.
            // For newly-created object, we don't want to reset the transform
            // b/c we also need to translate it to the specified position.
            // For existing objects, we reset the transform b/c that's the most
            // predictable policy. Neither alternatives make sense --- (1) preserving
            // child transforms make no sense b/c they are relative to the old parent
            // and (2) preserving "absolute" pos,angle,scale is impractical.
            if ( resetTransform )
            {
                newChild->ResetTransform();
            }

            // If newChild had a parent, remove it
            if ( oldParent )
            {
				SUMMED_TIMING( rc, "Group: Insert (release child)" );

                oldParent->Release( oldParent->Find( * newChild ) );
            }

            newChild->SetParent( this );
            fChildren.Insert( index, newChild );

            // ++TransactionId();
            DidInsert( true );

            // TODO:
        }
        else
        {
			SUMMED_TIMING( sp, "Group: Insert (same parent)" );

            // newChild already belongs in this group
            S32 oldIndex = oldParent->Find( * newChild );

            // If new index is different from the old, then re-insert
            if ( index != oldIndex )
            {
                DisplayObject* o = fChildren.Release( oldIndex ); Rtt_ASSERT( o == newChild );

                // Removing an element causes the indices of all elements
                // that came after to be one less.
                if ( oldIndex < index ) { --index; }

                //++TransactionId();
                fChildren.Insert( index, o );
                DidInsert( false );
            }
        }
    }
}

void
GroupObject::Remove( S32 index )
{
    fChildren.Remove( index, 1 );

    //++TransactionId();
    DidRemove();
}

DisplayObject*
GroupObject::Release( S32 index )
{
    DisplayObject* child = NULL;

    if (index < NumChildren())
    {
        child = fChildren.Release( index );
        child->SetParent( NULL );

        //++TransactionId();
        DidRemove();
    }

    return child;
}

S32
GroupObject::Find( const DisplayObject& child ) const
{
	SUMMED_TIMING( fc, "Group: Find child" );

    S32 i = 0, iMax = fChildren.Length();
    for ( ;
          i < iMax && ( & child != fChildren[i] );
          i++ )
    {
    }

    return ( i < iMax ? i : -1 );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
