#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ECTypes.h"
#include "ECSaveGame.generated.h"

/** One operative's persistent record — the C++/Unreal equivalent of a
 *  storage.js save slot. Three of these are meant to exist at once (three
 *  character bays), selected by SlotName in UECGameInstance. */
UCLASS()
class EREBUSCRADLE_API UECSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	FString CharacterName;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	EECDoctrine Doctrine = EECDoctrine::Bulwark;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	int32 Level = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	int32 XP = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	float MaxHealth = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	float MaxEnergy = 10.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	FECAttributeSet Attributes;

	/** How far down the campaign this operative has reached. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	int32 CampaignUnlockedIndex = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	TMap<FName, bool> ClearedMissions;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	int32 MissionsCompleted = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Save")
	FDateTime CreatedAt;
};
