#include "Weapons/ECWeaponBase.h"
#include "Characters/ECCharacterBase.h"
#include "Abilities/ECDoctrineAbilities.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

AECWeaponBase::AECWeaponBase()
{
	PrimaryActorTick.bCanEverTick = false;

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
}

void AECWeaponBase::BeginPlay()
{
	Super::BeginPlay();
	AmmoInMagazine = Stats.MagazineSize;
	ReserveAmmo = Stats.ReserveAmmo;
}

void AECWeaponBase::EquipTo(AECCharacterBase* NewOwnerCharacter)
{
	OwnerCharacter = NewOwnerCharacter;
	SetOwner(NewOwnerCharacter);
}

void AECWeaponBase::StartFire()
{
	if (bWantsToFire)
	{
		return;
	}
	bWantsToFire = true;

	FireOnce();
	const float Interval = 60.f / FMath::Max(1.f, Stats.RoundsPerMinute);
	GetWorldTimerManager().SetTimer(FireTimer, this, &AECWeaponBase::FireOnce, Interval, true);
}

void AECWeaponBase::StopFire()
{
	bWantsToFire = false;
	GetWorldTimerManager().ClearTimer(FireTimer);
}

void AECWeaponBase::Reload()
{
	if (bIsReloading || AmmoInMagazine >= Stats.MagazineSize || ReserveAmmo <= 0)
	{
		return;
	}
	bIsReloading = true;
	StopFire();
	GetWorldTimerManager().SetTimer(ReloadTimer, this, &AECWeaponBase::FinishReload, Stats.ReloadSeconds, false);
}

void AECWeaponBase::FinishReload()
{
	const int32 Needed = Stats.MagazineSize - AmmoInMagazine;
	const int32 Loaded = FMath::Min(Needed, ReserveAmmo);
	AmmoInMagazine += Loaded;
	ReserveAmmo -= Loaded;
	bIsReloading = false;
}

void AECWeaponBase::FireOnce()
{
	if (!OwnerCharacter || OwnerCharacter->IsDead() || bIsReloading)
	{
		StopFire();
		return;
	}

	if (AmmoInMagazine <= 0)
	{
		StopFire();
		Reload();
		return;
	}

	AmmoInMagazine--;

	FVector EyeLocation;
	FRotator EyeRotation;
	OwnerCharacter->GetActorEyesViewPoint(EyeLocation, EyeRotation);

	float DamageMultiplier = 1.f;
	if (UECPhaseStepComponent* PhaseStep = OwnerCharacter->FindComponentByClass<UECPhaseStepComponent>())
	{
		if (PhaseStep->ConsumeNextShotBuff())
		{
			DamageMultiplier = PrimedShotDamageMultiplier;
		}
	}

	const float SpreadDegrees = bIsAiming ? Stats.AimedSpreadDegrees : Stats.SpreadDegrees;

	for (int32 Pellet = 0; Pellet < FMath::Max(1, Stats.PelletsPerShot); ++Pellet)
	{
		const FVector ShotDirection = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(EyeRotation.Vector(), SpreadDegrees);
		const FVector TraceEnd = EyeLocation + ShotDirection * Stats.MaxRange;

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerCharacter);
		QueryParams.AddIgnoredActor(this);
		QueryParams.bTraceComplex = true;

		TArray<FHitResult> Hits;
		GetWorld()->LineTraceMultiByChannel(Hits, EyeLocation, TraceEnd, ECC_Visibility, QueryParams);

		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor)
			{
				continue;
			}

			const bool bHeadshot = Hit.BoneName.ToString().Contains(TEXT("head"), ESearchCase::IgnoreCase);
			float Damage = Stats.Damage * DamageMultiplier;
			if (bHeadshot)
			{
				Damage *= Stats.HeadshotMultiplier;
			}

			UGameplayStatics::ApplyPointDamage(HitActor, Damage, ShotDirection, Hit,
				OwnerCharacter->GetController(), this, nullptr);

			if (!Stats.bPiercing)
			{
				break;
			}
		}
	}
}
