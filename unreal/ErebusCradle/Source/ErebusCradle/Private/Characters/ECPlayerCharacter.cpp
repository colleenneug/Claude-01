#include "Characters/ECPlayerCharacter.h"
#include "Weapons/ECWeaponBase.h"
#include "Abilities/ECFieldAbilityComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"

AECPlayerCharacter::AECPlayerCharacter()
{
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 0.f;
	CameraBoom->bUsePawnControlRotation = true;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(CameraBoom);
	FirstPersonCamera->bUsePawnControlRotation = false;

	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
}

void AECPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void AECPlayerCharacter::InitializeDoctrine(const FECDoctrineDefinition& InDoctrine)
{
	CurrentDoctrine = InDoctrine;
	Attributes = InDoctrine.BaseAttributes;

	InitializeVitals(InDoctrine.BaseHealth, InDoctrine.BaseEnergy);
	DamageMitigation = InDoctrine.PassiveDamageMitigation;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed * InDoctrine.MoveSpeedMultiplier;

	if (EquippedWeapon)
	{
		EquippedWeapon->Destroy();
		EquippedWeapon = nullptr;
	}
	if (InDoctrine.WeaponClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		EquippedWeapon = GetWorld()->SpawnActor<AECWeaponBase>(InDoctrine.WeaponClass, GetActorTransform(), SpawnParams);
		if (EquippedWeapon)
		{
			EquippedWeapon->Stats = InDoctrine.Weapon;
			EquippedWeapon->AttachToComponent(FirstPersonCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			EquippedWeapon->EquipTo(this);
		}
	}

	if (FieldAbility)
	{
		FieldAbility->DestroyComponent();
		FieldAbility = nullptr;
	}
	if (InDoctrine.FieldAbilityClass)
	{
		FieldAbility = NewObject<UECFieldAbilityComponent>(this, InDoctrine.FieldAbilityClass);
		FieldAbility->RegisterComponent();
	}
}

void AECPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	if (MoveAction) EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AECPlayerCharacter::Move);
	if (LookAction) EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AECPlayerCharacter::Look);
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
	if (SprintAction)
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AECPlayerCharacter::StartSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AECPlayerCharacter::StopSprint);
	}
	if (CrouchAction)
	{
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Started, this, &AECPlayerCharacter::StartCrouchHeld);
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AECPlayerCharacter::StopCrouchHeld);
	}
	if (FireAction)
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AECPlayerCharacter::StartFire);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &AECPlayerCharacter::StopFire);
	}
	if (AimAction)
	{
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Started, this, &AECPlayerCharacter::StartAim);
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Completed, this, &AECPlayerCharacter::StopAim);
	}
	if (ReloadAction) EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this, &AECPlayerCharacter::DoReload);
	if (FieldAbilityAction) EnhancedInput->BindAction(FieldAbilityAction, ETriggerEvent::Started, this, &AECPlayerCharacter::DoFieldAbility);
}

void AECPlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (!Controller || MoveInput.IsNearlyZero())
	{
		return;
	}

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MoveInput.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MoveInput.X);
}

void AECPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void AECPlayerCharacter::StartSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed * CurrentDoctrine.MoveSpeedMultiplier * SprintSpeedMultiplier;
}

void AECPlayerCharacter::StopSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed * CurrentDoctrine.MoveSpeedMultiplier;
}

void AECPlayerCharacter::StartCrouchHeld()
{
	Crouch();
}

void AECPlayerCharacter::StopCrouchHeld()
{
	UnCrouch();
}

void AECPlayerCharacter::StartFire()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->StartFire();
	}
}

void AECPlayerCharacter::StopFire()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->StopFire();
	}
}

void AECPlayerCharacter::StartAim()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->SetAimingDownSights(true);
	}
}

void AECPlayerCharacter::StopAim()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->SetAimingDownSights(false);
	}
}

void AECPlayerCharacter::DoReload()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->Reload();
	}
}

void AECPlayerCharacter::DoFieldAbility()
{
	if (FieldAbility)
	{
		FieldAbility->TryActivate();
	}
}
