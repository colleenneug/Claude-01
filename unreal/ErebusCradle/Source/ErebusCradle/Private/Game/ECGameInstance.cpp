#include "Game/ECGameInstance.h"
#include "Game/ECSaveGame.h"
#include "Kismet/GameplayStatics.h"

void UECGameInstance::Init()
{
	Super::Init();
	BuildDefaultDoctrineTable();
}

/** Numbers ported 1:1 from src/js/classes.js and src/js/fps/weapons.js so the
 *  three doctrines feel identical to the browser build. Spread and range were
 *  authored there in radians and metres; converted here to degrees and
 *  centimetres (Unreal's native units) but otherwise untouched. Weapon and
 *  ability *classes* are left null — assign concrete Blueprint classes on a
 *  Blueprint child of this GameInstance once art/animation exists. */
void UECGameInstance::BuildDefaultDoctrineTable()
{
	DoctrineTable.Empty();

	FECDoctrineDefinition Bulwark;
	Bulwark.Doctrine = EECDoctrine::Bulwark;
	Bulwark.DisplayName = FText::FromString(TEXT("BULWARK"));
	Bulwark.Role = FText::FromString(TEXT("AEGIS DOCTRINE"));
	Bulwark.Tagline = FText::FromString(TEXT("Armour grown from the hull of a dead ship. Walks first, always."));
	Bulwark.PerkName = FText::FromString(TEXT("BULKHEAD PLATING"));
	Bulwark.PerkDescription = FText::FromString(TEXT("Sealed armour absorbs 22% of all incoming damage. The slowest doctrine on its feet, and the only one that can stand in a corridor and trade."));
	Bulwark.BaseHealth = 124.f;
	Bulwark.BaseEnergy = 10.f;
	Bulwark.BaseAttributes.Might = 4.f;
	Bulwark.BaseAttributes.Sync = 1.f;
	Bulwark.BaseAttributes.Guile = 1.f;
	Bulwark.HealthPerLevel = 14.f;
	Bulwark.EnergyPerEvenLevel = 1.f;
	Bulwark.PassiveDamageMitigation = 0.22f;
	Bulwark.Weapon.DisplayName = FText::FromString(TEXT("MAUL-12"));
	Bulwark.Weapon.Damage = 17.f;
	Bulwark.Weapon.PelletsPerShot = 8;
	Bulwark.Weapon.RoundsPerMinute = 75.f;
	Bulwark.Weapon.MagazineSize = 6;
	Bulwark.Weapon.ReserveAmmo = 48;
	Bulwark.Weapon.ReloadSeconds = 2.6f;
	Bulwark.Weapon.SpreadDegrees = 3.15f;
	Bulwark.Weapon.AimedSpreadDegrees = 2.06f;
	Bulwark.Weapon.MaxRange = 3400.f;
	Bulwark.Weapon.HeadshotMultiplier = 1.6f;
	Bulwark.Weapon.bPiercing = false;
	DoctrineTable.Add(EECDoctrine::Bulwark, Bulwark);

	FECDoctrineDefinition Oracle;
	Oracle.Doctrine = EECDoctrine::Oracle;
	Oracle.DisplayName = FText::FromString(TEXT("ORACLE"));
	Oracle.Role = FText::FromString(TEXT("SIGNAL DOCTRINE"));
	Oracle.Tagline = FText::FromString(TEXT("Wetware spliced to a dead god's switchboard. Talks to machines that stopped listening."));
	Oracle.PerkName = FText::FromString(TEXT("GHOST IN THE WIRE"));
	Oracle.PerkDescription = FText::FromString(TEXT("Induction rounds pass through a target and into whatever stood behind it. Deepest magazine and the flattest recoil on the roster."));
	Oracle.BaseHealth = 94.f;
	Oracle.BaseEnergy = 14.f;
	Oracle.BaseAttributes.Might = 1.f;
	Oracle.BaseAttributes.Sync = 5.f;
	Oracle.BaseAttributes.Guile = 2.f;
	Oracle.HealthPerLevel = 9.f;
	Oracle.EnergyPerEvenLevel = 2.f;
	Oracle.PassiveDamageMitigation = 0.f;
	Oracle.Weapon.DisplayName = FText::FromString(TEXT("ARC LANCE"));
	Oracle.Weapon.Damage = 26.f;
	Oracle.Weapon.PelletsPerShot = 1;
	Oracle.Weapon.RoundsPerMinute = 320.f;
	Oracle.Weapon.MagazineSize = 24;
	Oracle.Weapon.ReserveAmmo = 168;
	Oracle.Weapon.ReloadSeconds = 2.0f;
	Oracle.Weapon.SpreadDegrees = 0.69f;
	Oracle.Weapon.AimedSpreadDegrees = 0.11f;
	Oracle.Weapon.MaxRange = 9000.f;
	Oracle.Weapon.HeadshotMultiplier = 2.2f;
	Oracle.Weapon.bPiercing = true;
	DoctrineTable.Add(EECDoctrine::Oracle, Oracle);

	FECDoctrineDefinition Wraith;
	Wraith.Doctrine = EECDoctrine::Wraith;
	Wraith.DisplayName = FText::FromString(TEXT("WRAITH"));
	Wraith.Role = FText::FromString(TEXT("UMBRAL DOCTRINE"));
	Wraith.Tagline = FText::FromString(TEXT("Nine-tenths of a person and all of a knife. Arrives after the alarm should have."));
	Wraith.PerkName = FText::FromString(TEXT("BLINDSIDE"));
	Wraith.PerkDescription = FText::FromString(TEXT("Triple damage on a head shot, and a phase step that leaves the next round primed. Fragile in the open; lethal from an angle nobody covered."));
	Wraith.BaseHealth = 100.f;
	Wraith.BaseEnergy = 12.f;
	Wraith.BaseAttributes.Might = 2.f;
	Wraith.BaseAttributes.Sync = 2.f;
	Wraith.BaseAttributes.Guile = 5.f;
	Wraith.HealthPerLevel = 11.f;
	Wraith.EnergyPerEvenLevel = 1.f;
	Wraith.PassiveDamageMitigation = 0.f;
	Wraith.Weapon.DisplayName = FText::FromString(TEXT("WHISPER"));
	Wraith.Weapon.Damage = 15.f;
	Wraith.Weapon.PelletsPerShot = 1;
	Wraith.Weapon.RoundsPerMinute = 640.f;
	Wraith.Weapon.MagazineSize = 32;
	Wraith.Weapon.ReserveAmmo = 224;
	Wraith.Weapon.ReloadSeconds = 1.7f;
	Wraith.Weapon.SpreadDegrees = 1.15f;
	Wraith.Weapon.AimedSpreadDegrees = 0.29f;
	Wraith.Weapon.MaxRange = 7000.f;
	Wraith.Weapon.HeadshotMultiplier = 3.0f;
	Wraith.Weapon.bPiercing = false;
	DoctrineTable.Add(EECDoctrine::Wraith, Wraith);
}

FECDoctrineDefinition UECGameInstance::GetDoctrineDefinition(EECDoctrine Doctrine) const
{
	if (const FECDoctrineDefinition* Found = DoctrineTable.Find(Doctrine))
	{
		return *Found;
	}
	return FECDoctrineDefinition();
}

FECDoctrineDefinition UECGameInstance::GetActiveDoctrineDefinition() const
{
	const EECDoctrine Doctrine = ActiveSave ? ActiveSave->Doctrine : EECDoctrine::Bulwark;
	FECDoctrineDefinition Definition = GetDoctrineDefinition(Doctrine);

	if (ActiveSave)
	{
		Definition.BaseHealth = ActiveSave->MaxHealth;
		Definition.BaseEnergy = ActiveSave->MaxEnergy;
		Definition.BaseAttributes = ActiveSave->Attributes;
	}

	return Definition;
}

UECSaveGame* UECGameInstance::CreateCharacter(const FString& CharacterName, EECDoctrine Doctrine, const FString& SlotName)
{
	UECSaveGame* NewSave = Cast<UECSaveGame>(UGameplayStatics::CreateSaveGameObject(UECSaveGame::StaticClass()));
	if (!NewSave)
	{
		return nullptr;
	}

	const FECDoctrineDefinition Definition = GetDoctrineDefinition(Doctrine);
	NewSave->CharacterName = CharacterName;
	NewSave->Doctrine = Doctrine;
	NewSave->Level = 1;
	NewSave->XP = 0;
	NewSave->MaxHealth = Definition.BaseHealth;
	NewSave->MaxEnergy = Definition.BaseEnergy;
	NewSave->Attributes = Definition.BaseAttributes;
	NewSave->CampaignUnlockedIndex = 1;
	NewSave->CreatedAt = FDateTime::Now();

	UGameplayStatics::SaveGameToSlot(NewSave, SlotName, 0);

	ActiveSave = NewSave;
	ActiveSlotName = SlotName;
	return NewSave;
}

bool UECGameInstance::LoadCareer(const FString& SlotName)
{
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		return false;
	}

	if (UECSaveGame* Loaded = Cast<UECSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
	{
		ActiveSave = Loaded;
		ActiveSlotName = SlotName;
		return true;
	}
	return false;
}

bool UECGameInstance::SaveCareer() const
{
	if (!ActiveSave || ActiveSlotName.IsEmpty())
	{
		return false;
	}
	return UGameplayStatics::SaveGameToSlot(ActiveSave, ActiveSlotName, 0);
}
