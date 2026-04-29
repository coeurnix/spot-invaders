
#include "LoopingLandscapeWaypoint.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASpotLoopingLandscapeWaypoint::ASpotLoopingLandscapeWaypoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkerMesh"));
	MarkerMesh->SetupAttachment(Root);
	MarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	MarkerMesh->SetCanEverAffectNavigation(false);
	MarkerMesh->SetHiddenInGame(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		DefaultSphereMesh = SphereFinder.Object;
		MarkerMesh->SetStaticMesh(DefaultSphereMesh);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		DefaultMarkerMaterial = MaterialFinder.Object;
		MarkerMesh->SetMaterial(0, DefaultMarkerMaterial);
	}
}

void ASpotLoopingLandscapeWaypoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif

	ApplyMarkerSettings();
}

void ASpotLoopingLandscapeWaypoint::ApplyMarkerSettings()
{
	SetActorEnableCollision(false);

	if (!MarkerMesh)
	{
		return;
	}

	MarkerMesh->SetVisibility(true);
	MarkerMesh->SetHiddenInGame(bHideMarkerInGame);
	MarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	MarkerMesh->SetCanEverAffectNavigation(false);

	if (!MarkerMesh->GetStaticMesh() && DefaultSphereMesh)
	{
		MarkerMesh->SetStaticMesh(DefaultSphereMesh);
	}

	if (MarkerMaterial)
	{
		MarkerMesh->SetMaterial(0, MarkerMaterial);
	}
	else if (DefaultMarkerMaterial)
	{
		MarkerMesh->SetMaterial(0, DefaultMarkerMaterial);
	}

	if (UStaticMesh* Mesh = MarkerMesh->GetStaticMesh())
	{
		const FVector MeshSize = Mesh->GetBounds().BoxExtent * 2.0f;
		const float MeshDiameter = FMath::Max3(MeshSize.X, MeshSize.Y, MeshSize.Z);
		const float Scale = MeshDiameter > KINDA_SMALL_NUMBER ? (MarkerRadius * 2.0f) / MeshDiameter : 1.0f;
		MarkerMesh->SetRelativeScale3D(FVector(Scale));
	}
}
