// Copyright Epic Games, Inc. All Rights Reserved.

#include "TerrainFollowingCineCamera.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LoopingLandscapePathDriver.h"

ASpotTerrainFollowingCineCamera::ASpotTerrainFollowingCineCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	// Cinematic cameras are commonly referenced by Level Sequences and should stay always loaded in World Partition maps.
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
}

void ASpotTerrainFollowingCineCamera::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
	TryAutoAssignPathDriver();
}

void ASpotTerrainFollowingCineCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCameraFromDriver(DeltaSeconds);
}

bool ASpotTerrainFollowingCineCamera::UpdateCameraFromDriver(float DeltaSeconds)
{
	if (!PathDriverActor)
	{
		if (!TryAutoAssignPathDriver())
		{
			return false;
		}
	}

	const FVector DriverLocation = PathDriverActor->GetActorLocation();

	float GroundZ = 0.0f;
	if (!FindGroundZAtXY(DriverLocation, GroundZ))
	{
		return false;
	}

	const float DesiredZ = GroundZ + HeightAboveGround;
	const float EffectiveDeltaSeconds = DeltaSeconds > KINDA_SMALL_NUMBER ? DeltaSeconds : 1.0f / 60.0f;

	if (!bHasValidGroundResult)
	{
		SmoothedZ = DesiredZ;
		bHasValidGroundResult = true;
	}
	else
	{
		const float PreviousZ = SmoothedZ;
		SmoothedZ = FMath::FInterpTo(SmoothedZ, DesiredZ, EffectiveDeltaSeconds, ElevationInterpSpeed);

		if (MaxVerticalSpeed > 0.0f)
		{
			const float MaxStep = MaxVerticalSpeed * EffectiveDeltaSeconds;
			SmoothedZ = PreviousZ + FMath::Clamp(SmoothedZ - PreviousZ, -MaxStep, MaxStep);
		}
	}

	SetActorLocation(FVector(DriverLocation.X, DriverLocation.Y, SmoothedZ), false, nullptr, ETeleportType::TeleportPhysics);

	if (bForceTopDownRotation)
	{
		SetActorRotation(FRotator(-90.0f, TopDownYaw, 0.0f), ETeleportType::TeleportPhysics);
	}

	return true;
}

#if WITH_EDITOR
bool ASpotTerrainFollowingCineCamera::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

bool ASpotTerrainFollowingCineCamera::TryAutoAssignPathDriver()
{
	if (PathDriverActor || !bAutoFindSinglePathDriver)
	{
		return PathDriverActor != nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	ASpotLoopingLandscapePathDriver* OnlyDriver = nullptr;
	int32 DriverCount = 0;
	for (TActorIterator<ASpotLoopingLandscapePathDriver> It(World); It; ++It)
	{
		ASpotLoopingLandscapePathDriver* Driver = *It;
		if (!IsValid(Driver))
		{
			continue;
		}

		OnlyDriver = Driver;
		++DriverCount;
		if (DriverCount > 1)
		{
			return false;
		}
	}

	if (DriverCount == 1)
	{
		PathDriverActor = OnlyDriver;
		return true;
	}

	return false;
}

bool ASpotTerrainFollowingCineCamera::FindGroundZAtXY(const FVector& XYLocation, float& OutGroundZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	TArray<FVector2D, TInlineAllocator<5>> SampleOffsets;
	SampleOffsets.Add(FVector2D::ZeroVector);

	if (bUseMultiSampleGroundHeight)
	{
		const float Radius = FMath::Max(0.0f, SampleRadius);
		SampleOffsets.Add(FVector2D(Radius, 0.0f));
		SampleOffsets.Add(FVector2D(-Radius, 0.0f));
		SampleOffsets.Add(FVector2D(0.0f, Radius));
		SampleOffsets.Add(FVector2D(0.0f, -Radius));
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TerrainFollowingCineCameraTrace), false);
	QueryParams.AddIgnoredActor(this);

	// The Path Driver is a Sequencer control object, not terrain. Ignore it if it has collision enabled.
	if (PathDriverActor)
	{
		QueryParams.AddIgnoredActor(PathDriverActor);
	}

	int32 ValidHitCount = 0;
	float AccumulatedZ = 0.0f;
	float HighestZ = -TNumericLimits<float>::Max();

	for (const FVector2D& Offset : SampleOffsets)
	{
		const FVector TraceStart(XYLocation.X + Offset.X, XYLocation.Y + Offset.Y, XYLocation.Z + TraceAbove);
		const FVector TraceEnd(XYLocation.X + Offset.X, XYLocation.Y + Offset.Y, XYLocation.Z - TraceBelow);

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams) && Hit.bBlockingHit)
		{
			++ValidHitCount;
			AccumulatedZ += Hit.ImpactPoint.Z;
			HighestZ = FMath::Max(HighestZ, Hit.ImpactPoint.Z);
		}
	}

	if (ValidHitCount == 0)
	{
		return false;
	}

	OutGroundZ = GroundHeightMode == ETerrainFollowHeightMode::Highest
		? HighestZ
		: AccumulatedZ / static_cast<float>(ValidHitCount);

	return true;
}
