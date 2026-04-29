#include "SpotInvadersGfx.h"
#include "LoopingLandscapePathDriver.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneTracksComponentTypes.h"

namespace
{
float GetSpotPathDriverPlaybackAlpha(const UObject* Object)
{
	const ASpotLoopingLandscapePathDriver* Driver = CastChecked<const ASpotLoopingLandscapePathDriver>(Object);
	return Driver->PlaybackAlpha;
}

void SetSpotPathDriverPlaybackAlpha(UObject* Object, float InPlaybackAlpha)
{
	ASpotLoopingLandscapePathDriver* Driver = CastChecked<ASpotLoopingLandscapePathDriver>(Object);
	Driver->SetPlaybackAlpha(InPlaybackAlpha);
}
}

class FSpotInvadersGfxModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

		// Force Sequencer to drive PlaybackAlpha through SetPlaybackAlpha instead of a raw float write.
		// This keeps the visible actor, camera follow update, and Details-panel value on the same state.
		UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Accessors.Float.Add(
			ASpotLoopingLandscapePathDriver::StaticClass(),
			GET_MEMBER_NAME_CHECKED(ASpotLoopingLandscapePathDriver, PlaybackAlpha),
			GetSpotPathDriverPlaybackAlpha,
			SetSpotPathDriverPlaybackAlpha);
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MovieSceneTracks")))
		{
			UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Accessors.Float.Remove(
				ASpotLoopingLandscapePathDriver::StaticClass(),
				GET_MEMBER_NAME_CHECKED(ASpotLoopingLandscapePathDriver, PlaybackAlpha));
		}

		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FSpotInvadersGfxModule, SpotInvadersGfx, "SpotInvadersGfx");
