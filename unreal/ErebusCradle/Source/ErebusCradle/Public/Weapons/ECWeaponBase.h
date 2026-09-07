#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ECTypes.h"
#include "ECWeaponBase.generated.h"

class AECCharacterBase;
class USkeletalMeshComponent;

/** One data-driven hitscan weapon, shared by all three doctrines' guns — the
 *  MAUL-12, the ARC LANCE and the WHISPER are all this class with different
 *  FECWeaponStats, not three different classes. */
UCLASS()
class EREBUSCRADLE_API AECWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AECWeaponBase();

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FECWeaponStats Stats;

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void EquipTo(AECCharacterBase* NewOwnerCharacter);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartFire();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StopFire();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void Reload();

	/** ADS narrows spread to Stats.AimedSpreadDegrees. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetAimingDownSights(bool bAiming) { bIsAiming = bAiming; }

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	bool IsReloading() const { return bIsReloading; }

	/** Wraith's Phase Step primes exactly one shot for double damage; the weapon
	 *  asks its owner's ability component whether that buff is armed and consumes
	 *  it on the shot that uses it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float PrimedShotDamageMultiplier = 3.f;

protected:
	virtual void BeginPlay() override;

	void FireOnce();
	void FinishReload();

	UPROPERTY(Transient)
	TObjectPtr<AECCharacterBase> OwnerCharacter;

	UPROPERTY(Transient)
	int32 AmmoInMagazine = 0;

	UPROPERTY(Transient)
	int32 ReserveAmmo = 0;

	bool bIsAiming = false;
	bool bIsReloading = false;
	bool bWantsToFire = false;

	FTimerHandle FireTimer;
	FTimerHandle ReloadTimer;
};
