
#include "SciFiMazeGeneratorVolume.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "SciFiMazeGeneratorVolume"

DEFINE_LOG_CATEGORY_STATIC(LogSciFiMazeGenerator, Log, All);

namespace
{
	constexpr float DefaultRoadThickness = 8.0f;
	constexpr int32 MaxPlacementAttemptsMultiplier = 28;

	struct FGridEdge
	{
		FIntPoint A = FIntPoint::ZeroValue;
		FIntPoint B = FIntPoint::ZeroValue;

		FGridEdge() = default;

		FGridEdge(const FIntPoint& InA, const FIntPoint& InB)
		{
			if (InA.X < InB.X || (InA.X == InB.X && InA.Y <= InB.Y))
			{
				A = InA;
				B = InB;
			}
			else
			{
				A = InB;
				B = InA;
			}
		}

		friend bool operator==(const FGridEdge& Left, const FGridEdge& Right)
		{
			return Left.A == Right.A && Left.B == Right.B;
		}
	};

	FORCEINLINE uint32 GetTypeHash(const FGridEdge& Edge)
	{
		return HashCombine(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
	}

	struct FRoadSegment
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		float Width = 0.0f;
		bool bMajor = false;
		FString Label;
	};

	struct FCircularRoadArea
	{
		FVector2D Center = FVector2D::ZeroVector;
		float Radius = 0.0f;
		float Width = 0.0f;
	};

	struct FPlacedSolid
	{
		FVector2D Center = FVector2D::ZeroVector;
		float Radius = 0.0f;
	};

	float DistancePointToSegment2D(const FVector2D& Point, const FVector2D& SegmentStart, const FVector2D& SegmentEnd)
	{
		const FVector2D Segment = SegmentEnd - SegmentStart;
		const float SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= KINDA_SMALL_NUMBER)
		{
			return FVector2D::Distance(Point, SegmentStart);
		}

		const float T = FMath::Clamp(FVector2D::DotProduct(Point - SegmentStart, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
		return FVector2D::Distance(Point, SegmentStart + Segment * T);
	}

	FVector ToLocal3D(const FVector2D& Point, float Z = 0.0f)
	{
		return FVector(Point.X, Point.Y, Z);
	}

	FString CleanActorFolderName(FString Name)
	{
		Name.ReplaceInline(TEXT("/"), TEXT("_"));
		Name.ReplaceInline(TEXT("\\"), TEXT("_"));
		Name.ReplaceInline(TEXT(":"), TEXT("_"));
		return Name;
	}
}

ASpotSciFiMazeGeneratorVolume::ASpotSciFiMazeGeneratorVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("GenerationBounds"));
	SetRootComponent(BoundsComponent);
	BoundsComponent->SetBoxExtent(FVector(6000.0f, 6000.0f, 400.0f));
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsComponent->bHiddenInGame = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		DefaultRoadMesh = PlaneFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		DefaultSolidMesh = CubeFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		DefaultCylinderMesh = CylinderFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		DefaultConeMesh = ConeFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		DefaultSphereMesh = SphereFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		DefaultMaterial = MaterialFinder.Object;
	}
}

void ASpotSciFiMazeGeneratorVolume::PostLoad()
{
	Super::PostLoad();
	EnsureGeneratorGuid();
}

void ASpotSciFiMazeGeneratorVolume::PostActorCreated()
{
	Super::PostActorCreated();
	EnsureGeneratorGuid();
}

#if WITH_EDITOR
void ASpotSciFiMazeGeneratorVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	CellSize = FMath::Max(CellSize, 200.0f);
	RoadWidth = FMath::Clamp(RoadWidth, 100.0f, CellSize * 0.85f);
	BuildingDensity = FMath::Clamp(BuildingDensity, 0.0f, 1.0f);
	Branchiness = FMath::Clamp(Branchiness, 0.0f, 1.0f);
	LoopChance = FMath::Clamp(LoopChance, 0.0f, 1.0f);
	HeightVariation = FMath::Max(HeightVariation, 0.0f);
	AlienPatternIntensity = FMath::Clamp(AlienPatternIntensity, 0.0f, 1.0f);
	MaxBuildings = FMath::Max(MaxBuildings, 0);
	MaxCorridors = FMath::Max(MaxCorridors, 8);
}
#endif

void ASpotSciFiMazeGeneratorVolume::GenerateCityMaze()
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("GenerateCityMazeTransaction", "Generate Sci-Fi City Maze"));
	Modify();
	if (ULevel* Level = GetLevel())
	{
		Level->Modify();
	}
#endif

	GenerateCityMazeInternal();
}

void ASpotSciFiMazeGeneratorVolume::ClearGenerated()
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("ClearCityMazeTransaction", "Clear Sci-Fi City Maze"));
	Modify();
	if (ULevel* Level = GetLevel())
	{
		Level->Modify();
	}
#endif

	ClearGeneratedInternal();
	MarkOwningLevelDirty();
}

void ASpotSciFiMazeGeneratorVolume::RandomizeSeedAndGenerate()
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("RandomizeCityMazeTransaction", "Randomize Sci-Fi City Maze"));
	Modify();
	if (ULevel* Level = GetLevel())
	{
		Level->Modify();
	}
#endif

	Seed = FMath::RandRange(1, MAX_int32 - 1);
	GenerateCityMazeInternal();
}

void ASpotSciFiMazeGeneratorVolume::EnsureGeneratorGuid()
{
	if (!GeneratorGuid.IsValid())
	{
		GeneratorGuid = FGuid::NewGuid();
	}
}

bool ASpotSciFiMazeGeneratorVolume::ValidateGenerationSettings() const
{
	bool bValid = true;

	if (!BoundsComponent)
	{
		UE_LOG(LogSciFiMazeGenerator, Warning, TEXT("%s has no bounds component."), *GetName());
		return false;
	}

	const FVector Extent = BoundsComponent->GetScaledBoxExtent();
	if (Extent.X < CellSize || Extent.Y < CellSize)
	{
		UE_LOG(LogSciFiMazeGenerator, Warning, TEXT("%s bounds are smaller than one cell. Increase the box extent or reduce CellSize."), *GetName());
		bValid = false;
	}

	if (RoadWidth >= CellSize * 0.9f)
	{
		UE_LOG(LogSciFiMazeGenerator, Warning, TEXT("%s RoadWidth is too close to CellSize. Use a narrower road to leave building/navigation clearance."), *GetName());
		bValid = false;
	}

	if (MaxCorridors < 8)
	{
		UE_LOG(LogSciFiMazeGenerator, Warning, TEXT("%s MaxCorridors must be at least 8 for a useful connected maze."), *GetName());
		bValid = false;
	}

	if (RoadMeshes.IsEmpty())
	{
		UE_LOG(LogSciFiMazeGenerator, Display, TEXT("%s has no RoadMeshes assigned; using /Engine/BasicShapes/Plane."), *GetName());
	}
	if (BuildingMeshes.IsEmpty())
	{
		UE_LOG(LogSciFiMazeGenerator, Display, TEXT("%s has no BuildingMeshes assigned; using /Engine/BasicShapes/Cube."), *GetName());
	}

	return bValid;
}

void ASpotSciFiMazeGeneratorVolume::GenerateCityMazeInternal()
{
	EnsureGeneratorGuid();

	if (!ValidateGenerationSettings())
	{
		return;
	}

	ClearGeneratedInternal();

	UWorld* World = GetWorld();
	if (!World || !BoundsComponent)
	{
		return;
	}

	FRandomStream Stream(Seed);

	const FVector BoundsExtent = BoundsComponent->GetScaledBoxExtent();
	int32 GridWidth = FMath::Max(3, FMath::FloorToInt((BoundsExtent.X * 2.0f) / CellSize));
	int32 GridHeight = FMath::Max(3, FMath::FloorToInt((BoundsExtent.Y * 2.0f) / CellSize));

	while ((GridWidth * GridHeight - 1) > MaxCorridors && (GridWidth > 3 || GridHeight > 3))
	{
		if (GridWidth >= GridHeight && GridWidth > 3)
		{
			--GridWidth;
		}
		else if (GridHeight > 3)
		{
			--GridHeight;
		}
		else
		{
			break;
		}
	}

	if ((GridWidth * GridHeight - 1) > MaxCorridors)
	{
		UE_LOG(LogSciFiMazeGenerator, Warning, TEXT("%s MaxCorridors is too low for the minimum 3x3 connected grid; generation will use the minimum grid."), *GetName());
	}

	const float GridWorldWidth = (GridWidth - 1) * CellSize;
	const float GridWorldHeight = (GridHeight - 1) * CellSize;
	const float MaxNodeJitter = FMath::Clamp((CellSize - RoadWidth) * 0.12f, 0.0f, CellSize * 0.08f) * AlienPatternIntensity;

	TMap<FIntPoint, FVector2D> NodePositions;
	NodePositions.Reserve(GridWidth * GridHeight);

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const float BaseX = -GridWorldWidth * 0.5f + X * CellSize;
			const float BaseY = -GridWorldHeight * 0.5f + Y * CellSize;
			const bool bBoundaryNode = X == 0 || Y == 0 || X == GridWidth - 1 || Y == GridHeight - 1;
			const float JitterScale = bBoundaryNode ? 0.35f : 1.0f;
			const FVector2D Jitter(
				Stream.FRandRange(-MaxNodeJitter, MaxNodeJitter) * JitterScale,
				Stream.FRandRange(-MaxNodeJitter, MaxNodeJitter) * JitterScale);

			NodePositions.Add(FIntPoint(X, Y), FVector2D(BaseX, BaseY) + Jitter);
		}
	}

	auto IsInsideGrid = [GridWidth, GridHeight](const FIntPoint& Coord)
	{
		return Coord.X >= 0 && Coord.Y >= 0 && Coord.X < GridWidth && Coord.Y < GridHeight;
	};

	auto CardinalNeighbors = [&IsInsideGrid](const FIntPoint& Coord)
	{
		TArray<FIntPoint, TInlineAllocator<4>> Neighbors;
		const FIntPoint Offsets[] =
		{
			FIntPoint(1, 0),
			FIntPoint(-1, 0),
			FIntPoint(0, 1),
			FIntPoint(0, -1)
		};

		for (const FIntPoint& Offset : Offsets)
		{
			const FIntPoint Candidate = Coord + Offset;
			if (IsInsideGrid(Candidate))
			{
				Neighbors.Add(Candidate);
			}
		}

		return Neighbors;
	};

	TSet<FIntPoint> Visited;
	TArray<FIntPoint> Active;
	TSet<FGridEdge> EdgeSet;
	TArray<FGridEdge> MazeEdges;

	const FIntPoint StartCell(
		FMath::Clamp(GridWidth / 2 + Stream.RandRange(-1, 1), 0, GridWidth - 1),
		FMath::Clamp(GridHeight / 2 + Stream.RandRange(-1, 1), 0, GridHeight - 1));

	Visited.Add(StartCell);
	Active.Add(StartCell);

	while (!Active.IsEmpty())
	{
		const int32 ActiveIndex = Stream.FRand() < Branchiness ? Stream.RandRange(0, Active.Num() - 1) : Active.Num() - 1;
		const FIntPoint Current = Active[ActiveIndex];

		TArray<FIntPoint> UnvisitedNeighbors;
		for (const FIntPoint& Neighbor : CardinalNeighbors(Current))
		{
			if (!Visited.Contains(Neighbor))
			{
				UnvisitedNeighbors.Add(Neighbor);
			}
		}

		if (UnvisitedNeighbors.IsEmpty())
		{
			Active.RemoveAtSwap(ActiveIndex);
			continue;
		}

		const FIntPoint Next = UnvisitedNeighbors[Stream.RandRange(0, UnvisitedNeighbors.Num() - 1)];
		const FGridEdge NewEdge(Current, Next);
		EdgeSet.Add(NewEdge);
		MazeEdges.Add(NewEdge);
		Visited.Add(Next);
		Active.Add(Next);
	}

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const FIntPoint Current(X, Y);
			for (const FIntPoint& Neighbor : CardinalNeighbors(Current))
			{
				const FGridEdge Candidate(Current, Neighbor);
				const float UrbanLoopChance = FMath::Clamp(LoopChance + BuildingDensity * 0.18f, 0.0f, 1.0f);
				if (!EdgeSet.Contains(Candidate) && Stream.FRand() < UrbanLoopChance && MazeEdges.Num() < MaxCorridors)
				{
					EdgeSet.Add(Candidate);
					MazeEdges.Add(Candidate);
				}
			}
		}
	}

	TArray<FRoadSegment> RoadSegments;
	RoadSegments.Reserve(MazeEdges.Num() + 64);

	for (const FGridEdge& Edge : MazeEdges)
	{
		const FVector2D* Start = NodePositions.Find(Edge.A);
		const FVector2D* End = NodePositions.Find(Edge.B);
		if (Start && End)
		{
			FRoadSegment& Segment = RoadSegments.AddDefaulted_GetRef();
			Segment.Start = *Start;
			Segment.End = *End;
			Segment.Width = RoadWidth;
			Segment.bMajor = false;
			Segment.Label = TEXT("Corridor");
		}
	}

	TArray<FCircularRoadArea> Plazas;
	TArray<FVector2D> HubPositions;
	const int32 HubCount = FMath::Clamp(1 + FMath::RoundToInt(AlienPatternIntensity * 4.0f), 1, 5);
	HubPositions.Reserve(HubCount);
	HubPositions.Add(NodePositions.FindChecked(StartCell));

	for (int32 HubIndex = 1; HubIndex < HubCount; ++HubIndex)
	{
		const FIntPoint HubCell(Stream.RandRange(1, GridWidth - 2), Stream.RandRange(1, GridHeight - 2));
		HubPositions.Add(NodePositions.FindChecked(HubCell));
	}

	for (int32 HubIndex = 0; HubIndex < HubPositions.Num(); ++HubIndex)
	{
		const FVector2D Hub = HubPositions[HubIndex];
		const float PlazaRadius = FMath::Lerp(RoadWidth * 0.95f, CellSize * 0.72f, AlienPatternIntensity) * Stream.FRandRange(0.85f, 1.25f);

		FCircularRoadArea& Plaza = Plazas.AddDefaulted_GetRef();
		Plaza.Center = Hub;
		Plaza.Radius = PlazaRadius;
		Plaza.Width = RoadWidth * 1.15f;

		const int32 RingSegments = FMath::Clamp(8 + FMath::RoundToInt(AlienPatternIntensity * 8.0f), 8, 16);
		for (int32 RingIndex = 0; RingIndex < RingSegments; ++RingIndex)
		{
			const float AngleA = (TWO_PI * RingIndex) / RingSegments;
			const float AngleB = (TWO_PI * (RingIndex + 1)) / RingSegments;
			const FVector2D A = Hub + FVector2D(FMath::Cos(AngleA), FMath::Sin(AngleA)) * PlazaRadius;
			const FVector2D B = Hub + FVector2D(FMath::Cos(AngleB), FMath::Sin(AngleB)) * PlazaRadius;

			FRoadSegment& Ring = RoadSegments.AddDefaulted_GetRef();
			Ring.Start = A;
			Ring.End = B;
			Ring.Width = RoadWidth * 0.72f;
			Ring.bMajor = true;
			Ring.Label = TEXT("RingRoad");
		}

		const int32 SpokeCount = FMath::Clamp(2 + FMath::RoundToInt(AlienPatternIntensity * 3.0f), 2, 5);
		for (int32 SpokeIndex = 0; SpokeIndex < SpokeCount; ++SpokeIndex)
		{
			const float Angle = TWO_PI * (static_cast<float>(SpokeIndex) / SpokeCount) + Stream.FRandRange(-0.25f, 0.25f);
			FRoadSegment& Spoke = RoadSegments.AddDefaulted_GetRef();
			Spoke.Start = Hub;
			Spoke.End = Hub + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * PlazaRadius;
			Spoke.Width = RoadWidth * 0.82f;
			Spoke.bMajor = true;
			Spoke.Label = TEXT("PlazaSpoke");
		}

		const int32 RadialCount = FMath::Clamp(FMath::RoundToInt(AlienPatternIntensity * 2.0f), 0, 2);
		for (int32 RadialIndex = 0; RadialIndex < RadialCount; ++RadialIndex)
		{
			const float Angle = Stream.FRandRange(0.0f, TWO_PI);
			const float Length = Stream.FRandRange(CellSize * 1.3f, CellSize * 3.1f);
			FVector2D End = Hub + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Length;
			End.X = FMath::Clamp(End.X, -BoundsExtent.X + RoadWidth, BoundsExtent.X - RoadWidth);
			End.Y = FMath::Clamp(End.Y, -BoundsExtent.Y + RoadWidth, BoundsExtent.Y - RoadWidth);

			FRoadSegment& Radial = RoadSegments.AddDefaulted_GetRef();
			Radial.Start = Hub;
			Radial.End = End;
			Radial.Width = RoadWidth * Stream.FRandRange(0.65f, 1.0f);
			Radial.bMajor = true;
			Radial.Label = TEXT("RadialRoad");
		}
	}

	const int32 DiagonalSpurCount = FMath::RoundToInt(MazeEdges.Num() * AlienPatternIntensity * 0.08f);
	for (int32 SpurIndex = 0; SpurIndex < DiagonalSpurCount; ++SpurIndex)
	{
		const FIntPoint SpurCell(Stream.RandRange(0, GridWidth - 1), Stream.RandRange(0, GridHeight - 1));
		const FVector2D Start = NodePositions.FindChecked(SpurCell);
		const float DiagonalBaseAngle = (PI * 0.25f) + (PI * 0.5f) * Stream.RandRange(0, 3);
		const float Angle = DiagonalBaseAngle + Stream.FRandRange(-0.28f, 0.28f) * AlienPatternIntensity;
		const float Length = CellSize * Stream.FRandRange(0.65f, 1.55f);
		FVector2D End = Start + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Length;
		End.X = FMath::Clamp(End.X, -BoundsExtent.X + RoadWidth, BoundsExtent.X - RoadWidth);
		End.Y = FMath::Clamp(End.Y, -BoundsExtent.Y + RoadWidth, BoundsExtent.Y - RoadWidth);

		FRoadSegment& Spur = RoadSegments.AddDefaulted_GetRef();
		Spur.Start = Start;
		Spur.End = End;
		Spur.Width = RoadWidth * Stream.FRandRange(0.45f, 0.7f);
		Spur.bMajor = false;
		Spur.Label = TEXT("DiagonalAlley");
	}

	int32 RoadIndex = 0;
	for (const FRoadSegment& Segment : RoadSegments)
	{
		const float Length = FVector2D::Distance(Segment.Start, Segment.End);
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Segment.End - Segment.Start).GetSafeNormal();
		const float YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		const FString Prefix = Segment.bMajor ? FString::Printf(TEXT("Major_%s_%03d"), *Segment.Label, RoadIndex) : FString::Printf(TEXT("%s_%03d"), *Segment.Label, RoadIndex);
		SpawnGeneratedMeshActor(
			Prefix,
			ChooseMesh(Stream, RoadMeshes, DefaultRoadMesh),
			ChooseMaterial(Stream, RoadMaterials),
			ToLocal3D((Segment.Start + Segment.End) * 0.5f, 0.0f),
			YawDegrees,
			FVector(Length, Segment.Width, DefaultRoadThickness),
			false);
		++RoadIndex;
	}

	int32 PlazaIndex = 0;
	for (const FCircularRoadArea& Plaza : Plazas)
	{
		SpawnGeneratedMeshActor(
			FString::Printf(TEXT("CircularPlaza_%03d"), PlazaIndex),
			ChooseMesh(Stream, RoadMeshes, DefaultRoadMesh),
			ChooseMaterial(Stream, RoadMaterials),
			ToLocal3D(Plaza.Center, -1.0f),
			Stream.FRandRange(0.0f, 180.0f),
			FVector(Plaza.Radius * 2.0f, Plaza.Radius * 2.0f, DefaultRoadThickness),
			false);
		++PlazaIndex;
	}

	TArray<FPlacedSolid> PlacedSolids;
	PlacedSolids.Reserve(MaxBuildings + 32);

	auto IsClearForSolid = [&](const FVector2D& Center, float Radius, bool bDrawRejected) -> bool
	{
		const float BoundsPadding = Radius + RoadWidth * 0.25f;
		if (Center.X < -BoundsExtent.X + BoundsPadding || Center.X > BoundsExtent.X - BoundsPadding ||
			Center.Y < -BoundsExtent.Y + BoundsPadding || Center.Y > BoundsExtent.Y - BoundsPadding)
		{
			return false;
		}

		const float RoadClearance = Radius + RoadWidth * 0.72f;
		for (const FRoadSegment& Segment : RoadSegments)
		{
			if (DistancePointToSegment2D(Center, Segment.Start, Segment.End) < RoadClearance)
			{
				if (bDrawRejected && bDrawDebugPlacementRejections)
				{
					DrawDebugSphere(World, BoundsComponent->GetComponentLocation() + BoundsComponent->GetComponentQuat().RotateVector(ToLocal3D(Center, 120.0f)), 60.0f, 8, FColor::Orange, false, DebugDrawDuration);
				}
				return false;
			}
		}

		for (const FCircularRoadArea& Plaza : Plazas)
		{
			if (FVector2D::Distance(Center, Plaza.Center) < Radius + Plaza.Radius + RoadWidth * 0.55f)
			{
				return false;
			}
		}

		for (const FPlacedSolid& Existing : PlacedSolids)
		{
			if (FVector2D::Distance(Center, Existing.Center) < Radius + Existing.Radius + RoadWidth * 0.18f)
			{
				return false;
			}
		}

		return true;
	};

	auto AddPlacedSolid = [&PlacedSolids](const FVector2D& Center, float Radius)
	{
		FPlacedSolid& Solid = PlacedSolids.AddDefaulted_GetRef();
		Solid.Center = Center;
		Solid.Radius = Radius;
	};

	auto HasValidMesh = [](const TArray<TObjectPtr<UStaticMesh>>& Meshes)
	{
		for (UStaticMesh* Mesh : Meshes)
		{
			if (Mesh)
			{
				return true;
			}
		}
		return false;
	};

	const bool bUseUserBuildingMeshes = HasValidMesh(BuildingMeshes);
	auto ChooseFallbackBuildingMesh = [&](float ShapeRoll) -> UStaticMesh*
	{
		if (bUseUserBuildingMeshes)
		{
			return ChooseMesh(Stream, BuildingMeshes, DefaultSolidMesh);
		}

		if (ShapeRoll < 0.58f)
		{
			return DefaultSolidMesh.Get();
		}
		if (ShapeRoll < 0.83f)
		{
			return DefaultCylinderMesh ? DefaultCylinderMesh.Get() : DefaultSolidMesh.Get();
		}
		if (ShapeRoll < 0.94f)
		{
			return DefaultConeMesh ? DefaultConeMesh.Get() : DefaultSolidMesh.Get();
		}
		return DefaultSphereMesh ? DefaultSphereMesh.Get() : DefaultSolidMesh.Get();
	};

	auto IsClearForParcelSolid = [&](const FVector2D& Center, float Radius) -> bool
	{
		const float BoundsPadding = Radius + RoadWidth * 0.15f;
		if (Center.X < -BoundsExtent.X + BoundsPadding || Center.X > BoundsExtent.X - BoundsPadding ||
			Center.Y < -BoundsExtent.Y + BoundsPadding || Center.Y > BoundsExtent.Y - BoundsPadding)
		{
			return false;
		}

		for (const FRoadSegment& Segment : RoadSegments)
		{
			// Base maze corridors lie on parcel borders; only free-form roads can cut through a parcel.
			if ((Segment.bMajor || Segment.Label == TEXT("DiagonalAlley")) &&
				DistancePointToSegment2D(Center, Segment.Start, Segment.End) < Radius + Segment.Width * 0.62f)
			{
				return false;
			}
		}

		for (const FCircularRoadArea& Plaza : Plazas)
		{
			if (FVector2D::Distance(Center, Plaza.Center) < Radius + Plaza.Radius + RoadWidth * 0.5f)
			{
				return false;
			}
		}

		for (const FPlacedSolid& Existing : PlacedSolids)
		{
			if (FVector2D::Distance(Center, Existing.Center) < Radius + Existing.Radius + RoadWidth * 0.06f)
			{
				return false;
			}
		}

		return true;
	};

	auto SpawnPackedBuilding = [&](const FVector2D& Center, const FVector2D& Size, const FVector2D& AxisX, int32& BuildingsSpawned) -> bool
	{
		if (BuildingsSpawned >= MaxBuildings)
		{
			return false;
		}

		const float ShapeRoll = Stream.FRand();
		const bool bSiloLike = !bUseUserBuildingMeshes && ShapeRoll >= 0.58f;
		const float Radius = Size.Size() * 0.5f;
		if (!IsClearForParcelSolid(Center, Radius))
		{
			return false;
		}

		const float LowRiseMax = FMath::Lerp(1.1f, 1.95f, FMath::Clamp(HeightVariation, 0.0f, 1.0f));
		float Height = CellSize * Stream.FRandRange(0.38f, LowRiseMax);
		FVector TargetSize(Size.X, Size.Y, Height);
		if (bSiloLike)
		{
			const float SiloFootprint = FMath::Min(Size.X, Size.Y) * Stream.FRandRange(0.56f, 0.92f);
			Height = CellSize * Stream.FRandRange(0.55f, 1.55f + HeightVariation * 0.65f);
			TargetSize = FVector(SiloFootprint, SiloFootprint, Height);
		}
		else if (Stream.FRand() < 0.16f)
		{
			// Broad service slabs keep the skyline lower and make corridors feel carved between blocks.
			Height = CellSize * Stream.FRandRange(0.22f, 0.58f + HeightVariation * 0.35f);
			TargetSize.Z = Height;
		}

		const float AxisYaw = FMath::RadiansToDegrees(FMath::Atan2(AxisX.Y, AxisX.X));
		const float SnapYaw = FMath::RoundToFloat(AxisYaw / 90.0f) * 90.0f;
		const float AlienYaw = Stream.FRandRange(-10.0f, 10.0f) * AlienPatternIntensity;

		SpawnGeneratedMeshActor(
			FString::Printf(TEXT("Building_%03d"), BuildingsSpawned),
			ChooseFallbackBuildingMesh(ShapeRoll),
			ChooseMaterial(Stream, BuildingMaterials),
			ToLocal3D(Center, Height * 0.5f),
			SnapYaw + AlienYaw,
			TargetSize,
			true);

		AddPlacedSolid(Center, FMath::Max(TargetSize.X, TargetSize.Y) * 0.55f);
		++BuildingsSpawned;
		return true;
	};

	const int32 DesiredBuildings = FMath::Clamp(FMath::RoundToInt(MaxBuildings * BuildingDensity), 0, MaxBuildings);
	int32 BuildingsSpawned = 0;

	TArray<FIntPoint> ParcelCells;
	ParcelCells.Reserve((GridWidth - 1) * (GridHeight - 1));
	for (int32 Y = 0; Y < GridHeight - 1; ++Y)
	{
		for (int32 X = 0; X < GridWidth - 1; ++X)
		{
			ParcelCells.Add(FIntPoint(X, Y));
		}
	}

	for (int32 Index = ParcelCells.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = Stream.RandRange(0, Index);
		ParcelCells.Swap(Index, SwapIndex);
	}

	for (const FIntPoint& Parcel : ParcelCells)
	{
		if (BuildingsSpawned >= DesiredBuildings)
		{
			break;
		}

		if (Stream.FRand() > BuildingDensity)
		{
			continue;
		}

		const FVector2D P00 = NodePositions.FindChecked(Parcel);
		const FVector2D P10 = NodePositions.FindChecked(Parcel + FIntPoint(1, 0));
		const FVector2D P01 = NodePositions.FindChecked(Parcel + FIntPoint(0, 1));
		const FVector2D P11 = NodePositions.FindChecked(Parcel + FIntPoint(1, 1));

		const FVector2D Center = (P00 + P10 + P01 + P11) * 0.25f;
		const FVector2D AxisX = ((P10 - P00) + (P11 - P01)).GetSafeNormal();
		const FVector2D AxisY = ((P01 - P00) + (P11 - P10)).GetSafeNormal();
		const float ParcelWidth = FMath::Min(FVector2D::Distance(P00, P10), FVector2D::Distance(P01, P11));
		const float ParcelDepth = FMath::Min(FVector2D::Distance(P00, P01), FVector2D::Distance(P10, P11));
		const float BuildableWidth = FMath::Max(80.0f, ParcelWidth - RoadWidth * 1.05f);
		const float BuildableDepth = FMath::Max(80.0f, ParcelDepth - RoadWidth * 1.05f);

		int32 Subdivisions = 1;
		const float SplitRoll = Stream.FRand();
		if (SplitRoll < 0.44f + BuildingDensity * 0.22f)
		{
			Subdivisions = 2;
		}
		if (SplitRoll < 0.18f + BuildingDensity * 0.18f && BuildableWidth > RoadWidth * 1.7f && BuildableDepth > RoadWidth * 1.7f)
		{
			Subdivisions = 4;
		}

		TArray<FVector2D, TInlineAllocator<4>> Offsets;
		TArray<FVector2D, TInlineAllocator<4>> Sizes;
		if (Subdivisions == 1)
		{
			Offsets.Add(FVector2D::ZeroVector);
			Sizes.Add(FVector2D(BuildableWidth * Stream.FRandRange(0.82f, 0.98f), BuildableDepth * Stream.FRandRange(0.82f, 0.98f)));
		}
		else if (Subdivisions == 2)
		{
			const bool bSplitAlongWidth = BuildableWidth >= BuildableDepth;
			for (int32 Part = 0; Part < 2; ++Part)
			{
				const float Sign = Part == 0 ? -1.0f : 1.0f;
				const float OffsetAmount = (bSplitAlongWidth ? BuildableWidth : BuildableDepth) * 0.265f;
				Offsets.Add(bSplitAlongWidth ? FVector2D(Sign * OffsetAmount, 0.0f) : FVector2D(0.0f, Sign * OffsetAmount));
				Sizes.Add(bSplitAlongWidth
					? FVector2D(BuildableWidth * Stream.FRandRange(0.34f, 0.44f), BuildableDepth * Stream.FRandRange(0.78f, 0.96f))
					: FVector2D(BuildableWidth * Stream.FRandRange(0.78f, 0.96f), BuildableDepth * Stream.FRandRange(0.34f, 0.44f)));
			}
		}
		else
		{
			for (int32 PartY = 0; PartY < 2; ++PartY)
			{
				for (int32 PartX = 0; PartX < 2; ++PartX)
				{
					Offsets.Add(FVector2D((PartX == 0 ? -1.0f : 1.0f) * BuildableWidth * 0.255f, (PartY == 0 ? -1.0f : 1.0f) * BuildableDepth * 0.255f));
					Sizes.Add(FVector2D(BuildableWidth * Stream.FRandRange(0.34f, 0.43f), BuildableDepth * Stream.FRandRange(0.34f, 0.43f)));
				}
			}
		}

		for (int32 PartIndex = 0; PartIndex < Offsets.Num() && BuildingsSpawned < DesiredBuildings; ++PartIndex)
		{
			const FVector2D PartCenter = Center + AxisX * Offsets[PartIndex].X + AxisY * Offsets[PartIndex].Y;
			SpawnPackedBuilding(PartCenter, Sizes[PartIndex], AxisX, BuildingsSpawned);
		}

		if (BuildingsSpawned < DesiredBuildings && Stream.FRand() < BuildingDensity * 0.32f)
		{
			const float SiloSize = FMath::Min(BuildableWidth, BuildableDepth) * Stream.FRandRange(0.16f, 0.26f);
			const FVector2D SiloOffset(
				Stream.FRandRange(-BuildableWidth * 0.32f, BuildableWidth * 0.32f),
				Stream.FRandRange(-BuildableDepth * 0.32f, BuildableDepth * 0.32f));
			SpawnPackedBuilding(Center + AxisX * SiloOffset.X + AxisY * SiloOffset.Y, FVector2D(SiloSize, SiloSize), AxisX, BuildingsSpawned);
		}
	}

	const int32 InfillAttempts = FMath::Max((DesiredBuildings - BuildingsSpawned) * MaxPlacementAttemptsMultiplier, 0);
	for (int32 Attempt = 0; Attempt < InfillAttempts && BuildingsSpawned < DesiredBuildings; ++Attempt)
	{
		const float FootprintX = Stream.FRandRange(RoadWidth * 0.42f, CellSize * 0.36f);
		const float FootprintY = Stream.FRandRange(RoadWidth * 0.42f, CellSize * 0.36f);
		const FVector2D Center(Stream.FRandRange(-BoundsExtent.X, BoundsExtent.X), Stream.FRandRange(-BoundsExtent.Y, BoundsExtent.Y));
		if (IsClearForSolid(Center, FVector2D(FootprintX, FootprintY).Size() * 0.5f, true))
		{
			SpawnPackedBuilding(Center, FVector2D(FootprintX, FootprintY), FVector2D::UnitX(), BuildingsSpawned);
		}
	}

	const int32 DesiredObstacles = FMath::Clamp(FMath::RoundToInt(DesiredBuildings * 0.12f + AlienPatternIntensity * 10.0f), 0, FMath::Max(0, MaxBuildings / 4));
	int32 ObstaclesSpawned = 0;
	for (int32 Attempt = 0; Attempt < DesiredObstacles * MaxPlacementAttemptsMultiplier && ObstaclesSpawned < DesiredObstacles; ++Attempt)
	{
		const float Size = Stream.FRandRange(RoadWidth * 0.5f, CellSize * 0.55f);
		const float Radius = Size * 0.72f;
		const FVector2D Center(Stream.FRandRange(-BoundsExtent.X, BoundsExtent.X), Stream.FRandRange(-BoundsExtent.Y, BoundsExtent.Y));

		if (!IsClearForSolid(Center, Radius, false))
		{
			continue;
		}

		const float Height = Stream.FRandRange(RoadWidth * 0.75f, CellSize * (1.0f + HeightVariation));
		SpawnGeneratedMeshActor(
			FString::Printf(TEXT("Obstacle_%03d"), ObstaclesSpawned),
			ChooseMesh(Stream, ObstacleMeshes, DefaultSolidMesh),
			ChooseMaterial(Stream, ObstacleMaterials),
			ToLocal3D(Center, Height * 0.5f),
			Stream.FRandRange(0.0f, 360.0f),
			FVector(Size, Size * Stream.FRandRange(0.65f, 1.25f), Height),
			true);

		AddPlacedSolid(Center, Radius);
		++ObstaclesSpawned;
	}

	int32 LandmarksSpawned = 0;
	for (const FVector2D& Hub : HubPositions)
	{
		if (LandmarksSpawned >= FMath::Max(1, HubCount))
		{
			break;
		}

		const float Angle = Stream.FRandRange(0.0f, TWO_PI);
		const float DistanceFromHub = CellSize * Stream.FRandRange(0.95f, 1.7f);
		const FVector2D Center = Hub + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * DistanceFromHub;
		const float Footprint = Stream.FRandRange(RoadWidth * 0.9f, CellSize * 0.8f);
		const float Radius = Footprint * 0.75f;
		if (!IsClearForSolid(Center, Radius, false))
		{
			continue;
		}

		const float Height = CellSize * Stream.FRandRange(2.0f, 5.0f + HeightVariation * 2.0f);
		SpawnGeneratedMeshActor(
			FString::Printf(TEXT("Landmark_%03d"), LandmarksSpawned),
			ChooseMesh(Stream, LandmarkMeshes, DefaultSolidMesh),
			ChooseMaterial(Stream, LandmarkMaterials),
			ToLocal3D(Center, Height * 0.5f),
			Stream.FRandRange(0.0f, 360.0f),
			FVector(Footprint, Footprint, Height),
			true);

		AddPlacedSolid(Center, Radius);
		++LandmarksSpawned;
	}

	if (bDrawDebugRoadGraph)
	{
		const FQuat BoundsRotation = BoundsComponent->GetComponentQuat();
		const FVector BoundsOrigin = BoundsComponent->GetComponentLocation();
		for (const FRoadSegment& Segment : RoadSegments)
		{
			const FColor Color = Segment.bMajor ? FColor::Cyan : FColor::Green;
			DrawDebugLine(World, BoundsOrigin + BoundsRotation.RotateVector(ToLocal3D(Segment.Start, 45.0f)), BoundsOrigin + BoundsRotation.RotateVector(ToLocal3D(Segment.End, 45.0f)), Color, false, DebugDrawDuration, 0, 18.0f);
		}
	}

	if (bDrawDebugPlazas)
	{
		const FQuat BoundsRotation = BoundsComponent->GetComponentQuat();
		const FVector BoundsOrigin = BoundsComponent->GetComponentLocation();
		for (const FCircularRoadArea& Plaza : Plazas)
		{
			DrawDebugCircle(World, BoundsOrigin + BoundsRotation.RotateVector(ToLocal3D(Plaza.Center, 70.0f)), Plaza.Radius, 48, FColor::Purple, false, DebugDrawDuration, 0, 12.0f, BoundsRotation.GetAxisX(), BoundsRotation.GetAxisY(), false);
		}
	}

	UE_LOG(LogSciFiMazeGenerator, Log, TEXT("%s generated %d road pieces, %d buildings, %d obstacles, %d landmarks using seed %d."),
		*GetName(), RoadIndex + PlazaIndex, BuildingsSpawned, ObstaclesSpawned, LandmarksSpawned, Seed);

	MarkOwningLevelDirty();
}

void ASpotSciFiMazeGeneratorVolume::ClearGeneratedInternal()
{
	EnsureGeneratorGuid();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FName GeneratedTag = GetGeneratedActorTag();
	TArray<AActor*> ActorsToDestroy;
	ActorsToDestroy.Reserve(GeneratedActors.Num());

	for (AActor* Actor : GeneratedActors)
	{
		if (IsValid(Actor))
		{
			ActorsToDestroy.AddUnique(Actor);
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor != this && Actor->Tags.Contains(GeneratedTag))
		{
			ActorsToDestroy.AddUnique(Actor);
		}
	}

	for (AActor* Actor : ActorsToDestroy)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

#if WITH_EDITOR
		Actor->Modify();
#endif
		World->DestroyActor(Actor, false, true);
	}

	GeneratedActors.Reset();
}

void ASpotSciFiMazeGeneratorVolume::MarkOwningLevelDirty() const
{
#if WITH_EDITOR
	if (ULevel* Level = GetLevel())
	{
		Level->MarkPackageDirty();
	}

	if (UWorld* World = GetWorld())
	{
		World->MarkPackageDirty();
	}
#endif
}

FString ASpotSciFiMazeGeneratorVolume::GetGeneratedFolderPath() const
{
#if WITH_EDITOR
	return CleanActorFolderName(GetActorLabel()) + TEXT("_Generated");
#else
	return CleanActorFolderName(GetName()) + TEXT("_Generated");
#endif
}

FName ASpotSciFiMazeGeneratorVolume::GetGeneratedActorTag() const
{
	return FName(*FString::Printf(TEXT("SciFiMazeGenerated_%s"), *GeneratorGuid.ToString(EGuidFormats::Digits)));
}

UStaticMesh* ASpotSciFiMazeGeneratorVolume::ChooseMesh(FRandomStream& Stream, const TArray<TObjectPtr<UStaticMesh>>& Meshes, UStaticMesh* FallbackMesh) const
{
	TArray<UStaticMesh*, TInlineAllocator<16>> ValidMeshes;
	for (UStaticMesh* Mesh : Meshes)
	{
		if (Mesh)
		{
			ValidMeshes.Add(Mesh);
		}
	}

	if (!ValidMeshes.IsEmpty())
	{
		return ValidMeshes[Stream.RandRange(0, ValidMeshes.Num() - 1)];
	}

	return FallbackMesh;
}

UMaterialInterface* ASpotSciFiMazeGeneratorVolume::ChooseMaterial(FRandomStream& Stream, const TArray<TObjectPtr<UMaterialInterface>>& Materials) const
{
	TArray<UMaterialInterface*, TInlineAllocator<16>> ValidMaterials;
	for (UMaterialInterface* Material : Materials)
	{
		if (Material)
		{
			ValidMaterials.Add(Material);
		}
	}

	if (!ValidMaterials.IsEmpty())
	{
		return ValidMaterials[Stream.RandRange(0, ValidMaterials.Num() - 1)];
	}

	return DefaultMaterial;
}

AActor* ASpotSciFiMazeGeneratorVolume::SpawnGeneratedMeshActor(
	const FString& LabelPrefix,
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FVector& LocalCenter,
	float LocalYawDegrees,
	const FVector& TargetWorldSize,
	bool bBlocksNavigation)
{
	if (!Mesh || !BoundsComponent)
	{
		UE_LOG(LogSciFiMazeGenerator, Warning, TEXT("%s could not spawn %s because no static mesh was available."), *GetName(), *LabelPrefix);
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FQuat BoundsRotation = BoundsComponent->GetComponentQuat();
	const FVector WorldLocation = BoundsComponent->GetComponentLocation() + BoundsRotation.RotateVector(LocalCenter);
	const FRotator WorldRotation = (BoundsRotation * FQuat(FRotator(0.0f, LocalYawDegrees, 0.0f))).Rotator();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.OverrideLevel = GetLevel();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = MakeUniqueObjectName(GetLevel(), AStaticMeshActor::StaticClass(), FName(*FString::Printf(TEXT("%s_%s"), *GetName(), *LabelPrefix)));

	AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldLocation, WorldRotation, SpawnParameters);
	if (!MeshActor)
	{
		return nullptr;
	}

	MeshActor->SetFlags(RF_Transactional);
	MeshActor->Tags.AddUnique(GetGeneratedActorTag());
	MeshActor->SetActorEnableCollision(bBlocksNavigation);

#if WITH_EDITOR
	MeshActor->Modify();
	MeshActor->SetActorLabel(FString::Printf(TEXT("%s_%s"), *GetActorLabel(), *LabelPrefix), true);
	MeshActor->SetFolderPath(FName(*GetGeneratedFolderPath()));
#endif

	UStaticMeshComponent* MeshComponent = MeshActor->GetStaticMeshComponent();
	check(MeshComponent);
	MeshComponent->SetFlags(RF_Transactional);
	MeshComponent->SetMobility(EComponentMobility::Static);
	MeshComponent->SetStaticMesh(Mesh);

	if (Material)
	{
		MeshComponent->SetMaterial(0, Material);
	}

	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	const FVector MeshSize(
		FMath::Max(MeshBounds.BoxExtent.X * 2.0f, 1.0f),
		FMath::Max(MeshBounds.BoxExtent.Y * 2.0f, 1.0f),
		FMath::Max(MeshBounds.BoxExtent.Z * 2.0f, 1.0f));

	const FVector Scale(
		FMath::Max(TargetWorldSize.X, 1.0f) / MeshSize.X,
		FMath::Max(TargetWorldSize.Y, 1.0f) / MeshSize.Y,
		MeshBounds.BoxExtent.Z <= KINDA_SMALL_NUMBER ? 1.0f : FMath::Max(TargetWorldSize.Z, 1.0f) / MeshSize.Z);

	MeshComponent->SetWorldScale3D(Scale);

	if (bBlocksNavigation)
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		MeshComponent->SetCanEverAffectNavigation(true);
	}
	else
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComponent->SetCanEverAffectNavigation(false);
	}

	GeneratedActors.Add(MeshActor);
	return MeshActor;
}

#undef LOCTEXT_NAMESPACE
