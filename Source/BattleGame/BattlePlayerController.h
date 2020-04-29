// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BattlePlayerController.generated.h"

/**
 * 
 */
UCLASS()
class BATTLEGAME_API ABattlePlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	// Sets up input
	void SetupInputComponent() override;

	// Called locally on input
	void OnAttackPressed();

	UFUNCTION(Server, Reliable)
	// Called on server to actually apply damage.
	void Server_ApplyDamage();
};
