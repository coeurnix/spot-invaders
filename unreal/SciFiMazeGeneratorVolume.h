
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SciFiMazeGeneratorVolume.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;

UCLASS(Blueprintable)
class SPOTINVADERSGFX_API ASpotSciFiMazeGeneratorVolume : public AActor
{
	GENERATED_BODY()

public:
	ASpotSciFiMazeGeneratorVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sci-Fi Maze")
	TObjectPtr<UBoxComponent> BoundsComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation")
	int32 Seed = 12345;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "200.0", UIMin = "200.0"))
	float CellSize = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "100.0", UIMin = "100.0"))
	float RoadWidth = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float BuildingDensity = 0.88f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Branchiness = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float LoopChance = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HeightVariation = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float AlienPatternIntensity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxBuildings = 420;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Generation", meta = (ClampMin = "8", UIMin = "8"))
	int32 MaxCorridors = 280;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Meshes")
	TArray<TObjectPtr<UStaticMesh>> RoadMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Meshes")
	TArray<TObjectPtr<UStaticMesh>> BuildingMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Meshes")
	TArray<TObjectPtr<UStaticMesh>> ObstacleMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Meshes")
	TArray<TObjectPtr<UStaticMesh>> LandmarkMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Materials")
	TArray<TObjectPtr<UMaterialInterface>> RoadMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Materials")
	TArray<TObjectPtr<UMaterialInterface>> BuildingMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Materials")
	TArray<TObjectPtr<UMaterialInterface>> ObstacleMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Materials")
	TArray<TObjectPtr<UMaterialInterface>> LandmarkMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Debug")
	bool bDrawDebugRoadGraph = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Debug")
	bool bDrawDebugPlazas = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Debug")
	bool bDrawDebugPlacementRejections = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sci-Fi Maze|Debug", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugDrawDuration = 12.0f;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Sci-Fi Maze")
	void GenerateCityMaze();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Sci-Fi Maze")
	void ClearGenerated();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Sci-Fi Maze")
	void RandomizeSeedAndGenerate();

protected:
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Sci-Fi Maze|Generated")
	TArray<TObjectPtr<AActor>> GeneratedActors;

	UPROPERTY(VisibleInstanceOnly, Category = "Sci-Fi Maze|Generated")
	FGuid GeneratorGuid;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultRoadMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultSolidMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultCylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultConeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultSphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultMaterial;

	void EnsureGeneratorGuid();
	void GenerateCityMazeInternal();
	void ClearGeneratedInternal();
	bool ValidateGenerationSettings() const;
	void MarkOwningLevelDirty() const;

	FString GetGeneratedFolderPath() const;
	FName GetGeneratedActorTag() const;

	UStaticMesh* ChooseMesh(FRandomStream& Stream, const TArray<TObjectPtr<UStaticMesh>>& Meshes, UStaticMesh* FallbackMesh) const;
	UMaterialInterface* ChooseMaterial(FRandomStream& Stream, const TArray<TObjectPtr<UMaterialInterface>>& Materials) const;

	AActor* SpawnGeneratedMeshActor(
		const FString& LabelPrefix,
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FVector& LocalCenter,
		float LocalYawDegrees,
		const FVector& TargetWorldSize,
		bool bBlocksNavigation);
};
