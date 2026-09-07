#include "Game/ECProgressionLibrary.h"
#include "Game/ECSaveGame.h"

int32 UECProgressionLibrary::XPRequiredForLevel(int32 Level)
{
	return 40 + (Level - 1) * 55;
}

TArray<FString> UECProgressionLibrary::GrantXP(UECSaveGame* SaveSlot, int32 Amount, const FECDoctrineDefinition& DoctrineDefinition)
{
	TArray<FString> Notices;
	if (!SaveSlot || Amount <= 0)
	{
		return Notices;
	}

	SaveSlot->XP += Amount;

	while (SaveSlot->XP >= XPRequiredForLevel(SaveSlot->Level))
	{
		SaveSlot->XP -= XPRequiredForLevel(SaveSlot->Level);
		SaveSlot->Level++;

		SaveSlot->MaxHealth += DoctrineDefinition.HealthPerLevel;
		if (SaveSlot->Level % 2 == 0)
		{
			SaveSlot->MaxEnergy += DoctrineDefinition.EnergyPerEvenLevel;
		}

		FString AttrName;
		switch (SaveSlot->Doctrine)
		{
		case EECDoctrine::Bulwark:
			SaveSlot->Attributes.Might += 1.f;
			AttrName = TEXT("MIGHT");
			break;
		case EECDoctrine::Oracle:
			SaveSlot->Attributes.Sync += 1.f;
			AttrName = TEXT("SYNC");
			break;
		case EECDoctrine::Wraith:
			SaveSlot->Attributes.Guile += 1.f;
			AttrName = TEXT("GUILE");
			break;
		}

		Notices.Add(FString::Printf(TEXT("RANK %d — +%.0f vitals, +1 %s"),
			SaveSlot->Level, DoctrineDefinition.HealthPerLevel, *AttrName));
	}

	return Notices;
}
