#include "Characters/ECCharacterBase.h"
#include "GameFramework/CharacterMovementComponent.h"

AECCharacterBase::AECCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AECCharacterBase::InitializeVitals(float InMaxHealth, float InMaxEnergy)
{
	MaxHealth = InMaxHealth;
	Health = InMaxHealth;
	MaxEnergy = InMaxEnergy;
	Energy = InMaxEnergy;
	Shield = 0.f;
	bIsDead = false;
}

bool AECCharacterBase::SpendEnergy(float Amount)
{
	if (Energy < Amount)
	{
		return false;
	}
	Energy -= Amount;
	return true;
}

void AECCharacterBase::RegenEnergy(float Amount)
{
	Energy = FMath::Clamp(Energy + Amount, 0.f, MaxEnergy);
}

void AECCharacterBase::AddShield(float Amount)
{
	Shield = FMath::Max(0.f, Shield + Amount);
}

void AECCharacterBase::Heal(float Amount)
{
	if (bIsDead || Amount <= 0.f)
	{
		return;
	}
	Health = FMath::Clamp(Health + Amount, 0.f, MaxHealth);
}

void AECCharacterBase::SetTemporaryInvulnerability(float Seconds)
{
	bIsInvulnerable = true;
	GetWorldTimerManager().SetTimer(InvulnerabilityTimer, [this]() { bIsInvulnerable = false; }, Seconds, false);
}

float AECCharacterBase::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float BaseDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (bIsDead || BaseDamage <= 0.f || bIsInvulnerable)
	{
		return 0.f;
	}

	const float Mitigated = BaseDamage * (1.f - FMath::Clamp(DamageMitigation, 0.f, 0.9f));

	float Remaining = Mitigated;
	if (Shield > 0.f)
	{
		const float ShieldAbsorbed = FMath::Min(Shield, Remaining);
		Shield -= ShieldAbsorbed;
		Remaining -= ShieldAbsorbed;
	}

	Health = FMath::Clamp(Health - Remaining, 0.f, MaxHealth);
	OnDamaged.Broadcast(Health, Mitigated);

	if (Health <= 0.f)
	{
		Die();
	}

	return Mitigated;
}

void AECCharacterBase::Die()
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;
	OnDied.Broadcast();

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
	}
	SetActorEnableCollision(false);
}
