// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"

UShapeComponent::UShapeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static const FName CollisionProfileName(TEXT("OverlapAllDynamic"));
	BodyInstance.SetCollisionProfileName(CollisionProfileName);
	// when we deprecated this variable, we switched on for the shapecomponent collision profile
	// the problem with adding collision profile later, the instanced data will be wiped
	// since shape component is so popular for BP and so on, I'm adding manual support for compatibility
	// this only works since this variable is getting deprecated. 
	BodyInstance.ResponseToChannels_DEPRECATED.SetAllChannels(ECR_Block);

	bHiddenInGame = true;
	bCastDynamicShadow = false;
	ShapeColor = FColor(223, 149, 157, 255);
	bShouldCollideWhenPlacing = false;
	bCanEverAffectNavigation = true;
}

FPrimitiveSceneProxy* UShapeComponent::CreateSceneProxy()
{
	check( false && "Subclass needs to Implement this" );
	return NULL;
}

FBoxSphereBounds UShapeComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	check( false && "Subclass needs to Implement this" );
	return FBoxSphereBounds();
}

void UShapeComponent::UpdateBodySetup()
{
	check( false && "Subclass needs to Implement this" );
}

UBodySetup* UShapeComponent::GetBodySetup()
{
	UpdateBodySetup();
	return ShapeBodySetup;
}

void UShapeComponent::GetUsedMaterials( TArray<UMaterialInterface*>& OutMaterials ) const
{
	OutMaterials.Add( ShapeMaterial );
}

#if WITH_EDITOR
void UShapeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!IsTemplate())
	{
		UpdateBodySetup(); // do this before reregistering components so that new values are used for collision
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR