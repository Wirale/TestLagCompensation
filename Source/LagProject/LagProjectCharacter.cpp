#include "LagProjectCharacter.h"

#include "EngineUtils.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "GameFramework/GameStateBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

int32 CVarLagCompensationDrawDebugValue = 0;
static FAutoConsoleVariableRef CVarSomeCoolDebugValue(TEXT("LagCompensation.DrawDebug"), CVarLagCompensationDrawDebugValue, *FString("Draw sphere and line trace lag compensation"));

FSavedPosition::FSavedPosition()
{
	this->Time = 0.f;
	this->SavePosition = FVector::ZeroVector;
}

FSavedPosition::FSavedPosition(float Time, const FVector& SavedPosition)
{
	this->Time = Time;
	this->SavePosition = SavedPosition;
}

ALagProjectCharacter::ALagProjectCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(false);	
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	
	SavedPositions = {};
	ShotRange = 1000.f;
}

void ALagProjectCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));
}

void ALagProjectCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);

	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &ALagProjectCharacter::OnFire);
	
	PlayerInputComponent->BindAxis("MoveForward", this, &ALagProjectCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ALagProjectCharacter::MoveRight);
	
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ALagProjectCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ALagProjectCharacter::LookUpAtRate);
}

void ALagProjectCharacter::OnFire()
{
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}

	if (IsLocallyControlled())
	{
		const UWorld* World = GetWorld();
		if (IsValid(World))
		{
			const AGameStateBase* GameState = UGameplayStatics::GetGameState(this);
			if (IsValid(GameState))
			{
				const float ShotTime = GameState->GetServerWorldTimeSeconds();
				Server_CalculateFire(ShotTime, FP_MuzzleLocation->GetForwardVector(), FP_MuzzleLocation->GetComponentLocation());
			}
		}
	}
}

void ALagProjectCharacter::Server_CalculateFire_Implementation(float Time, const FVector_NetQuantize100& ShotDirection, const FVector_NetQuantize& StartLocation)
{
	UWorld* World = GetWorld();
	
	for (TActorIterator<ALagProjectCharacter> PawnItr(World); PawnItr; ++PawnItr)
	{
		if (*PawnItr == this && IsValid(*PawnItr))
		{
			continue;
		}
		
		if (Time > 0.f)
		{
			FVector TargetLocation = PawnItr->GetActorLocation();
			
			for (int32 i = PawnItr->SavedPositions.Num() - 1; i >= 0; --i)
			{				
				TargetLocation = PawnItr->SavedPositions[i].SavePosition;
				if (PawnItr->SavedPositions[i].Time < Time)
				{					
					break;
				}
			}
			
			const FVector& EndLocation = StartLocation + ShotDirection * ShotRange;

			const float CollisionHeight = PawnItr->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float CollisionRadius = PawnItr->GetCapsuleComponent()->GetScaledCapsuleRadius();
			
			FVector ClosestPoint(0.f);
			FVector ClosestCapsulePoint = TargetLocation;
			
			FVector CapsuleSegment = FVector(0.f, 0.f, CollisionHeight - CollisionRadius);
			FMath::SegmentDistToSegmentSafe(StartLocation, EndLocation, TargetLocation - CapsuleSegment, TargetLocation + CapsuleSegment, ClosestPoint, ClosestCapsulePoint);

			const bool bHitTarget = (ClosestPoint - ClosestCapsulePoint).SizeSquared() < FMath::Square(CollisionRadius);
			
			Multicast_DrawDebugSphere(PawnItr->GetActorLocation(), TargetLocation, bHitTarget, StartLocation, ClosestPoint);
		}
	}
}

void ALagProjectCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AddMovementInput(GetActorForwardVector(), 0.5);
	
	if (HasAuthority())
	{
		const UWorld* World = GetWorld();
		if (IsValid(World))
		{
			const FSavedPosition SavedPosition(World->GetTimeSeconds(), GetActorLocation());
			SavedPositions.Add(SavedPosition);
		}
	}
}

void ALagProjectCharacter::Multicast_DrawDebugSphere_Implementation(const FVector_NetQuantize& LocationNow, const FVector_NetQuantize& RestoredLocation, bool bHitTarget, const FVector_NetQuantize100& StartHit, const FVector_NetQuantize100& HitPoint)
{
	if (CVarLagCompensationDrawDebugValue)
	{
		UKismetSystemLibrary::DrawDebugCapsule(this, RestoredLocation, GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), GetCapsuleComponent()->GetScaledCapsuleRadius(), FRotator::ZeroRotator, bHitTarget ? FLinearColor::Green : FLinearColor::Red, 10, 2);
		UKismetSystemLibrary::DrawDebugCapsule(this, LocationNow, GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), GetCapsuleComponent()->GetScaledCapsuleRadius(), FRotator::ZeroRotator, FLinearColor::Blue, 10, 2);
		UKismetSystemLibrary::DrawDebugLine(this, StartHit, HitPoint, bHitTarget ? FLinearColor::Green : FLinearColor::Red, 10, 2);
	}
}

void ALagProjectCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void ALagProjectCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void ALagProjectCharacter::TurnAtRate(float Rate)
{
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ALagProjectCharacter::LookUpAtRate(float Rate)
{
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}