#include "Abilities/ECFieldAbilityComponent.h"
#include "Characters/ECCharacterBase.h"

UECFieldAbilityComponent::UECFieldAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UECFieldAbilityComponent::IsOnCooldown() const
{
	return GetCooldownRemaining() > 0.f;
}

float UECFieldAbilityComponent::GetCooldownRemaining() const
{
	const float Elapsed = GetWorld() ? GetWorld()->GetTimeSeconds() - LastActivationTime : CooldownSeconds;
	return FMath::Max(0.f, CooldownSeconds - Elapsed);
}

bool UECFieldAbilityComponent::TryActivate()
{
	AECCharacterBase* Owner = Cast<AECCharacterBase>(GetOwner());
	if (!Owner || Owner->IsDead() || IsOnCooldown() || !Owner->HasEnergy(EnergyCost))
	{
		return false;
	}

	Owner->SpendEnergy(EnergyCost);
	LastActivationTime = GetWorld()->GetTimeSeconds();
	Activate(Owner);
	return true;
}
