#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ECTypes.h"
#include "ECGameInstance.generated.h"

class UECSaveGame;

/** Owns the doctrine table (the class system's actual data) and the currently
 *  active career save, so both persist across level loads the way the
 *  browser build's classes.js + storage.js survive a scene change. Populate
 *  WeaponClass / FieldAbilityClass on each doctrine's entry from a Blueprint
 *  child of this class once you have concrete weapon/ability Blueprints. */
UCLASS(Blueprintable)
class EREBUSCRADLE_API UECGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Doctrine")
	FECDoctrineDefinition GetDoctrineDefinition(EECDoctrine Doctrine) const;

	/** The doctrine definition for whichever operative is currently active,
	 *  built from the doctrine table plus that operative's save-slot level. */
	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Doctrine")
	FECDoctrineDefinition GetActiveDoctrineDefinition() const;

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Save")
	UECSaveGame* CreateCharacter(const FString& CharacterName, EECDoctrine Doctrine, const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Save")
	bool LoadCareer(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Save")
	bool SaveCareer() const;

	UFUNCTION(BlueprintCallable, Category = "Erebus Cradle|Save")
	UECSaveGame* GetActiveSave() const { return ActiveSave; }

protected:
	/** Editable in a Blueprint child so designers can retune doctrines without
	 *  a C++ recompile; defaults set in Init() match classes.js exactly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Erebus Cradle|Doctrine")
	TMap<EECDoctrine, FECDoctrineDefinition> DoctrineTable;

	UPROPERTY(Transient)
	TObjectPtr<UECSaveGame> ActiveSave;

	UPROPERTY(Transient)
	FString ActiveSlotName;

	void BuildDefaultDoctrineTable();
};
