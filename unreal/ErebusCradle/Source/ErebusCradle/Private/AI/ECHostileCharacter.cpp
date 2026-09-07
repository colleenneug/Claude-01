#include "AI/ECHostileCharacter.h"
#include "AI/ECHostileAIController.h"
#include "Kismet/GameplayStatics.h"

AECHostileCharacter::AECHostileCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	AIControllerClass = AECHostileAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AECHostileCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeVitals(100.f);
}

void AECHostileCharacter::ApplyDifficultyScaling(float HealthMultiplier, float DamageMultiplier)
{
	InitializeVitals(GetMaxHealth() * HealthMultiplier);
	AttackDamage *= DamageMultiplier;
}

void AECHostileCharacter::TryAttack(AActor* Target)
{
	if (!bAttackReady || IsDead() || !Target)
	{
		return;
	}

	const float Distance = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	if (Distance > AttackRange)
	{
		return;
	}

	UGameplayStatics::ApplyDamage(Target, AttackDamage, GetController(), this, nullptr);

	bAttackReady = false;
	GetWorldTimerManager().SetTimer(AttackCooldownTimer, this, &AECHostileCharacter::ResetAttackReady, AttackIntervalSeconds, false);
}

void AECHostileCharacter::Die()
{
	Super::Die();
	GetWorldTimerManager().ClearTimer(AttackCooldownTimer);
}
