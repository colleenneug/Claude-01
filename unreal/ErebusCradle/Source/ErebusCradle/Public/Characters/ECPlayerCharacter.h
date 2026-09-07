#pragma once

#include "CoreMinimal.h"
#include "Characters/ECCharacterBase.h"
#include "ECTypes.h"
#include "ECPlayerCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class AECWeaponBase;
class UECFieldAbilityComponent;

/** The one player character class. Which doctrine it plays as is a runtime choice
 *  (InitializeDoctrine), not a subclass — Bulwark/Oracle/Wraith all run through this
 *  same pawn with different FECDoctrineDefinition data plugged in, matching the
 *  browser build's classes.js: one character record, a doctrine id, and everything
 *  else derived from the doctrine table. */
UCLASS()
class EREBUSCRADLE_API AECPlayerCharacter : public AECCharacterBase
{
	GENERATED_BODY()

public:
	AECPlayerCharacter();

	/** Applies a doctrine's vitals, attributes, weapon and field ability to this
	 *  pawn. Call once after spawning (or on respawn) with the definition looked
	 *  up from UECGameInstance::GetDoctrineDefinition. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Doctrine")
	void InitializeDoctrine(const FECDoctrineDefinition& InDoctrine);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Doctrine")
	EECDoctrine GetDoctrine() const { return CurrentDoctrine.Doctrine; }

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Doctrine")
	const FECAttributeSet& GetAttributes() const { return Attributes; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle")
	TObjectPtr<AECWeaponBase> EquippedWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle")
	TObjectPtr<UECFieldAbilityComponent> FieldAbility;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Erebus Cradle|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Assign the Input Mapping Context and Input Actions on a Blueprint child of
	 *  this class (or in a derived C++ class's constructor) — Enhanced Input assets
	 *  are content, and this project ships no Content/ folder. See unreal/README.md. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Input")
	TObjectPtr<UInputAction> FieldAbilityAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erebus Cradle|Movement")
	float WalkSpeed = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erebus Cradle|Movement")
	float SprintSpeedMultiplier = 1.6f;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void StartCrouchHeld();
	void StopCrouchHeld();
	void StartFire();
	void StopFire();
	void StartAim();
	void StopAim();
	void DoReload();
	void DoFieldAbility();

private:
	FECDoctrineDefinition CurrentDoctrine;
	FECAttributeSet Attributes;
};
