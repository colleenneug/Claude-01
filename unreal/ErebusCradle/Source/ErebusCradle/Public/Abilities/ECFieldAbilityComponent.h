#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ECFieldAbilityComponent.generated.h"

class AECCharacterBase;

/** Base for the three field abilities (Aegis Barrier, Systems Breach, Phase Step).
 *  Each doctrine's FECDoctrineDefinition::FieldAbilityClass names one of these; the
 *  player character adds whichever class the doctrine calls for, so the ability
 *  itself decides what "activate" means rather than the character branching on
 *  doctrine. */
UCLASS(Abstract, ClassGroup = (ErebusCradle), meta = (BlueprintSpawnableComponent))
class EREBUSCRADLE_API UECFieldAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UECFieldAbilityComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Ability")
	float EnergyCost = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Ability")
	float CooldownSeconds = 15.f;

	UFUNCTION(BlueprintCallable, Category = "Field Ability")
	bool TryActivate();

	UFUNCTION(BlueprintCallable, Category = "Field Ability")
	bool IsOnCooldown() const;

	UFUNCTION(BlueprintCallable, Category = "Field Ability")
	float GetCooldownRemaining() const;

protected:
	/** The actual effect. Called only once energy/cooldown have already cleared. */
	virtual void Activate(AECCharacterBase* Owner) PURE_VIRTUAL(UECFieldAbilityComponent::Activate, );

	UPROPERTY(Transient)
	float LastActivationTime = -1000.f;
};
