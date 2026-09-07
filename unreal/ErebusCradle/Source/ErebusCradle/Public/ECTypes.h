#pragma once

#include "CoreMinimal.h"
#include "ECTypes.generated.h"

class AECWeaponBase;
class UECFieldAbilityComponent;

/** The three doctrines. Everything a doctrine changes — weapon, ability, passive,
 *  starting vitals — hangs off one FECDoctrineDefinition, so adding a fourth doctrine
 *  is data, not a new character subclass. */
UENUM(BlueprintType)
enum class EECDoctrine : uint8
{
	Bulwark UMETA(DisplayName = "Bulwark - Aegis Doctrine"),
	Oracle  UMETA(DisplayName = "Oracle - Signal Doctrine"),
	Wraith  UMETA(DisplayName = "Wraith - Umbral Doctrine")
};

USTRUCT(BlueprintType)
struct FECAttributeSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
	float Might = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
	float Sync = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
	float Guile = 0.f;
};

/** Hitscan weapon stats, kept mathematically identical to the browser build's
 *  fps/weapons.js so the doctrines feel the same everywhere the game ships. */
USTRUCT(BlueprintType)
struct FECWeaponStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float Damage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	int32 PelletsPerShot = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float RoundsPerMinute = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	int32 ReserveAmmo = 90;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float ReloadSeconds = 2.f;

	/** Unreal units (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float MaxRange = 9000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float SpreadDegrees = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float AimedSpreadDegrees = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float HeadshotMultiplier = 2.f;

	/** ORACLE's ARC LANCE: a bolt keeps going through its target and into whatever
	 *  stood behind it, instead of stopping at the first hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	bool bPiercing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FText DisplayName;
};

USTRUCT(BlueprintType)
struct FECDoctrineDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	EECDoctrine Doctrine = EECDoctrine::Bulwark;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FText Role;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FText Tagline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FText PerkName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FText PerkDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	float BaseHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	float BaseEnergy = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FECAttributeSet BaseAttributes;

	/** Vitals gained, and which attribute goes up, each time this doctrine ranks up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	float HealthPerLevel = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	float EnergyPerEvenLevel = 1.f;

	/** 0.22 for Bulwark's Bulkhead Plating; 0 for doctrines with no flat mitigation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	float PassiveDamageMitigation = 0.f;

	/** ×3 on a headshot for Wraith's Blindside; other doctrines set this in their weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	float MoveSpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	FECWeaponStats Weapon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	TSubclassOf<AECWeaponBase> WeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doctrine")
	TSubclassOf<UECFieldAbilityComponent> FieldAbilityClass;
};
