// Fill out your copyright notice in the Description page of Project Settings.


#include "BattlePlayerController.h"
#include "BattleGameCharacter.h"


void ABattlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Input only called locally, so this function is local too.
	InputComponent->BindAction(TEXT("Attack"), IE_Pressed, this, &ABattlePlayerController::OnAttackPressed);
}

void ABattlePlayerController::OnAttackPressed()
{
	// Tell the server to apply damage to our pawn.
	Server_ApplyDamage();
}

void ABattlePlayerController::Server_ApplyDamage_Implementation()
{
	GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Emerald, TEXT("SHDIOASHDA"));
	// Called on server to set replicated properties that can be replicated back to clients.
	ABattleGameCharacter* PlayerCharacter = GetPawn<ABattleGameCharacter>();
	PlayerCharacter->TakeDamage(20.f, FDamageEvent(), nullptr, nullptr);
}
