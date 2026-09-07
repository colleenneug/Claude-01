#pragma once

#include "CoreMinimal.h"
#include "Abilities/ECFieldAbilityComponent.h"
#include "ECDoctrineAbilities.generated.h"

/** BULWARK — Aegis Barrier: 60 points of overshield that soaks the next wave. */
UCLASS(ClassGroup = (ErebusCradle), meta = (BlueprintSpawnableComponent))
class EREBUSCRADLE_API UECAegisBarrierComponent : public UECFieldAbilityComponent
{
	GENERATED_BODY()

public:
	UECAegisBarrierComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aegis Barrier")
	float ShieldAmount = 60.f;

protected:
	virtual void Activate(AECCharacterBase* Owner) override;
};

/** ORACLE — Systems Breach: EMP pulse, stunning and damaging everything within range. */
UCLASS(ClassGroup = (ErebusCradle), meta = (BlueprintSpawnableComponent))
class EREBUSCRADLE_API UECSystemsBreachComponent : public UECFieldAbilityComponent
{
	GENERATED_BODY()

public:
	UECSystemsBreachComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Systems Breach")
	float Radius = 1300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Systems Breach")
	float Damage = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Systems Breach")
	float StunSeconds = 2.5f;

protected:
	virtual void Activate(AECCharacterBase* Owner) override;
};

/** WRAITH — Phase Step: blink forward, briefly untargetable, next shot primed for
 *  double damage. The "primed" flag is read and cleared by ECWeaponBase on its next
 *  shot, the same one-shot-buff pattern the browser build uses. */
UCLASS(ClassGroup = (ErebusCradle), meta = (BlueprintSpawnableComponent))
class EREBUSCRADLE_API UECPhaseStepComponent : public UECFieldAbilityComponent
{
	GENERATED_BODY()

public:
	UECPhaseStepComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Step")
	float BlinkDistance = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Step")
	float UntargetableSeconds = 0.4f;

	UFUNCTION(BlueprintCallable, Category = "Phase Step")
	bool ConsumeNextShotBuff();

protected:
	virtual void Activate(AECCharacterBase* Owner) override;

private:
	bool bNextShotPrimed = false;
};
