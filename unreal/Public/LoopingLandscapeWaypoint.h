
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LoopingLandscapeWaypoint.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class SPOTINVADERSGFX_API ASpotLoopingLandscapeWaypoint : public AActor
{
	GENERATED_BODY()

public:
	ASpotLoopingLandscapeWaypoint();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MarkerMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint", meta = (ClampMin = "10.0", UIMin = "10.0"))
	float MarkerRadius = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	// Keep this enabled for cinematic waypoint markers; the marker is visible in editor but hidden during gameplay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	bool bHideMarkerInGame = true;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultSphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultMarkerMaterial;

	void ApplyMarkerSettings();
};
