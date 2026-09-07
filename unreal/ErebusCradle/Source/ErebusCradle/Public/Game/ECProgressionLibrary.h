#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ECTypes.h"
#include "ECProgressionLibrary.generated.h"

class UECSaveGame;

UCLASS()
class EREBUSCRADLE_API UECProgressionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** XP needed to clear the given level, matching classes.js's
	 *  xpForLevel: 40 + (lvl - 1) * 55. Kept as one formula, not a table, so the
	 *  two implementations can never drift apart on a single mistyped row. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Progression")
	static int32 XPRequiredForLevel(int32 Level);

	/** Applies XP to a save slot, ranking it up as many times as the XP allows.
	 *  Returns one human-readable notice per rank gained, e.g.
	 *  "RANK 4 — +14 vitals, +1 MIGHT" — the same strings classes.js's
	 *  grantXp produces, for a shared debrief screen. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Progression")
	static TArray<FString> GrantXP(UECSaveGame* SaveSlot, int32 Amount, const FECDoctrineDefinition& DoctrineDefinition);
};
