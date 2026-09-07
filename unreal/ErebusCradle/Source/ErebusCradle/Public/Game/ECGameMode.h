#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ECGameMode.generated.h"

UCLASS()
class EREBUSCRADLE_API AECGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AECGameMode();

protected:
	virtual void BeginPlay() override;
};
