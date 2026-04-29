
#include "LoopingLandscapePathDriver.h"

#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LoopingLandscapeWaypoint.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TerrainFollowingCineCamera.h"
#include "UObject/ConstructorHelpers.h"

ASpotLoopingLandscapePathDriver::ASpotLoopingLandscapePathDriver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SetActorEnableCollision(false);
	// Sequencer and camera references should remain valid regardless of World Partition streaming.
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DriverMarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DriverMarkerMesh"));
	DriverMarkerMesh->SetupAttachment(Root);
	DriverMarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DriverMarkerMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	DriverMarkerMesh->SetCanEverAffectNavigation(false);
	DriverMarkerMesh->SetHiddenInGame(true);

	PathSpline = CreateDefaultSubobject<USplineComponent>(TEXT("PathSpline"));
	PathSpline->SetupAttachment(Root);

	// The actor moves along the path, so keep the debug spline absolute in world space.
	PathSpline->SetAbsolute(true, true, true);
	PathSpline->SetClosedLoop(true);
	PathSpline->SetVisibility(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		DefaultSphereMesh = SphereFinder.Object;
		DriverMarkerMesh->SetStaticMesh(DefaultSphereMesh);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		DefaultMarkerMaterial = MaterialFinder.Object;
		DriverMarkerMesh->SetMaterial(0, DefaultMarkerMaterial);
	}

	ApplyCollisionSettings();
	ApplyDriverMarkerSettings();
}

void ASpotLoopingLandscapePathDriver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (CachedSplineLength <= KINDA_SMALL_NUMBER || LoopDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (bUseSequencerPlaybackAlpha)
	{
		ApplyPlaybackAlpha(DeltaSeconds);
		return;
	}

	if (!bAutoPlay)
	{
		return;
	}

	ElapsedTimeSeconds += DeltaSeconds;
	const float PlaybackTime = ElapsedTimeSeconds + StartTimeOffsetSeconds;
	float NormalizedAlpha = 0.0f;

	if (bLoop)
	{
		NormalizedAlpha = FMath::Fmod(PlaybackTime, LoopDurationSeconds) / LoopDurationSeconds;
		if (NormalizedAlpha < 0.0f)
		{
			NormalizedAlpha += 1.0f;
		}
	}
	else
	{
		NormalizedAlpha = FMath::Clamp(PlaybackTime / LoopDurationSeconds, 0.0f, 1.0f);
	}

	PlaceAtNormalizedAlpha(NormalizedAlpha, DeltaSeconds);
}

void ASpotLoopingLandscapePathDriver::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// This actor is intended to be used as a Sequencer path driver.
	// Assign it to ASpotTerrainFollowingCineCamera::PathDriverActor.
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
	CacheStartLocation(Transform.GetLocation());

	if (bRebuildSplineOnConstruction)
	{
		RebuildSpline();
	}

	ApplyCollisionSettings();
	ApplyDriverMarkerSettings();
}

#if WITH_EDITOR
void ASpotLoopingLandscapePathDriver::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	LoopDurationSeconds = FMath::Max(LoopDurationSeconds, 0.001f);
	TraceAbove = FMath::Max(TraceAbove, 0.0f);
	TraceBelow = FMath::Max(TraceBelow, 0.0f);
	ElevationInterpSpeed = FMath::Max(ElevationInterpSpeed, 0.0f);
	PathSmoothingStrength = FMath::Clamp(PathSmoothingStrength, 0.0f, 2.0f);
	MaxTangentLengthRatio = FMath::Clamp(MaxTangentLengthRatio, 0.1f, 1.5f);
	PathReparamStepsPerSegment = FMath::Clamp(PathReparamStepsPerSegment, 4, 100);
	PlaybackAlpha = FMath::Clamp(PlaybackAlpha, 0.0f, 1.0f);

	const FName ChangedPropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASpotLoopingLandscapePathDriver, PlaybackAlpha) ||
		ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASpotLoopingLandscapePathDriver, bUseSequencerPlaybackAlpha))
	{
		const float NormalizedAlpha = NormalizePlaybackAlpha(PlaybackAlpha);
		LastAppliedSequencerPlaybackAlpha = NormalizedAlpha;
		PlaceAtNormalizedAlpha(NormalizedAlpha, 1.0f / 60.0f);
	}
}

bool ASpotLoopingLandscapePathDriver::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

float ASpotLoopingLandscapePathDriver::GetCurrentSpeedCmPerSecond() const
{
	return LoopDurationSeconds > KINDA_SMALL_NUMBER ? CachedSplineLength / LoopDurationSeconds : 0.0f;
}

void ASpotLoopingLandscapePathDriver::RebuildSpline()
{
	if (!PathSpline)
	{
		return;
	}

	// Do not duplicate the starting point at the end; SetClosedLoop(true) creates the final segment.
	PathSpline->ClearSplinePoints(false);
	PathSpline->SetAbsolute(true, true, true);
	PathSpline->ReparamStepsPerSegment = FMath::Clamp(PathReparamStepsPerSegment, 4, 100);

	if (!bHasCachedStartLocation)
	{
		CacheStartLocation(GetActorLocation());
	}

	PathSpline->AddSplinePoint(InitialStartLocation, ESplineCoordinateSpace::World, false);

	bool bHasValidWaypoints = false;
	for (ASpotLoopingLandscapeWaypoint* Waypoint : Waypoints)
	{
		if (IsValid(Waypoint))
		{
#if WITH_EDITOR
			Waypoint->Modify();
			Waypoint->SetIsSpatiallyLoaded(false);
#endif
			bHasValidWaypoints = true;
			PathSpline->AddSplinePoint(Waypoint->GetActorLocation(), ESplineCoordinateSpace::World, false);
		}
	}

	bool bHasValidActorWaypoints = false;
	if (!bHasValidWaypoints)
	{
		for (AActor* WaypointActor : WaypointActors)
		{
			if (IsValid(WaypointActor))
			{
#if WITH_EDITOR
				// Hard references from Sequencer/path drivers to World Partition spatial actors produce MapCheck errors.
				WaypointActor->Modify();
				WaypointActor->SetIsSpatiallyLoaded(false);
#endif
				bHasValidActorWaypoints = true;
				PathSpline->AddSplinePoint(WaypointActor->GetActorLocation(), ESplineCoordinateSpace::World, false);
			}
		}
	}

	if (!bHasValidWaypoints && !bHasValidActorWaypoints)
	{
		for (const FVector& WaypointPosition : WaypointPositions)
		{
			const FVector WorldPoint = bWaypointPositionsAreWorldSpace
				? WaypointPosition
				: InitialStartLocation + WaypointPosition;
			PathSpline->AddSplinePoint(WorldPoint, ESplineCoordinateSpace::World, false);
		}
	}

	PathSpline->SetClosedLoop(true, false);
	ApplyClosedLoopPointTypes();
	PathSpline->UpdateSpline();
	CachedSplineLength = PathSpline->GetSplineLength();
}

void ASpotLoopingLandscapePathDriver::ResetPlayback()
{
	ElapsedTimeSeconds = 0.0f;
	SmoothedZ = 0.0f;
	bHasInitializedZ = false;
	ApplyPlaybackAlpha(0.0f);
}

void ASpotLoopingLandscapePathDriver::SetPlaybackAlpha(float Alpha)
{
	if (CachedSplineLength <= KINDA_SMALL_NUMBER)
	{
		RebuildSpline();
	}

	PlaybackAlpha = bLoop ? Alpha : FMath::Clamp(Alpha, 0.0f, 1.0f);
	const float NormalizedAlpha = NormalizePlaybackAlpha(PlaybackAlpha);
	ElapsedTimeSeconds = NormalizedAlpha * LoopDurationSeconds - StartTimeOffsetSeconds;

	// Sequencer custom accessors call this setter, which makes scrubbing update the actor immediately.
	ApplyPlaybackAlpha(0.0f);
}

void ASpotLoopingLandscapePathDriver::SetPlaybackTimeSeconds(float TimeSeconds)
{
	if (CachedSplineLength <= KINDA_SMALL_NUMBER)
	{
		RebuildSpline();
	}

	ElapsedTimeSeconds = TimeSeconds - StartTimeOffsetSeconds;

	const float Alpha = LoopDurationSeconds > KINDA_SMALL_NUMBER ? TimeSeconds / LoopDurationSeconds : 0.0f;
	PlaybackAlpha = NormalizePlaybackAlpha(Alpha);
	PlaceAtNormalizedAlpha(PlaybackAlpha, 0.0f);
}

bool ASpotLoopingLandscapePathDriver::FindGroundZAtXY(const FVector& XYLocation, float& OutGroundZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector TraceStart(XYLocation.X, XYLocation.Y, XYLocation.Z + TraceAbove);
	const FVector TraceEnd(XYLocation.X, XYLocation.Y, XYLocation.Z - TraceBelow);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LoopingLandscapePathDriverTrace), false);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams) && Hit.bBlockingHit)
	{
		OutGroundZ = Hit.ImpactPoint.Z;
		return true;
	}

	return false;
}

void ASpotLoopingLandscapePathDriver::CacheStartLocation(const FVector& StartLocation)
{
	InitialStartLocation = StartLocation;
	bHasCachedStartLocation = true;
}

void ASpotLoopingLandscapePathDriver::ApplyClosedLoopPointTypes()
{
	if (!PathSpline)
	{
		return;
	}

	const int32 PointCount = PathSpline->GetNumberOfSplinePoints();
	if (!bUseCinematicSmoothTangents || PointCount < 3)
	{
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			PathSpline->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
		}
		return;
	}

	TArray<FVector> Points;
	Points.Reserve(PointCount);
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		Points.Add(PathSpline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World));
	}

	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		const int32 PrevIndex = (PointIndex - 1 + PointCount) % PointCount;
		const int32 NextIndex = (PointIndex + 1) % PointCount;

		const FVector& PrevPoint = Points[PrevIndex];
		const FVector& CurrentPoint = Points[PointIndex];
		const FVector& NextPoint = Points[NextIndex];

		const float PrevDistance = FVector::Dist(PrevPoint, CurrentPoint);
		const float NextDistance = FVector::Dist(CurrentPoint, NextPoint);
		const float TangentLimit = FMath::Min(PrevDistance, NextDistance) * MaxTangentLengthRatio;

		FVector TangentDirection = NextPoint - PrevPoint;
		if (!TangentDirection.Normalize())
		{
			TangentDirection = (NextPoint - CurrentPoint).GetSafeNormal();
		}

		// Matching arrive/leave tangents gives C1-style continuity at every waypoint, including the closed-loop wrap.
		const FVector SmoothTangent = TangentDirection * TangentLimit * PathSmoothingStrength;
		PathSpline->SetSplinePointType(PointIndex, ESplinePointType::CurveCustomTangent, false);
		PathSpline->SetTangentsAtSplinePoint(PointIndex, SmoothTangent, SmoothTangent, ESplineCoordinateSpace::World, false);
	}
}

void ASpotLoopingLandscapePathDriver::ApplyCollisionSettings()
{
	SetActorEnableCollision(!bForceNoCollision);

	if (DriverMarkerMesh)
	{
		DriverMarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DriverMarkerMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		DriverMarkerMesh->SetCanEverAffectNavigation(false);
	}

	if (PathSpline)
	{
		if (bForceNoCollision)
		{
			PathSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PathSpline->SetCollisionResponseToAllChannels(ECR_Ignore);
		}
		else
		{
			PathSpline->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}
}

void ASpotLoopingLandscapePathDriver::ApplyDriverMarkerSettings()
{
	if (!DriverMarkerMesh)
	{
		return;
	}

	DriverMarkerMesh->SetVisibility(true);
	DriverMarkerMesh->SetHiddenInGame(bHideDriverMarkerInGame);
	DriverMarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DriverMarkerMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	DriverMarkerMesh->SetCanEverAffectNavigation(false);

	if (!DriverMarkerMesh->GetStaticMesh() && DefaultSphereMesh)
	{
		DriverMarkerMesh->SetStaticMesh(DefaultSphereMesh);
	}

	UMaterialInterface* MaterialToUse = DriverMarkerMaterial ? DriverMarkerMaterial.Get() : DefaultMarkerMaterial.Get();
	if (MaterialToUse)
	{
		DriverMarkerMID = UMaterialInstanceDynamic::Create(MaterialToUse, this);
		if (DriverMarkerMID)
		{
			// Yellow makes the moving driver distinct from the default waypoint markers and easier to see than cyan.
			const FLinearColor DriverColor(1.0f, 0.82f, 0.0f, 1.0f);
			DriverMarkerMID->SetVectorParameterValue(TEXT("Color"), DriverColor);
			DriverMarkerMID->SetVectorParameterValue(TEXT("BaseColor"), DriverColor);
			DriverMarkerMID->SetVectorParameterValue(TEXT("Base Color"), DriverColor);
			DriverMarkerMesh->SetMaterial(0, DriverMarkerMID);
		}
		else
		{
			DriverMarkerMesh->SetMaterial(0, MaterialToUse);
		}
	}

	if (UStaticMesh* Mesh = DriverMarkerMesh->GetStaticMesh())
	{
		const FVector MeshSize = Mesh->GetBounds().BoxExtent * 2.0f;
		const float MeshDiameter = FMath::Max3(MeshSize.X, MeshSize.Y, MeshSize.Z);
		const float Scale = MeshDiameter > KINDA_SMALL_NUMBER ? (DriverMarkerRadius * 2.0f) / MeshDiameter : 1.0f;
		DriverMarkerMesh->SetRelativeScale3D(FVector(Scale));
	}
}

float ASpotLoopingLandscapePathDriver::NormalizePlaybackAlpha(float Alpha) const
{
	if (bLoop)
	{
		float WrappedAlpha = FMath::Fmod(Alpha, 1.0f);
		if (WrappedAlpha < 0.0f)
		{
			WrappedAlpha += 1.0f;
		}
		return WrappedAlpha;
	}

	return FMath::Clamp(Alpha, 0.0f, 1.0f);
}

void ASpotLoopingLandscapePathDriver::ApplyPlaybackAlpha(float DeltaSeconds)
{
	if (CachedSplineLength <= KINDA_SMALL_NUMBER)
	{
		RebuildSpline();
	}

	const float NormalizedAlpha = NormalizePlaybackAlpha(PlaybackAlpha);
	LastAppliedSequencerPlaybackAlpha = NormalizedAlpha;
	PlaceAtNormalizedAlpha(NormalizedAlpha, DeltaSeconds);
}

void ASpotLoopingLandscapePathDriver::PlaceAtNormalizedAlpha(float NormalizedAlpha, float DeltaSeconds)
{
	if (!PathSpline || CachedSplineLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float DistanceAlongSpline = NormalizedAlpha * CachedSplineLength;
	FVector TargetLocation = PathSpline->GetLocationAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::World);

	if (bFollowLandscapeHeight)
	{
		float GroundZ = 0.0f;
		if (FindGroundZAtXY(TargetLocation, GroundZ))
		{
			const float TargetZ = GroundZ + HeightAboveGround;
			if (!bHasInitializedZ || ElevationInterpSpeed <= 0.0f || DeltaSeconds <= KINDA_SMALL_NUMBER)
			{
				SmoothedZ = TargetZ;
				bHasInitializedZ = true;
			}
			else
			{
				SmoothedZ = FMath::FInterpTo(SmoothedZ, TargetZ, DeltaSeconds, ElevationInterpSpeed);
			}

			TargetLocation.Z = SmoothedZ;
		}
	}
	else
	{
		SmoothedZ = TargetLocation.Z;
		bHasInitializedZ = true;
	}

	SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (bOrientAlongPath)
	{
		FVector Direction = PathSpline->GetDirectionAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::World);
		Direction.Z = 0.0f;
		if (!Direction.IsNearlyZero())
		{
			const float DirectionYaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
			SetActorRotation(FRotator(FixedPitch, DirectionYaw + YawOffsetDegrees, FixedRoll), ETeleportType::TeleportPhysics);
		}
	}

	NotifyLinkedTerrainFollowingCameras(DeltaSeconds);
}

void ASpotLoopingLandscapePathDriver::OnPlaybackAlphaSequencerChanged()
{
	// Called by Sequencer after evaluating PlaybackAlpha. This catches evaluation paths that write the
	// property directly instead of going through SetPlaybackAlpha.
	ApplyPlaybackAlpha(0.0f);
}

void ASpotLoopingLandscapePathDriver::NotifyLinkedTerrainFollowingCameras(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ASpotTerrainFollowingCineCamera> It(World); It; ++It)
	{
		ASpotTerrainFollowingCineCamera* Camera = *It;
		if (!IsValid(Camera))
		{
			continue;
		}

		if (Camera->PathDriverActor == this || (!Camera->PathDriverActor && Camera->bAutoFindSinglePathDriver))
		{
			Camera->UpdateCameraFromDriver(DeltaSeconds);
		}
	}
}
