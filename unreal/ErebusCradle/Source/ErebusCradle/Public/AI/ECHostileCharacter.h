#pragma once

#include "CoreMinimal.h"
#include "Characters/ECCharacterBase.h"
#include "ECHostileCharacter.generated.h"

/** A shootable hostile. Steering and target selection live in
 *  ECHostileAIController (via AIPerception, no Behavior Tree asset needed);
 *  this class only owns combat stats and the attack itself, mirroring the
 *  browser build's fps/ai.js idle -> chase -> attack -> die state machine. */
UCLASS()
class EREBUSCRADLE_API AECHostileCharacter : public AECCharacterBase
{
	GENERATED_BODY()

public:
	AECHostileCharacter();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hostile")
	float AttackDamage = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hostile")
	float AttackRange = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hostile")
	float AttackIntervalSeconds = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hostile")
	float SightRadius = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hostile")
	float SightAgeSeconds = 5.f;

	UFUNCTION(BlueprintCallable, Category = "Hostile")
	void TryAttack(AActor* Target);

	/** Scales Health/AttackDamage for the escalation curve — see campaign.js's
	 *  per-mission health/damage multipliers, ported 1:1 as this one call. */
	UFUNCTION(BlueprintCallable, Category = "Hostile")
	void ApplyDifficultyScaling(float HealthMultiplier, float DamageMultiplier);

protected:
	virtual void BeginPlay() override;
	virtual void Die() override;

private:
	FTimerHandle AttackCooldownTimer;
	bool bAttackReady = true;
	void ResetAttackReady() { bAttackReady = true; }
};
