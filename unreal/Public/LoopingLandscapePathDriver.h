// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LoopingLandscapePathDriver.generated.h"

class USceneComponent;
class USplineComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class ASpotLoopingLandscapeWaypoint;

UCLASS(Blueprintable)
class SPOTINVADERSGFX_API ASpotLoopingLandscapePathDriver : public AActor
{
	GENERATED_BODY()

public:
	ASpotLoopingLandscapePathDriver();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USplineComponent> PathSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DriverMarkerMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker", meta = (ClampMin = "10.0", UIMin = "10.0"))
	float DriverMarkerRadius = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker")
	TObjectPtr<UMaterialInterface> DriverMarkerMaterial;

	// Visible in editor for placement/debugging, hidden for gameplay and cinematic renders.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker")
	bool bHideDriverMarkerInGame = true;

	// Preferred ordered waypoint markers. Place ASpotLoopingLandscapeWaypoint actors in the landscape and assign them here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TArray<TObjectPtr<ASpotLoopingLandscapeWaypoint>> Waypoints;

	// Generic actor fallback. Used only when no valid Waypoints are assigned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TArray<TObjectPtr<AActor>> WaypointActors;

	// Used only when there are no valid Waypoints or WaypointActors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TArray<FVector> WaypointPositions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	bool bWaypointPositionsAreWorldSpace = true;

	// Rebuild in construction so the visible closed spline stays current while editing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	bool bRebuildSplineOnConstruction = true;

	// Uses explicit closed-loop tangents so the driver glides through waypoints with a cinematic orbital feel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Smoothing")
	bool bUseCinematicSmoothTangents = true;

	// Higher values create wider, more elegant arcs through waypoints. Lower values stay tighter to the waypoint layout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Smoothing", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0"))
	float PathSmoothingStrength = 1.0f;

	// Prevents very long tangent handles from overshooting when neighboring waypoints are unevenly spaced.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Smoothing", meta = (ClampMin = "0.1", ClampMax = "1.5", UIMin = "0.1", UIMax = "1.5"))
	float MaxTangentLengthRatio = 1.0f;

	// More reparameterization samples gives smoother constant-speed evaluation on large cinematic curves.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Smoothing", meta = (ClampMin = "4", ClampMax = "100", UIMin = "4", UIMax = "100"))
	int32 PathReparamStepsPerSegment = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float LoopDurationSeconds = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	bool bAutoPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	float StartTimeOffsetSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape Follow")
	bool bFollowLandscapeHeight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape Follow")
	float HeightAboveGround = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TraceAbove = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TraceBelow = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape Follow")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// If 0, snap exactly to the traced height. If greater than 0, smooth Z with FInterpTo.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ElevationInterpSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bForceNoCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	bool bOrientAlongPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float FixedPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float FixedRoll = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float YawOffsetDegrees = 0.0f;

	// Enable this for Sequencer-driven shots, then key PlaybackAlpha from 0 to 1 over the shot duration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequencer")
	bool bUseSequencerPlaybackAlpha = false;

	// Animate this actor's PlaybackAlpha or waypoints, not the terrain-following camera transform directly.
	// InterpNotify makes Sequencer call back after evaluating the property, including when it uses a direct property write path.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetPlaybackAlpha", Interp, Category = "Sequencer", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", InterpNotify = "OnPlaybackAlphaSequencerChanged"))
	float PlaybackAlpha = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Motion")
	float GetCurrentSpeedCmPerSecond() const;

	UFUNCTION(BlueprintCallable, Category = "Path")
	void RebuildSpline();

	UFUNCTION(BlueprintCallable, Category = "Motion")
	void ResetPlayback();

	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Motion")
	void SetPlaybackAlpha(float Alpha);

	UFUNCTION(BlueprintCallable, Category = "Motion")
	void SetPlaybackTimeSeconds(float TimeSeconds);

protected:
	bool FindGroundZAtXY(const FVector& XYLocation, float& OutGroundZ) const;

private:
	UPROPERTY(Transient)
	FVector InitialStartLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	float ElapsedTimeSeconds = 0.0f;

	UPROPERTY(Transient)
	float CachedSplineLength = 0.0f;

	UPROPERTY(Transient)
	float SmoothedZ = 0.0f;

	UPROPERTY(Transient)
	bool bHasInitializedZ = false;

	UPROPERTY(Transient)
	bool bHasCachedStartLocation = false;

	UPROPERTY(Transient)
	float LastAppliedSequencerPlaybackAlpha = TNumericLimits<float>::Lowest();

	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultSphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultMarkerMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DriverMarkerMID;

	void CacheStartLocation(const FVector& StartLocation);
	void ApplyClosedLoopPointTypes();
	void ApplyCollisionSettings();
	void ApplyDriverMarkerSettings();
	void NotifyLinkedTerrainFollowingCameras(float DeltaSeconds);
	float NormalizePlaybackAlpha(float Alpha) const;
	void ApplyPlaybackAlpha(float DeltaSeconds);
	void PlaceAtNormalizedAlpha(float NormalizedAlpha, float DeltaSeconds);

	UFUNCTION()
	void OnPlaybackAlphaSequencerChanged();
};
