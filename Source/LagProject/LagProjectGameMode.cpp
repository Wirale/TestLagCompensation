// Copyright Epic Games, Inc. All Rights Reserved.

#include "LagProjectGameMode.h"
#include "LagProjectHUD.h"
#include "LagProjectCharacter.h"
#include "UObject/ConstructorHelpers.h"

ALagProjectGameMode::ALagProjectGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPersonCPP/Blueprints/FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// use our custom HUD class
	HUDClass = ALagProjectHUD::StaticClass();
}
