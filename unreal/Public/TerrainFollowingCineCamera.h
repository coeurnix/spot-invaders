
#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "TerrainFollowingCineCamera.generated.h"

class AActor;
class ASpotLoopingLandscapePathDriver;

UENUM(BlueprintType)
enum class ETerrainFollowHeightMode : uint8
{
	Average UMETA(DisplayName = "Average"),
	Highest UMETA(DisplayName = "Highest")
};

UCLASS(Blueprintable)
class SPOTINVADERSGFX_API ASpotTerrainFollowingCineCamera : public ACineCameraActor
{
	GENERATED_BODY()

public:
	ASpotTerrainFollowingCineCamera(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Terrain Follow")
	bool UpdateCameraFromDriver(float DeltaSeconds);

	// Animate this separate actor in Sequencer. The camera generally should not have transform keys of its own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	TObjectPtr<AActor> PathDriverActor;

	// If no PathDriverActor is assigned, automatically bind to the only ASpotLoopingLandscapePathDriver in the level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	bool bAutoFindSinglePathDriver = true;

	// Desired camera height above the traced landscape or ground mesh, in centimeters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HeightAboveGround = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TraceAbove = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TraceBelow = 100000.0f;

	// Higher values follow terrain more tightly; lower values produce smoother aerial cinematic motion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ElevationInterpSpeed = 1.5f;

	// Optional vertical speed clamp in cm/sec. Set to 0 to disable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxVerticalSpeed = 0.0f;

	// Landscape or ground meshes must block this channel for the camera to find terrain height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	bool bForceTopDownRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	float TopDownYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	bool bUseMultiSampleGroundHeight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SampleRadius = 1000.0f;

	// Average is smoother for aerial shots; Highest is safer over sharp hills, ridges, or cliffs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Follow")
	ETerrainFollowHeightMode GroundHeightMode = ETerrainFollowHeightMode::Average;

protected:
	bool FindGroundZAtXY(const FVector& XYLocation, float& OutGroundZ) const;

private:
	UPROPERTY(Transient)
	bool bHasValidGroundResult = false;

	UPROPERTY(Transient)
	float SmoothedZ = 0.0f;

	bool TryAutoAssignPathDriver();
};
