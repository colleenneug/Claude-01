#include "Game/ECGameMode.h"
#include "Characters/ECPlayerCharacter.h"
#include "Game/ECGameInstance.h"
#include "Kismet/GameplayStatics.h"

AECGameMode::AECGameMode()
{
	DefaultPawnClass = AECPlayerCharacter::StaticClass();
}

void AECGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UECGameInstance* EC_GI = GetGameInstance<UECGameInstance>())
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			if (AECPlayerCharacter* PlayerCharacter = Cast<AECPlayerCharacter>(PlayerPawn))
			{
				PlayerCharacter->InitializeDoctrine(EC_GI->GetActiveDoctrineDefinition());
			}
		}
	}
}
