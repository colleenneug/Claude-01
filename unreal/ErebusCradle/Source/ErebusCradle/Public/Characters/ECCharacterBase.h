#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ECCharacterBase.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FECOnDamaged, float, NewHealth, float, DamageAmount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FECOnDied);

/** Vitals shared by everything that can be shot: players and hostiles alike.
 *  Doctrine passives (Bulkhead Plating's flat mitigation) and shields (Aegis
 *  Barrier's overshield) both hook through TakeDamage here rather than each
 *  caller re-deriving the math. */
UCLASS(Abstract)
class EREBUSCRADLE_API AECCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	AECCharacterBase();

	UPROPERTY(BlueprintAssignable, Category = "Erebus Cradle|Vitals")
	FECOnDamaged OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "Erebus Cradle|Vitals")
	FECOnDied OnDied;

	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	float GetShield() const { return Shield; }

	/** Adds overshield that soaks damage before Health does. Used by Aegis Barrier. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	void AddShield(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	void Heal(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	virtual void InitializeVitals(float InMaxHealth, float InMaxEnergy = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	float GetEnergy() const { return Energy; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	float GetMaxEnergy() const { return MaxEnergy; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	bool HasEnergy(float Amount) const { return Energy >= Amount; }

	/** Returns false (and spends nothing) if Energy is short. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	bool SpendEnergy(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	void RegenEnergy(float Amount);

	/** Phase Step's blink window: incoming damage is ignored while this is set. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	void SetTemporaryInvulnerability(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Vitals")
	bool IsInvulnerable() const { return bIsInvulnerable; }

	/** 0.22 for a Bulwark; 0 for anything with no flat damage mitigation. Wraith's
	 *  Phase Step primes a temporary damage vulnerability window separately —
	 *  see ECPhaseStepComponent — so this stays a plain doctrine constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erebus Cradle|Vitals")
	float DamageMitigation = 0.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Vitals")
	float Health = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Vitals")
	float MaxHealth = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Vitals")
	float Shield = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Vitals")
	float Energy = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Vitals")
	float MaxEnergy = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Vitals")
	bool bIsDead = false;

	virtual void Die();

	bool bIsInvulnerable = false;
	FTimerHandle InvulnerabilityTimer;
};
