#include "AI/ECHostileAIController.h"
#include "AI/ECHostileCharacter.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "Navigation/PathFollowingComponent.h"

AECHostileAIController::AECHostileAIController()
{
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
	SetPerceptionComponent(*PerceptionComponent);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 2500.f;
	SightConfig->LoseSightRadius = SightConfig->SightRadius + 400.f;
	SightConfig->PeripheralVisionAngleDegrees = 90.f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());
}

void AECHostileAIController::BeginPlay()
{
	Super::BeginPlay();
	PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &AECHostileAIController::OnTargetPerceived);
}

void AECHostileAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (AECHostileCharacter* Hostile = Cast<AECHostileCharacter>(InPawn))
	{
		SightConfig->SightRadius = Hostile->SightRadius;
		SightConfig->LoseSightRadius = Hostile->SightRadius + 400.f;
		PerceptionComponent->RequestStimuliListenerUpdate();
	}

	GetWorldTimerManager().SetTimer(ChaseTimer, this, &AECHostileAIController::TickChase, 0.2f, true);
}

void AECHostileAIController::OnTargetPerceived(AActor* Actor, FAIStimulus Stimulus)
{
	if (Stimulus.WasSuccessfullySensed())
	{
		CurrentTarget = Actor;
	}
	else if (CurrentTarget == Actor)
	{
		CurrentTarget = nullptr;
	}
}

void AECHostileAIController::TickChase()
{
	AECHostileCharacter* Hostile = Cast<AECHostileCharacter>(GetPawn());
	if (!Hostile || Hostile->IsDead() || !CurrentTarget)
	{
		return;
	}

	const float Distance = FVector::Dist(Hostile->GetActorLocation(), CurrentTarget->GetActorLocation());
	if (Distance <= Hostile->AttackRange)
	{
		StopMovement();
		Hostile->TryAttack(CurrentTarget);
	}
	else
	{
		MoveToActor(CurrentTarget, Hostile->AttackRange * 0.8f);
	}
}
