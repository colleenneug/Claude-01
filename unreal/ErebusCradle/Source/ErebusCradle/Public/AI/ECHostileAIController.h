#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ECHostileAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;

/** Idle -> chase -> attack, driven entirely from C++ via AIPerception (sight)
 *  rather than a Behavior Tree asset, so this controller needs no Content/
 *  data to function — see unreal/README.md for why that trade was made. */
UCLASS()
class EREBUSCRADLE_API AECHostileAIController : public AAIController
{
	GENERATED_BODY()

public:
	AECHostileAIController();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION()
	void OnTargetPerceived(AActor* Actor, FAIStimulus Stimulus);

	void TickChase();

private:
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UAIPerceptionComponent> PerceptionComponent;

	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CurrentTarget;

	FTimerHandle ChaseTimer;
};
