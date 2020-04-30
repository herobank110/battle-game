// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BattleGameCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"

//////////////////////////////////////////////////////////////////////////
// ABattleGameCharacter

ABattleGameCharacter::ABattleGameCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)
}

//////////////////////////////////////////////////////////////////////////
// Input

void ABattleGameCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &ABattleGameCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ABattleGameCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ABattleGameCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ABattleGameCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ABattleGameCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &ABattleGameCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ABattleGameCharacter::OnResetVR);

	// Bind additional BattleGame input mappings.
	PlayerInputComponent->BindAction("Attack", IE_Pressed, this, &ABattleGameCharacter::Local_Attack);
}

void ABattleGameCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Doesn't need to replicate since maxhealth is known at build time and is guaranteed to be the same.
	Health = MaxHealth;
}

void ABattleGameCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABattleGameCharacter, Health);
}

float ABattleGameCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Get damage amount from parent (usually returns the same value as DamageAmount)
	const float DamageToApply = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health -= DamageToApply;
	//GEngine->AddOnScreenDebugMessage(48273, 2.f, FColor::Yellow, FString::Printf(TEXT("Player health now %.1f"), Health));
	return DamageToApply;
}

void ABattleGameCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void ABattleGameCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void ABattleGameCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

void ABattleGameCharacter::Local_Attack()
{
	// Call the attack on the server.
	// There could be additional checks client side to see if there is anyone available to attack, but for now
	// it will just tell the server to do the checks.
	Server_Attack();
}

bool ABattleGameCharacter::TraceForOpponent(FHitResult& HitResult)
{
	// Return the center of the actor (hips).
	const auto Start = GetActorLocation();
	// Line trace forward from the actor's forward vector (not the camera's.)
	auto ForwardProjection = GetActorForwardVector();
	ForwardProjection.Z = 0.f;  // Ignore pitch information, trace straight forward.
	ForwardProjection *= 75.f;  // How far to extend the line trace, in unreal units.
	const auto End = Start + ForwardProjection;

	// Only trace for other pawns (players.)
	const FCollisionObjectQueryParams ObjectsQueryParams{ ECC_Pawn };
	const FCollisionQueryParams TraceParams{ /*InTraceTag=*/NAME_None, /*bInTraceComplex=*/false, /*InIgnoreActor=*/this };
	return GetWorld()->LineTraceSingleByObjectType(HitResult, Start, End, ObjectsQueryParams, TraceParams);
}

void ABattleGameCharacter::SeekAndApplyDamage()
{
	bool bWasHitSuccessful = false;
	FHitResult HitResult;
	if (TraceForOpponent(HitResult))
	{
		const auto OtherPlayer = Cast<ABattleGameCharacter>(HitResult.Actor);
		if (OtherPlayer)
		{
			// Successfully hit a player character. Apply damage.
			// TODO: damage from the weapon that hit them.
			OtherPlayer->TakeDamage(AttackAmount, FDamageEvent(AttackDamageClass), GetController(), this);
			// Mark it as a successful hit.
			bWasHitSuccessful = true;
		}
	}

	// Trigger the relevant multicast events for blueprints to react.
	if (bWasHitSuccessful)
		Multicast_OnAttackSuccessful(HitResult);
}

void ABattleGameCharacter::Server_Attack_Implementation()
{
	if (AttackTimer.IsValid())
		// Don't start an attack while another one is still valid.
		return;

	auto &TimerManager = GetWorld()->GetTimerManager();
	// Set a self-invalidating timer so we can't attack again during the attack phase.
	TimerManager.SetTimer(AttackTimer, [this]() {AttackTimer.Invalidate(); }, AttackCooldownDuration, /*inBLoop=*/false);

	if (ApplyAttackDamageDelay > 0.0)
	{
		// Apply the damage after a short delay to be in time with the animation.
		const float AttackDelay = ApplyAttackDamageDelay < AttackCooldownDuration ? ApplyAttackDamageDelay : AttackCooldownDuration;
		TimerManager.SetTimer(ApplyAttackDamageTimer, [this]() {SeekAndApplyDamage(); }, AttackDelay, false);
	}
	else
	{
		// The attack delay was zero or less so apply damage right away. Setting a timer with zero or less delay would never be called!
		SeekAndApplyDamage();
	}

	// Trigger the relevant multicast events for blueprints to react.
	Multicast_OnAttackAttempted();
}

void ABattleGameCharacter::Multicast_OnAttackAttempted_Implementation()
{
	OnAttackAttempted();
}

void ABattleGameCharacter::Multicast_OnAttackSuccessful_Implementation(const FHitResult& Hit)
{
	OnAttackSuccessful(Hit);
}

void ABattleGameCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ABattleGameCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void ABattleGameCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void ABattleGameCharacter::MoveRight(float Value)
{
	if ( (Controller != NULL) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}
