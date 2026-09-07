#include "Abilities/ECDoctrineAbilities.h"
#include "Characters/ECCharacterBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/OverlapResult.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"

UECAegisBarrierComponent::UECAegisBarrierComponent()
{
	EnergyCost = 0.f;
	CooldownSeconds = 16.f;
}

void UECAegisBarrierComponent::Activate(AECCharacterBase* Owner)
{
	Owner->AddShield(ShieldAmount);
}

UECSystemsBreachComponent::UECSystemsBreachComponent()
{
	EnergyCost = 0.f;
	CooldownSeconds = 14.f;
}

void UECSystemsBreachComponent::Activate(AECCharacterBase* Owner)
{
	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
	World->OverlapMultiByObjectType(Overlaps, Owner->GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), Sphere);

	for (const FOverlapResult& Result : Overlaps)
	{
		AECCharacterBase* Hit = Cast<AECCharacterBase>(Result.GetActor());
		if (Hit && Hit != Owner && !Hit->IsDead())
		{
			UGameplayStatics::ApplyDamage(Hit, Damage, Owner->GetController(), Owner, nullptr);
			if (UCharacterMovementComponent* Movement = Hit->GetCharacterMovement())
			{
				Movement->DisableMovement();
				FTimerHandle StunTimer;
				TWeakObjectPtr<AECCharacterBase> WeakHit = Hit;
				World->GetTimerManager().SetTimer(StunTimer, [WeakHit]()
				{
					if (WeakHit.IsValid() && !WeakHit->IsDead())
					{
						if (UCharacterMovementComponent* Move = WeakHit->GetCharacterMovement())
						{
							Move->SetMovementMode(MOVE_Walking);
						}
					}
				}, StunSeconds, false);
			}
		}
	}
}

UECPhaseStepComponent::UECPhaseStepComponent()
{
	EnergyCost = 0.f;
	CooldownSeconds = 10.f;
}

void UECPhaseStepComponent::Activate(AECCharacterBase* Owner)
{
	const FVector Forward = Owner->GetActorForwardVector();
	const FVector Destination = Owner->GetActorLocation() + Forward * BlinkDistance;
	Owner->SetActorLocation(Destination, true);
	Owner->SetTemporaryInvulnerability(UntargetableSeconds);
	bNextShotPrimed = true;
}

bool UECPhaseStepComponent::ConsumeNextShotBuff()
{
	const bool bWasPrimed = bNextShotPrimed;
	bNextShotPrimed = false;
	return bWasPrimed;
}
