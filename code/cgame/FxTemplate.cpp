/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "common_headers.h"

#if !defined(FX_SCHEDULER_H_INC)
	#include "FxScheduler.h"
#endif

#include "../game/genericparser2.h"
#include "qcommon/safe/string.h"

#include <array>

//------------------------------------------------------
// CPrimitiveTemplate
//	Set up our minimal default values
//
// Input:
//	none
//
// Return:
//	none
//------------------------------------------------------
CPrimitiveTemplate::CPrimitiveTemplate()
{
	// We never start out as a copy or with a name
	mCopy = false;
	mName[0] = 0;
	mCullRange = 0;

	mFlags = mSpawnFlags = 0;

	mLife.SetRange( 50.0f, 50.0f );
	mSpawnCount.SetRange( 1.0f, 1.0f );
	mRadius.SetRange( 10.0f, 10.0f );
	mHeight.SetRange( 10.0f, 10.0f );
	mWindModifier.SetRange( 1.0f, 1.0f );

	VectorSet( mMin, 0.0f, 0.0f, 0.0f );
	VectorSet( mMax, 0.0f, 0.0f, 0.0f );

	mRedStart.SetRange( 1.0f, 1.0f );
	mGreenStart.SetRange( 1.0f, 1.0f );
	mBlueStart.SetRange( 1.0f, 1.0f );

	mRedEnd.SetRange( 1.0f, 1.0f );
	mGreenEnd.SetRange( 1.0f, 1.0f );
	mBlueEnd.SetRange( 1.0f, 1.0f );

	mAlphaStart.SetRange( 1.0f, 1.0f );
	mAlphaEnd.SetRange( 1.0f, 1.0f );

	mSizeStart.SetRange( 1.0f, 1.0f );
	mSizeEnd.SetRange( 1.0f, 1.0f );

	mSize2Start.SetRange( 1.0f, 1.0f );
	mSize2End.SetRange( 1.0f, 1.0f );

	mLengthStart.SetRange( 1.0f, 1.0f );
	mLengthEnd.SetRange( 1.0f, 1.0f );

	mTexCoordS.SetRange( 1.0f, 1.0f );
	mTexCoordT.SetRange( 1.0f, 1.0f );

	mVariance.SetRange( 1.0f, 1.0f );
	mDensity.SetRange( 10.0f, 10.0f );// default this high so it doesn't do bad things

	// no participating medium unless the primitive has a volumetricMedia group
	mVolMedia = false;
	mVolExtinction.SetRange( 0.02f, 0.02f );
	mVolHasAlbedo = false;
	VectorSet( mVolAlbedo, 1.0f, 1.0f, 1.0f );
	mVolRadiusScale = 0.75f;
	VectorSet( mVolAspect, 1.0f, 1.0f, 1.0f );
	mVolSoftness = 0.5f;
	VectorClear( mVolEmissive );
	mVolEmissiveDensity = 0.0f;
	mVolEmissiveTint = false;
	mVolHasAnisotropy = false;
	mVolAnisotropy = 0.0f;

	// a point light unless the Light has a spot group
	mSpot = false;
	mSpotInner.SetRange( 20.0f, 20.0f );
	mSpotOuter.SetRange( 30.0f, 30.0f );
	VectorSet( mSpotDir, 1.0f, 0.0f, 0.0f );
	mSpotShadows = true;
	mSpotCookie = 0;
	mSpotCookieRoll = 0.0f;
}

//-----------------------------------------------------------
void CPrimitiveTemplate::operator=(const CPrimitiveTemplate &that)
{
	// I'm assuming that doing a memcpy wouldn't work here
	// If you are looking at this and know a better way to do this, please tell me.
	strcpy( mName, that.mName );

	mType				= that.mType;

	mSpawnDelay			= that.mSpawnDelay;
	mSpawnCount			= that.mSpawnCount;
	mLife				= that.mLife;
	mCullRange			= that.mCullRange;

	mMediaHandles		= that.mMediaHandles;
	mPhysicalEffect = that.mPhysicalEffect;
	mPhysicalShaders = that.mPhysicalShaders;
	mPhysicalAdvancedReady = that.mPhysicalAdvancedReady;
	mImpactFxHandles	= that.mImpactFxHandles;
	mDeathFxHandles		= that.mDeathFxHandles;
	mEmitterFxHandles	= that.mEmitterFxHandles;
	mPlayFxHandles		= that.mPlayFxHandles;

	mFlags				= that.mFlags;
	mSpawnFlags			= that.mSpawnFlags;

	VectorCopy( that.mMin, mMin );
	VectorCopy( that.mMax, mMax );

	mOrigin1X			= that.mOrigin1X;
	mOrigin1Y			= that.mOrigin1Y;
	mOrigin1Z			= that.mOrigin1Z;

	mOrigin2X			= that.mOrigin2X;
	mOrigin2Y			= that.mOrigin2Y;
	mOrigin2Z			= that.mOrigin2Z;

	mRadius				= that.mRadius;
	mHeight				= that.mHeight;
	mWindModifier		= that.mWindModifier;

	mRotation			= that.mRotation;
	mRotationDelta		= that.mRotationDelta;

	mAngle1				= that.mAngle1;
	mAngle2				= that.mAngle2;
	mAngle3				= that.mAngle3;

	mAngle1Delta		= that.mAngle1Delta;
	mAngle2Delta		= that.mAngle2Delta;
	mAngle3Delta		= that.mAngle3Delta;

	mVelX				= that.mVelX;
	mVelY				= that.mVelY;
	mVelZ				= that.mVelZ;

	mAccelX				= that.mAccelX;
	mAccelY				= that.mAccelY;
	mAccelZ				= that.mAccelZ;

	mGravity			= that.mGravity;

	mDensity			= that.mDensity;
	mVariance			= that.mVariance;

	mRedStart			= that.mRedStart;
	mGreenStart			= that.mGreenStart;
	mBlueStart			= that.mBlueStart;

	mRedEnd				= that.mRedEnd;
	mGreenEnd			= that.mGreenEnd;
	mBlueEnd			= that.mBlueEnd;

	mRGBParm			= that.mRGBParm;

	mAlphaStart			= that.mAlphaStart;
	mAlphaEnd			= that.mAlphaEnd;
	mAlphaParm			= that.mAlphaParm;

	mSizeStart			= that.mSizeStart;
	mSizeEnd			= that.mSizeEnd;
	mSizeParm			= that.mSizeParm;

	mSize2Start			= that.mSize2Start;
	mSize2End			= that.mSize2End;
	mSize2Parm			= that.mSize2Parm;

	mLengthStart		= that.mLengthStart;
	mLengthEnd			= that.mLengthEnd;
	mLengthParm			= that.mLengthParm;

	mTexCoordS			= that.mTexCoordS;
	mTexCoordT			= that.mTexCoordT;

	mElasticity			= that.mElasticity;

	mVolMedia			= that.mVolMedia;
	mVolExtinction		= that.mVolExtinction;
	mVolHasAlbedo		= that.mVolHasAlbedo;
	VectorCopy( that.mVolAlbedo, mVolAlbedo );
	mVolRadiusScale		= that.mVolRadiusScale;
	VectorCopy( that.mVolAspect, mVolAspect );
	mVolSoftness		= that.mVolSoftness;
	VectorCopy( that.mVolEmissive, mVolEmissive );
	mVolEmissiveDensity	= that.mVolEmissiveDensity;
	mVolEmissiveTint	= that.mVolEmissiveTint;
	mVolHasAnisotropy	= that.mVolHasAnisotropy;
	mVolAnisotropy		= that.mVolAnisotropy;

	mSpot				= that.mSpot;
	mSpotInner			= that.mSpotInner;
	mSpotOuter			= that.mSpotOuter;
	VectorCopy( that.mSpotDir, mSpotDir );
	mSpotShadows		= that.mSpotShadows;
	mSpotCookie			= that.mSpotCookie;
	mSpotCookieRoll		= that.mSpotCookieRoll;
}

//------------------------------------------------------
// ParseFloat
//	Removes up to two values from a passed in string and
//	sets these values into the passed in min and max
//	fields.  if no max is present, min is copied into it.
//
// input:
//	string that contains up to two float values
//  min & max are used to return the parse values
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseFloat( const gsl::cstring_span& val, float& min, float& max )
{
	// attempt to read out the values
	int v = Q::sscanf( val, min, max );

	if ( v == 0 )
	{ // nothing was there, failure
		return false;
	}
	else if ( v == 1 )
	{ // only one field entered, this is ok, but we should copy min into max
		max = min;
	}

	return true;
}


//------------------------------------------------------
// ParseVector
//	Removes up to six values from a passed in string and
//	sets these values into the passed in min and max vector
//	fields. if no max is present, min is copied into it.
//
// input:
//	string that contains up to six float values
//  min & max are used to return the parse values
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseVector( const gsl::cstring_span& val, vec3_t min, vec3_t max )
{
	// we don't allow passing in a null
	if ( min == nullptr || max == nullptr )
	{
		return false;
	}

	// attempt to read out our values
	int v = Q::sscanf( val, min[0], min[1], min[2], max[0], max[1], max[2] );

	// Check for completeness
	if ( v < 3 || v == 4 || v == 5 )
	{ // not a complete value
		return false;
	}
	else if ( v == 3 )
	{ // only a min was entered, so copy the result into max
		VectorCopy( min, max );
	}

	return true;
}

namespace detail
{
	// calls Q::sscanf with the elements of the given array as arguments

	template< std::size_t remaining >
	struct ScanStrings
	{
		template< std::size_t count, typename... Args >
		static int call( const gsl::cstring_span& val, std::array< gsl::cstring_span, count >& arr, Args&... args )
		{
			return ScanStrings< remaining - 1 >::call( val, arr, arr[ remaining - 1 ], args... );
		}
	};

	template<>
	struct ScanStrings< 0 >
	{
		template< std::size_t count, typename... Args >
		static int call( const gsl::cstring_span& val, std::array< gsl::cstring_span, count >& arr, Args&... args )
		{
			return Q::sscanf( val, args... );
		}
	};
}

template< std::size_t count >
static gsl::span< gsl::cstring_span > scanStrings( const gsl::cstring_span& val, std::array< gsl::cstring_span, count >& arr )
{
	int numParsed = detail::ScanStrings< count >::call( val, arr );
	return{ arr.data(), arr.data() + numParsed };
}

//------------------------------------------------------
// ParseGroupFlags
//	Group flags are generic in nature, so we can easily
//	use a generic function to parse them in, then the
//	caller can shift them into the appropriate range.
//
// input:
//	string that contains the flag strings
//  *flags returns the set bit flags
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseGroupFlags( const gsl::cstring_span& val, int& flags )
{
	// For a sub group, really you probably only have one or two flags set
	std::array< gsl::cstring_span, 4 > flag;

	const auto availableFlag = scanStrings( val, flag );

	// Clear out the flags field, then convert the flag string to an actual value ( use generic flags )
	flags = 0;

	bool ok = true;
	for( auto& cur : availableFlag  )
	{
		static StringViewIMap< int > flagNames{
			{ CSTRING_VIEW( "linear" ), FX_LINEAR },
			{ CSTRING_VIEW( "nonlinear" ), FX_NONLINEAR },
			{ CSTRING_VIEW( "wave" ), FX_WAVE },
			{ CSTRING_VIEW( "random" ), FX_RAND },
			{ CSTRING_VIEW( "clamp" ), FX_CLAMP },
		};

		auto pos = flagNames.find( cur );
		if( pos == flagNames.end() )
		{
			ok = false;
		}
		else
		{
			flags |= pos->second;
		}
	}

	return ok;
}

//------------------------------------------------------
// ParseMin
//	Reads in a min bounding box field in vector format
//
// input:
//	string that contains three float values
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseMin( const gsl::cstring_span& val )
{
	vec3_t min;

	if ( ParseVector( val, min, min ) == true )
	{
		VectorCopy( min, mMin );

		// We assume that if a min is being set that we are using physics and a bounding box
		mFlags |= (FX_USE_BBOX | FX_APPLY_PHYSICS);
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseMax
//	Reads in a max bounding box field in vector format
//
// input:
//	string that contains three float values
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseMax( const gsl::cstring_span& val )
{
	vec3_t max;

	if ( ParseVector( val, max, max ) == true )
	{
		VectorCopy( max, mMax );

		// We assume that if a max is being set that we are using physics and a bounding box
		mFlags |= (FX_USE_BBOX | FX_APPLY_PHYSICS);
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseLife
//	Reads in a ranged life value
//
// input:
//	string that contains a float range ( two vals )
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseLife( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mLife.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseDelay
//	Reads in a ranged delay value
//
// input:
//	string that contains a float range ( two vals )
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseDelay( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSpawnDelay.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseCount
//	Reads in a ranged count value
//
// input:
//	string that contains a float range ( two vals )
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseCount( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSpawnCount.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseElasticity
//	Reads in a ranged elasticity value
//
// input:
//	string that contains a float range ( two vals )
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseElasticity( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mElasticity.SetRange( min, max );

		// We assume that if elasticity is set that we are using physics, but don't assume we are
		//	using a bounding box unless a min/max are explicitly set
//		mFlags |= FX_APPLY_PHYSICS;
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseOrigin1
//	Reads in an origin field in vector format
//
// input:
//	string that contains three float values
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseOrigin1( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mOrigin1X.SetRange( min[0], max[0] );
		mOrigin1Y.SetRange( min[1], max[1] );
		mOrigin1Z.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseOrigin2
//	Reads in an origin field in vector format
//
// input:
//	string that contains three float values
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseOrigin2( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mOrigin2X.SetRange( min[0], max[0] );
		mOrigin2Y.SetRange( min[1], max[1] );
		mOrigin2Z.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRadius
//	Reads in a ranged radius value
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRadius( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mRadius.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseHeight
//	Reads in a ranged height value
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseHeight( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mHeight.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseWindModifier
//	Reads in a ranged wind modifier value
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseWindModifier( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mWindModifier.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRotation
//	Reads in a ranged rotation value
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRotation( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == qtrue )
	{
		mRotation.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRotationDelta
//	Reads in a ranged rotationDelta value
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRotationDelta( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == qtrue )
	{
		mRotationDelta.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseAngle
//	Reads in a ranged angle field in vector format
//
// input:
//	string that contains one or two vectors
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAngle( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mAngle1.SetRange( min[0], max[0] );
		mAngle2.SetRange( min[1], max[1] );
		mAngle3.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseAngleDelta
//	Reads in a ranged angleDelta field in vector format
//
// input:
//	string that contains one or two vectors
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAngleDelta( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mAngle1Delta.SetRange( min[0], max[0] );
		mAngle2Delta.SetRange( min[1], max[1] );
		mAngle3Delta.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseVelocity
//	Reads in a ranged velocity field in vector format
//
// input:
//	string that contains one or two vectors
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseVelocity( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mVelX.SetRange( min[0], max[0] );
		mVelY.SetRange( min[1], max[1] );
		mVelZ.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseFlags
//	These are flags that are not specific to a group,
//	rather, they are specific to the whole primitive.
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseFlags( const gsl::cstring_span& val )
{
	// For a primitive, really you probably only have two or less flags set
	std::array< gsl::cstring_span, 7 > flag;

	const auto availableFlag = scanStrings( val, flag );

	bool	ok = true;
	for( auto& cur : availableFlag )
	{
		static StringViewIMap< int > flagNames{
			{ CSTRING_VIEW( "useModel" ), FX_ATTACHED_MODEL },
			{ CSTRING_VIEW( "useBBox" ), FX_USE_BBOX },
			{ CSTRING_VIEW( "usePhysics" ), FX_APPLY_PHYSICS },
			{ CSTRING_VIEW( "expensivePhysics" ), FX_EXPENSIVE_PHYSICS },
			//rww - begin g2 stuff
			{ CSTRING_VIEW( "ghoul2Collision" ), ( FX_GHOUL2_TRACE | FX_APPLY_PHYSICS | FX_EXPENSIVE_PHYSICS ) },
			{ CSTRING_VIEW( "ghoul2Decals" ), FX_GHOUL2_DECALS },
			//rww - end
			{ CSTRING_VIEW( "impactKills" ), FX_KILL_ON_IMPACT },
			{ CSTRING_VIEW( "impactFx" ), FX_IMPACT_RUNS_FX },
			{ CSTRING_VIEW( "deathFx" ), FX_DEATH_RUNS_FX },
			{ CSTRING_VIEW( "useAlpha" ), FX_USE_ALPHA },
			{ CSTRING_VIEW( "emitFx" ), FX_EMIT_FX },
			{ CSTRING_VIEW( "depthHack" ), FX_DEPTH_HACK },
			{ CSTRING_VIEW( "setShaderTime" ), FX_SET_SHADER_TIME },
		};

		auto pos = flagNames.find( cur );
		if( pos == flagNames.end() )
		{ // we have badness going on, but continue on in case there are any valid fields in here
			ok = false;
		}
		else
		{
			mFlags |= pos->second;
		}
	}

	return ok;
}

//------------------------------------------------------
// ParseSpawnFlags
//	These kinds of flags control how things spawn.  They
//	never get passed on to a primitive.
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSpawnFlags( const gsl::cstring_span& val )
{
	std::array< gsl::cstring_span, 7 > flag;

	// For a primitive, really you probably only have two or less flags set
	const auto availableFlag = scanStrings( val, flag );

	bool ok = true;
	for( auto& cur : availableFlag )
	{
		static StringViewIMap< int > flagNames{
			{ CSTRING_VIEW( "org2fromTrace" ), FX_ORG2_FROM_TRACE },
			{ CSTRING_VIEW( "traceImpactFx" ), FX_TRACE_IMPACT_FX },
			{ CSTRING_VIEW( "org2isOffset" ), FX_ORG2_IS_OFFSET },
			{ CSTRING_VIEW( "cheapOrgCalc" ), FX_CHEAP_ORG_CALC },
			{ CSTRING_VIEW( "cheapOrg2Calc" ), FX_CHEAP_ORG2_CALC },
			{ CSTRING_VIEW( "absoluteVel" ), FX_VEL_IS_ABSOLUTE },
			{ CSTRING_VIEW( "absoluteAccel" ), FX_ACCEL_IS_ABSOLUTE },
			{ CSTRING_VIEW( "orgOnSphere" ), FX_ORG_ON_SPHERE },
			{ CSTRING_VIEW( "orgOnCylinder" ), FX_ORG_ON_CYLINDER },
			{ CSTRING_VIEW( "axisFromSphere" ), FX_AXIS_FROM_SPHERE },
			{ CSTRING_VIEW( "randrotaroundfwd" ), FX_RAND_ROT_AROUND_FWD },
			{ CSTRING_VIEW( "evenDistribution" ), FX_EVEN_DISTRIBUTION },
			{ CSTRING_VIEW( "rgbComponentInterpolation" ), FX_RGB_COMPONENT_INTERP },
			{ CSTRING_VIEW( "lessAttenuation" ), FX_SND_LESS_ATTENUATION },
		};
		auto pos = flagNames.find( cur );
		if( pos == flagNames.end() )
		{
			ok = false;
		}
		else
		{
			mSpawnFlags |= pos->second;
		}
	}

	return ok;
}

//------------------------------------------------------
// ParseAcceleration
//	Reads in a ranged acceleration field in vector format
//
// input:
//	string that contains one or two vectors
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAcceleration( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mAccelX.SetRange( min[0], max[0] );
		mAccelY.SetRange( min[1], max[1] );
		mAccelZ.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseGravity
//	Reads in a ranged gravity value
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseGravity( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mGravity.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseDensity
//	Reads in a ranged density value.  Density is only
//	for emitters that are calling effects...it basically
//	specifies how often the emitter should emit fx.
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseDensity( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mDensity.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseVariance
//	Reads in a ranged variance value.  Variance is only
//	valid for emitters that are calling effects...
//	it basically determines the amount of slop in the
//	density calculations
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseVariance( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mVariance.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRGBStart
//	Reads in a ranged rgbStart field in vector format
//
// input:
//	string that contains one or two vectors
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRGBStart( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mRedStart.SetRange( min[0], max[0] );
		mGreenStart.SetRange( min[1], max[1] );
		mBlueStart.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRGBEnd
//	Reads in a ranged rgbEnd field in vector format
//
// input:
//	string that contains one or two vectors
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRGBEnd( const gsl::cstring_span& val )
{
	vec3_t min, max;

	if ( ParseVector( val, min, max ) == true )
	{
		mRedEnd.SetRange( min[0], max[0] );
		mGreenEnd.SetRange( min[1], max[1] );
		mBlueEnd.SetRange( min[2], max[2] );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRGBParm
//	Reads in a ranged rgbParm field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRGBParm( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mRGBParm.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseRGBFlags
//	Reads in a set of rgbFlags in string format
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRGBFlags( const gsl::cstring_span& val )
{
	int flags;

	if ( ParseGroupFlags( val, flags ) == true )
	{
		// Convert our generic flag values into type specific ones
		mFlags |= ( flags << FX_RGB_SHIFT );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseAlphaStart
//	Reads in a ranged alphaStart field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAlphaStart( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mAlphaStart.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseAlphaEnd
//	Reads in a ranged alphaEnd field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAlphaEnd( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mAlphaEnd.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseAlphaParm
//	Reads in a ranged alphaParm field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAlphaParm( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mAlphaParm.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseAlphaFlags
//	Reads in a set of alphaFlags in string format
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAlphaFlags( const gsl::cstring_span& val )
{
	int flags;

	if ( ParseGroupFlags( val, flags ) == true )
	{
		// Convert our generic flag values into type specific ones
		mFlags |= ( flags << FX_ALPHA_SHIFT );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSizeStart
//	Reads in a ranged sizeStart field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSizeStart( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSizeStart.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSizeEnd
//	Reads in a ranged sizeEnd field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSizeEnd( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSizeEnd.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSizeParm
//	Reads in a ranged sizeParm field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSizeParm( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSizeParm.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSizeFlags
//	Reads in a set of sizeFlags in string format
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSizeFlags( const gsl::cstring_span& val )
{
	int flags;

	if ( ParseGroupFlags( val, flags ) == true )
	{
		// Convert our generic flag values into type specific ones
		mFlags |= ( flags << FX_SIZE_SHIFT );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSize2Start
//	Reads in a ranged Size2Start field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSize2Start( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSize2Start.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSize2End
//	Reads in a ranged Size2End field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSize2End( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSize2End.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSize2Parm
//	Reads in a ranged Size2Parm field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSize2Parm( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mSize2Parm.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseSize2Flags
//	Reads in a set of Size2Flags in string format
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSize2Flags( const gsl::cstring_span& val )
{
	int flags;

	if ( ParseGroupFlags( val, flags ) == true )
	{
		// Convert our generic flag values into type specific ones
		mFlags |= ( flags << FX_SIZE2_SHIFT );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseLengthStart
//	Reads in a ranged lengthStart field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseLengthStart( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mLengthStart.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseLengthEnd
//	Reads in a ranged lengthEnd field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseLengthEnd( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mLengthEnd.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseLengthParm
//	Reads in a ranged lengthParm field in float format
//
// input:
//	string that contains one or two floats
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseLengthParm( const gsl::cstring_span& val )
{
	float min, max;

	if ( ParseFloat( val, min, max ) == true )
	{
		mLengthParm.SetRange( min, max );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseLengthFlags
//	Reads in a set of lengthFlags in string format
//
// input:
//	string that contains the flag strings
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseLengthFlags( const gsl::cstring_span& val )
{
	int flags;

	if ( ParseGroupFlags( val, flags ) == true )
	{
		// Convert our generic flag values into type specific ones
		mFlags |= ( flags << FX_LENGTH_SHIFT );
		return true;
	}

	return false;
}

//------------------------------------------------------
// ParseShaders
//	Reads in a group of shaders and registers them
//
// input:
//	Parse group that contains the list of shaders to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseShaders( const CGPProperty& grp )
{
	bool any = false;
	for( auto& value : grp.GetValues() )
	{
		if( !value.empty() )
		{
			any = true;
			int handle = theFxHelper.RegisterShader( value );
			mMediaHandles.AddHandle( handle );
			FxPhysical::Alternative alternative;
			alternative.handle = handle;
			alternative.shader = FxPhysical::ShaderPath(std::string(value.begin(), value.end()));
			mPhysicalShaders.push_back(alternative);
		}
	}
	if( !any )
	{
		// empty "list"
		theFxHelper.Print( "CPrimitiveTemplate::ParseShaders called with an empty list!\n" );
		return false;
	}
	return true;
}

//------------------------------------------------------
// ParseSounds
//	Reads in a group of sounds and registers them
//
// input:
//	Parse group that contains the list of sounds to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSounds( const CGPProperty& grp )
{
	bool any = false;
	for( auto& value : grp.GetValues() )
	{
		if( !value.empty() )
		{
			any = true;
			int handle = theFxHelper.RegisterSound( value );
			mMediaHandles.AddHandle( handle );
		}
	}
	if( !any )
	{
		// empty "list"
		theFxHelper.Print( "CPrimitiveTemplate::ParseSounds called with an empty list!\n" );
		return false;
	}
	return true;
}

//------------------------------------------------------
// ParseModels
//	Reads in a group of models and registers them
//
// input:
//	Parse group that contains the list of models to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseModels( const CGPProperty& grp )
{
	bool any = false;
	for( auto& value : grp.GetValues() )
	{
		if( !value.empty() )
		{
			any = true;
			int handle = theFxHelper.RegisterModel( value );
			mMediaHandles.AddHandle( handle );
		}
	}
	if( !any )
	{
		// empty "list"
		theFxHelper.Print( "CPrimitiveTemplate::ParseModels called with an empty list!\n" );
		return false;
	}
	mFlags |= FX_ATTACHED_MODEL;
	return true;
}

static bool ParseFX( const CGPProperty& grp, CFxScheduler& scheduler, CMediaHandles& handles, SFxHelper& helper, int& flags, int successFlags, gsl::czstring loadError, gsl::czstring emptyError )
{
	bool any = false;
	for( auto& value : grp.GetValues() )
	{
		if( !value.empty() )
		{
			any = true;
			// TODO: string_view parameter
			int handle = scheduler.RegisterEffect( std::string( value.begin(), value.end() ).c_str() );
			if( handle )
			{
				handles.AddHandle( handle );
				flags |= successFlags;
			}
			else
			{
				helper.Print( "%s", loadError );
			}
		}
	}
	if( !any )
	{
		helper.Print( "%s", emptyError );
	}
	return any;
}

//------------------------------------------------------
// ParseImpactFxStrings
//	Reads in a group of fx file names and registers them
//
// input:
//	Parse group that contains the list of fx to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseImpactFxStrings( const CGPProperty& grp )
{
	return ParseFX(
		grp,
		theFxScheduler, mImpactFxHandles, theFxHelper,
		mFlags, FX_IMPACT_RUNS_FX | FX_APPLY_PHYSICS,
		"FxTemplate: Impact effect file not found.\n",
		"CPrimitiveTemplate::ParseImpactFxStrings called with an empty list!\n"
		);
}

//------------------------------------------------------
// ParseDeathFxStrings
//	Reads in a group of fx file names and registers them
//
// input:
//	Parse group that contains the list of fx to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseDeathFxStrings( const CGPProperty& grp )
{
	return ParseFX(
		grp,
		theFxScheduler, mDeathFxHandles, theFxHelper,
		mFlags, FX_DEATH_RUNS_FX,
		"FxTemplate: Death effect file not found.\n",
		"CPrimitiveTemplate::ParseDeathFxStrings called with an empty list!\n"
		);
}

//------------------------------------------------------
// ParseEmitterFxStrings
//	Reads in a group of fx file names and registers them
//
// input:
//	Parse group that contains the list of fx to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseEmitterFxStrings( const CGPProperty& grp )
{
	return ParseFX(
		grp,
		theFxScheduler, mEmitterFxHandles, theFxHelper,
		mFlags, FX_EMIT_FX,
		"FxTemplate: Emitter effect file not found.\n",
		"CPrimitiveTemplate::ParseEmitterFxStrings called with an empty list!\n"
		);
}

//------------------------------------------------------
// ParsePlayFxStrings
//	Reads in a group of fx file names and registers them
//
// input:
//	Parse group that contains the list of fx to parse
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParsePlayFxStrings( const CGPProperty& grp )
{
	return ParseFX(
		grp,
		theFxScheduler, mPlayFxHandles, theFxHelper,
		mFlags, 0,
		"FxTemplate: Effect file not found.\n",
		"CPrimitiveTemplate::ParsePlayFxStrings called with an empty list!\n"
		);
}

bool CPrimitiveTemplate::ParseGroup( const CGPGroup& grp, const StringViewIMap< ParseMethod >& parseMethods, gsl::czstring name )
{
	for( auto& cur : grp.GetProperties() )
	{
		auto pos = parseMethods.find( cur.GetName() );
		if( pos == parseMethods.end() )
		{
			theFxHelper.Print( "Unknown key parsing %s group!", name );
		}
		else
		{
			ParseMethod method = pos->second;
			( this->*method )( cur.GetTopValue() );
		}
	}
	return true;
}

//------------------------------------------------------
// ParseRGB
//	Takes an RGB group and chomps out any pairs contained
//	in it.
//
// input:
//	the parse group to process
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseRGB( const CGPGroup& grp )
{
	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "start" ), &CPrimitiveTemplate::ParseRGBStart },

		{ CSTRING_VIEW( "end" ), &CPrimitiveTemplate::ParseRGBEnd },

		{ CSTRING_VIEW( "parm" ), &CPrimitiveTemplate::ParseRGBParm },
		{ CSTRING_VIEW( "parms" ), &CPrimitiveTemplate::ParseRGBParm },

		{ CSTRING_VIEW( "flag" ), &CPrimitiveTemplate::ParseRGBFlags },
		{ CSTRING_VIEW( "flags" ), &CPrimitiveTemplate::ParseRGBFlags },
	};
	return ParseGroup( grp, parseMethods, "RGB" );
}

//------------------------------------------------------
// ParseAlpha
//	Takes an alpha group and chomps out any pairs contained
//	in it.
//
// input:
//	the parse group to process
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseAlpha( const CGPGroup& grp )
{
	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "start" ), &CPrimitiveTemplate::ParseAlphaStart },

		{ CSTRING_VIEW( "end" ), &CPrimitiveTemplate::ParseAlphaEnd },

		{ CSTRING_VIEW( "parm" ), &CPrimitiveTemplate::ParseAlphaParm },
		{ CSTRING_VIEW( "parms" ), &CPrimitiveTemplate::ParseAlphaParm },

		{ CSTRING_VIEW( "flag" ), &CPrimitiveTemplate::ParseAlphaFlags },
		{ CSTRING_VIEW( "flags" ), &CPrimitiveTemplate::ParseAlphaFlags },
	};
	return ParseGroup( grp, parseMethods, "Alpha" );
}

//------------------------------------------------------
// ParseSize
//	Takes a size group and chomps out any pairs contained
//	in it.
//
// input:
//	the parse group to process
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSize( const CGPGroup& grp )
{
	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "start" ), &CPrimitiveTemplate::ParseSizeStart },

		{ CSTRING_VIEW( "end" ), &CPrimitiveTemplate::ParseSizeEnd },

		{ CSTRING_VIEW( "parm" ), &CPrimitiveTemplate::ParseSizeParm },
		{ CSTRING_VIEW( "parms" ), &CPrimitiveTemplate::ParseSizeParm },

		{ CSTRING_VIEW( "flag" ), &CPrimitiveTemplate::ParseSizeFlags },
		{ CSTRING_VIEW( "flags" ), &CPrimitiveTemplate::ParseSizeFlags },
	};
	return ParseGroup( grp, parseMethods, "Size" );
}

//------------------------------------------------------
// ParseSize2
//	Takes a Size2 group and chomps out any pairs contained
//	in it.
//
// input:
//	the parse group to process
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSize2( const CGPGroup& grp )
{
	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "start" ), &CPrimitiveTemplate::ParseSize2Start },

		{ CSTRING_VIEW( "end" ), &CPrimitiveTemplate::ParseSize2End },

		{ CSTRING_VIEW( "parm" ), &CPrimitiveTemplate::ParseSize2Parm },
		{ CSTRING_VIEW( "parms" ), &CPrimitiveTemplate::ParseSize2Parm },

		{ CSTRING_VIEW( "flag" ), &CPrimitiveTemplate::ParseSize2Flags },
		{ CSTRING_VIEW( "flags" ), &CPrimitiveTemplate::ParseSize2Flags },
	};
	return ParseGroup( grp, parseMethods, "Size2" );
}

//------------------------------------------------------
// ParseLength
//	Takes a length group and chomps out any pairs contained
//	in it.
//
// input:
//	the parse group to process
//
// return:
//	success of parse operation.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseLength( const CGPGroup& grp )
{
	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "start" ), &CPrimitiveTemplate::ParseLengthStart },

		{ CSTRING_VIEW( "end" ), &CPrimitiveTemplate::ParseLengthEnd },

		{ CSTRING_VIEW( "parm" ), &CPrimitiveTemplate::ParseLengthParm },
		{ CSTRING_VIEW( "parms" ), &CPrimitiveTemplate::ParseLengthParm },

		{ CSTRING_VIEW( "flag" ), &CPrimitiveTemplate::ParseLengthFlags },
		{ CSTRING_VIEW( "flags" ), &CPrimitiveTemplate::ParseLengthFlags },
	};
	return ParseGroup( grp, parseMethods, "Length" );
}


//------------------------------------------------------
// ParseVolumetricMedia
//	The participating medium of a particle (rend2 volumetric FX particles,
//	docs/rend2-volumetric-fog.md "FX particle media"). Its presence turns it on:
//
//	volumetricMedia
//	{
//		extinction	0.02 0.03	// per world unit at full alpha, range
//		albedo		0.2 0.2 0.2	// optional, default = the start rgb
//		radiusScale	0.75		// optional, proxy radius / sprite radius
//		aspect		1 1 0.7		// optional, ellipsoid scale along world x y z
//		softness	0.5			// optional, soft part of the radius 0..1
//		emissive	6 3 1.2		// optional glow: scene linear HDR radiance of the
//								// opaque medium; visible by itself, lights nothing
//		emissiveDensity	0.02	// optional, per world unit (default: the extinction)
//		emissiveTint	1		// optional, times the particle's current rgb
//		anisotropy	0.6			// optional, Henyey-Greenstein g -0.9..0.9 of the
//								// scattering (default r_volumetricFogAnisotropy)
//	}
//------------------------------------------------------
bool CPrimitiveTemplate::ParseVolExtinction( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mVolExtinction.SetRange( Q_max( 0.0f, min ), Q_max( 0.0f, max ) );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolAlbedo( const gsl::cstring_span& val )
{
	vec3_t min, max;
	if ( ParseVector( val, min, max ) == true )
	{
		VectorCopy( min, mVolAlbedo );
		mVolHasAlbedo = true;
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolRadiusScale( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mVolRadiusScale = Q_max( 0.0f, min );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolAspect( const gsl::cstring_span& val )
{
	vec3_t min, max;
	if ( ParseVector( val, min, max ) == true )
	{
		VectorCopy( min, mVolAspect );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolSoftness( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mVolSoftness = Com_Clamp( 0.0f, 1.0f, min );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolEmissive( const gsl::cstring_span& val )
{
	vec3_t min, max;
	if ( ParseVector( val, min, max ) == true )
	{
		VectorSet( mVolEmissive, Q_max( 0.0f, min[0] ), Q_max( 0.0f, min[1] ), Q_max( 0.0f, min[2] ) );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolEmissiveDensity( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mVolEmissiveDensity = Q_max( 0.0f, min );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolEmissiveTint( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mVolEmissiveTint = ( min > 0.5f );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolAnisotropy( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mVolAnisotropy = Com_Clamp( -0.9f, 0.9f, min );
		mVolHasAnisotropy = true;
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseVolumetricMedia( const CGPGroup& grp )
{
	if ( mType != Particle && mType != OrientedParticle )
	{
		theFxHelper.Print( "volumetricMedia is only supported by Particle and OrientedParticle, ignored\n" );
		return false;
	}

	mVolMedia = true;

	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "extinction" ), &CPrimitiveTemplate::ParseVolExtinction },

		{ CSTRING_VIEW( "albedo" ), &CPrimitiveTemplate::ParseVolAlbedo },
		{ CSTRING_VIEW( "color" ), &CPrimitiveTemplate::ParseVolAlbedo },

		{ CSTRING_VIEW( "radiusScale" ), &CPrimitiveTemplate::ParseVolRadiusScale },

		{ CSTRING_VIEW( "aspect" ), &CPrimitiveTemplate::ParseVolAspect },

		{ CSTRING_VIEW( "softness" ), &CPrimitiveTemplate::ParseVolSoftness },

		{ CSTRING_VIEW( "emissive" ), &CPrimitiveTemplate::ParseVolEmissive },
		{ CSTRING_VIEW( "emissiveDensity" ), &CPrimitiveTemplate::ParseVolEmissiveDensity },
		{ CSTRING_VIEW( "emissiveTint" ), &CPrimitiveTemplate::ParseVolEmissiveTint },
		{ CSTRING_VIEW( "anisotropy" ), &CPrimitiveTemplate::ParseVolAnisotropy },
	};
	return ParseGroup( grp, parseMethods, "volumetricMedia" );
}

//------------------------------------------------------
// ParseSpot
//	Turns a Light into a spot light (rend2 spot lights, docs/rend2-spot-lights.md).
//	Its presence turns it on; without it the Light is the point light it always was:
//
//	spot
//	{
//		innerAngle	20			// degrees from the axis, full intensity inside (range)
//		outerAngle	30			// degrees from the axis, zero outside (range, max 89)
//		direction	1 0 0		// optional, the cone axis in the effect's axes (forward
//								//	= the effect's direction / the bolt's / muzzle's forward)
//		shadows		1			// optional, 0 = never casts a shadow
//		cookie		gfx/test/volumetric_cookie	// optional light cookie / gobo image
//		cookieRoll	0			// optional, degrees around the axis
//	}
//
//	A cookie is a spot light property (its projection is the cone): it is only
//	accepted in this group, point lights have none.
//
//	Renderers (engines) without spot lights draw the point light.
//------------------------------------------------------
bool CPrimitiveTemplate::ParseSpotInner( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mSpotInner.SetRange( Com_Clamp( 0.0f, 89.0f, min ), Com_Clamp( 0.0f, 89.0f, max ) );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseSpotOuter( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mSpotOuter.SetRange( Com_Clamp( 0.5f, 89.0f, min ), Com_Clamp( 0.5f, 89.0f, max ) );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseSpotDir( const gsl::cstring_span& val )
{
	vec3_t min, max;
	if ( ParseVector( val, min, max ) == true && VectorNormalize( min ) > 0.0f )
	{
		VectorCopy( min, mSpotDir );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseSpotShadows( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mSpotShadows = ( min != 0.0f );
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseSpotCookie( const gsl::cstring_span& val )
{
	const std::string name( val.begin(), val.end() );
	// registered now (effect load): no hitch when the light first shows
	mSpotCookie = theFxHelper.RegisterLightCookie( name.c_str() );
	return true;
}

bool CPrimitiveTemplate::ParseSpotCookieRoll( const gsl::cstring_span& val )
{
	float min, max;
	if ( ParseFloat( val, min, max ) == true )
	{
		mSpotCookieRoll = min;
		return true;
	}
	return false;
}

bool CPrimitiveTemplate::ParseSpot( const CGPGroup& grp )
{
	if ( mType != Light )
	{
		theFxHelper.Print( "spot is only supported by Light, ignored\n" );
		return false;
	}

	mSpot = true;

	static StringViewIMap< ParseMethod > parseMethods{
		{ CSTRING_VIEW( "innerAngle" ), &CPrimitiveTemplate::ParseSpotInner },
		{ CSTRING_VIEW( "inner" ), &CPrimitiveTemplate::ParseSpotInner },

		{ CSTRING_VIEW( "outerAngle" ), &CPrimitiveTemplate::ParseSpotOuter },
		{ CSTRING_VIEW( "outer" ), &CPrimitiveTemplate::ParseSpotOuter },

		{ CSTRING_VIEW( "direction" ), &CPrimitiveTemplate::ParseSpotDir },
		{ CSTRING_VIEW( "dir" ), &CPrimitiveTemplate::ParseSpotDir },

		{ CSTRING_VIEW( "shadows" ), &CPrimitiveTemplate::ParseSpotShadows },
		{ CSTRING_VIEW( "shadow" ), &CPrimitiveTemplate::ParseSpotShadows },

		{ CSTRING_VIEW( "cookie" ), &CPrimitiveTemplate::ParseSpotCookie },
		{ CSTRING_VIEW( "cookieRoll" ), &CPrimitiveTemplate::ParseSpotCookieRoll },
	};
	return ParseGroup( grp, parseMethods, "spot" );
}

// Parse a primitive, apply defaults first, grab any base level
//	key pairs, then process any sub groups we may contain.
//------------------------------------------------------
bool CPrimitiveTemplate::ParsePrimitive( const CGPGroup& grp )
{
	// Property
	for( auto& prop : grp.GetProperties() )
	{
		// Single Value Parsing
		{
			static StringViewIMap< ParseMethod > parseMethods{
				{ CSTRING_VIEW( "count" ), &CPrimitiveTemplate::ParseCount },
				{ CSTRING_VIEW( "life" ), &CPrimitiveTemplate::ParseLife },
				{ CSTRING_VIEW( "delay" ), &CPrimitiveTemplate::ParseDelay },
				{ CSTRING_VIEW( "bounce" ), &CPrimitiveTemplate::ParseElasticity },
				{ CSTRING_VIEW( "intensity" ), &CPrimitiveTemplate::ParseElasticity },
				{ CSTRING_VIEW( "min" ), &CPrimitiveTemplate::ParseMin },
				{ CSTRING_VIEW( "max" ), &CPrimitiveTemplate::ParseMax },
				{ CSTRING_VIEW( "angle" ), &CPrimitiveTemplate::ParseAngle },
				{ CSTRING_VIEW( "angles" ), &CPrimitiveTemplate::ParseAngle },
				{ CSTRING_VIEW( "angleDelta" ), &CPrimitiveTemplate::ParseAngleDelta },
				{ CSTRING_VIEW( "velocity" ), &CPrimitiveTemplate::ParseVelocity },
				{ CSTRING_VIEW( "vel" ), &CPrimitiveTemplate::ParseVelocity },
				{ CSTRING_VIEW( "acceleration" ), &CPrimitiveTemplate::ParseAcceleration },
				{ CSTRING_VIEW( "accel" ), &CPrimitiveTemplate::ParseAcceleration },
				{ CSTRING_VIEW( "gravity" ), &CPrimitiveTemplate::ParseGravity },
				{ CSTRING_VIEW( "density" ), &CPrimitiveTemplate::ParseDensity },
				{ CSTRING_VIEW( "variance" ), &CPrimitiveTemplate::ParseVariance },
				{ CSTRING_VIEW( "origin" ), &CPrimitiveTemplate::ParseOrigin1 },
				{ CSTRING_VIEW( "origin2" ), &CPrimitiveTemplate::ParseOrigin2 },
				{ CSTRING_VIEW( "radius" ), &CPrimitiveTemplate::ParseRadius },
				{ CSTRING_VIEW( "height" ), &CPrimitiveTemplate::ParseHeight },
				{ CSTRING_VIEW( "wind" ), &CPrimitiveTemplate::ParseWindModifier },
				{ CSTRING_VIEW( "rotation" ), &CPrimitiveTemplate::ParseRotation },
				{ CSTRING_VIEW( "rotationDelta" ), &CPrimitiveTemplate::ParseRotationDelta },
				{ CSTRING_VIEW( "flags" ), &CPrimitiveTemplate::ParseFlags },
				{ CSTRING_VIEW( "flag" ), &CPrimitiveTemplate::ParseFlags },
				{ CSTRING_VIEW( "spawnFlags" ), &CPrimitiveTemplate::ParseSpawnFlags },
				{ CSTRING_VIEW( "spawnFlag" ), &CPrimitiveTemplate::ParseSpawnFlags },
			};
			auto pos = parseMethods.find( prop.GetName() );
			if( pos != parseMethods.end() )
			{
				ParseMethod method = pos->second;
				( this->*method )( prop.GetTopValue() );
				continue;
			}
		}
		// Property Parsing
		{
			using PropertyParseMethod = bool( CPrimitiveTemplate::* )( const CGPProperty& );
			static StringViewIMap< PropertyParseMethod > parseMethods{
				{ CSTRING_VIEW( "shaders" ), &CPrimitiveTemplate::ParseShaders },
				{ CSTRING_VIEW( "shader" ), &CPrimitiveTemplate::ParseShaders },
				{ CSTRING_VIEW( "models" ), &CPrimitiveTemplate::ParseModels },
				{ CSTRING_VIEW( "model" ), &CPrimitiveTemplate::ParseModels },
				{ CSTRING_VIEW( "sounds" ), &CPrimitiveTemplate::ParseSounds },
				{ CSTRING_VIEW( "sound" ), &CPrimitiveTemplate::ParseSounds },
				{ CSTRING_VIEW( "impactfx" ), &CPrimitiveTemplate::ParseImpactFxStrings },
				{ CSTRING_VIEW( "deathfx" ), &CPrimitiveTemplate::ParseDeathFxStrings },
				{ CSTRING_VIEW( "emitfx" ), &CPrimitiveTemplate::ParseEmitterFxStrings },
				{ CSTRING_VIEW( "playfx" ), &CPrimitiveTemplate::ParsePlayFxStrings },
			};
			auto pos = parseMethods.find( prop.GetName() );
			if( pos != parseMethods.end() )
			{
				PropertyParseMethod method = pos->second;
				( this->*method )( prop );
				continue;
			}
		}
		// Special Cases
		if( Q::stricmp( prop.GetName(), CSTRING_VIEW( "cullrange" ) ) == Q::Ordering::EQ )
		{
			mCullRange = Q::svtoi( prop.GetTopValue() );
			mCullRange *= mCullRange; // Square
		}
		else if( Q::stricmp( prop.GetName(), CSTRING_VIEW( "name" ) ) == Q::Ordering::EQ )
		{
			if( !prop.GetTopValue().empty() )
			{
				// just stash the descriptive name of the primitive
				std::size_t len = std::min< std::size_t >( prop.GetTopValue().size(), FX_MAX_PRIM_NAME - 1 );
				auto begin = prop.GetTopValue().begin();
				std::copy( begin, begin + len, &mName[ 0 ] );
				mName[ len ] = '\0';
			}
		}
		// Error
		else
		{
			theFxHelper.Print( "Unknown key parsing an effect primitive!\n" );
		}
	}

	for( auto& subGrp : grp.GetSubGroups() )
	{
		using GroupParseMethod = bool ( CPrimitiveTemplate::* )( const CGPGroup& );
		static StringViewIMap< GroupParseMethod > parseMethods{
			{ CSTRING_VIEW( "rgb" ), &CPrimitiveTemplate::ParseRGB },

			{ CSTRING_VIEW( "alpha" ), &CPrimitiveTemplate::ParseAlpha },

			{ CSTRING_VIEW( "size" ), &CPrimitiveTemplate::ParseSize },
			{ CSTRING_VIEW( "width" ), &CPrimitiveTemplate::ParseSize },

			{ CSTRING_VIEW( "size2" ), &CPrimitiveTemplate::ParseSize2 },
			{ CSTRING_VIEW( "width2" ), &CPrimitiveTemplate::ParseSize2 },

			{ CSTRING_VIEW( "length" ), &CPrimitiveTemplate::ParseLength },
			{ CSTRING_VIEW( "height" ), &CPrimitiveTemplate::ParseLength },

			{ CSTRING_VIEW( "volumetricMedia" ), &CPrimitiveTemplate::ParseVolumetricMedia },
			{ CSTRING_VIEW( "spot" ), &CPrimitiveTemplate::ParseSpot },
		};
		auto pos = parseMethods.find( subGrp.GetName() );
		if( pos == parseMethods.end() )
		{
			theFxHelper.Print( "Unknown group key parsing a particle!\n" );
		}
		else
		{
			GroupParseMethod method = pos->second;
			( this->*method )( subGrp );
		}
	}
	return true;
}
