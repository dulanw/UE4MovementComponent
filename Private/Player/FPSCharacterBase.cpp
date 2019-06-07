// Copyright 2019 Dulan Wettasinghe. All Rights Reserved.

#include "FPSCharacterBase.h"
#include "FPSGame.h"
#include "FPSCharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Utility/FPSHitBoxesManager.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSCharacter, Log, All);

// Sets default values
AFPSCharacterBase::AFPSCharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UFPSCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	BaseEyeHeight = 64.0f;
	CrouchedEyeHeight = 50.0f;

	/*use bUseControllerDesiredRotation in movement component instead*/
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	if (CameraComponent)
	{
		CameraComponent->SetupAttachment(RootComponent);
		CameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight));
		CameraComponent->bUsePawnControlRotation = true;
	}

	/*Net Settings*/
	//NetUpdateFrequency = 128.0f;
	//MinNetUpdateFrequency = 32.0f;
}

void AFPSCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AFPSCharacterBase, bIsSprinting, COND_SimulatedOnly);
}

void AFPSCharacterBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	/*Set the default values for the characterhalfheight and eye height*/
	if (CameraComponent)
	{
		BaseEyeHeight = CameraComponent->RelativeLocation.Z;
		DefaultEyeHeight = BaseEyeHeight;
/*
		UE_LOG(LogTemp, Warning, TEXT("PostInitializeComponents BaseEyeHeight = %f"), BaseEyeHeight);
		UE_LOG(LogTemp, Warning, TEXT(" PostInitializeComponents Camera Z = %f"), CameraComponent->RelativeLocation.Z);
		UE_LOG(LogTemp, Warning, TEXT(" PostInitializeComponents CurrentCharacterHalfHeight Z = %f"), CurrentCharacterHalfHeight);*/
	}
}


// Called when the game starts or when spawned
void AFPSCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	//HitBoxManager = NewObject<UFPSHitBoxesManager>(this, TEXT("HitBoxManager"));

	//if (HitBoxManager)
	//	HitBoxManager->RegisterComponent();
/*
	UE_LOG(LogTemp, Warning, TEXT("BaseEyeHeight = %f"), BaseEyeHeight);
	UE_LOG(LogTemp, Warning, TEXT("Camera Z = %f"), CameraComponent->RelativeLocation.Z);
	UE_LOG(LogTemp, Warning, TEXT(" CurrentCharacterHalfHeight Z = %f"), CurrentCharacterHalfHeight);*/
}

void AFPSCharacterBase::OnRep_IsCrouched()
{
	UFPSCharacterMovementComponent* MovementComponent = Cast<UFPSCharacterMovementComponent>(GetCharacterMovement());
	if (MovementComponent)
	{
		if (bIsCrouched)
		{
			MovementComponent->bWantsToCrouch = true;
		}
		else
		{
			MovementComponent->bWantsToCrouch = false;
		}
		MovementComponent->bCheckCrouch = true;
		MovementComponent->bNetworkUpdateReceived = true;
	}
}

void AFPSCharacterBase::OnRep_IsSprinting()
{

}

// Called every frame
void AFPSCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void AFPSCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAction("Sprint", IE_Pressed, this, &AFPSCharacterBase::StartSprint);
	PlayerInputComponent->BindAction("Sprint", IE_Released, this, &AFPSCharacterBase::StopSprint);

	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AFPSCharacterBase::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &AFPSCharacterBase::StopJumping);

	PlayerInputComponent->BindAction("Crouch", IE_Pressed, this, &AFPSCharacterBase::ToggleCrouch);
	
	PlayerInputComponent->BindAxis("MoveForward", this, &AFPSCharacterBase::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AFPSCharacterBase::MoveRight);
	PlayerInputComponent->BindAxis("LookRight", this, &AFPSCharacterBase::AddControllerYawInput);
	PlayerInputComponent->BindAxis("LookDown", this, &AFPSCharacterBase::AddControllerPitchInput);
}

void AFPSCharacterBase::MoveForward(float AxisValue)
{
	FRotator ControlRotation = Controller->GetControlRotation();
	FRotator ControlRotationForward = FRotator(0.0f, ControlRotation.Yaw, 0.0f);

	AxisValue = FMath::Clamp(AxisValue, -1.0f, 1.0f);
	AddMovementInput(ControlRotationForward.Vector(), AxisValue);

	//FRotator ControlRotation = Controller->GetControlRotation();
	//// Limit pitch when walking or falling
	//if (GetCharacterMovement()->IsMovingOnGround() || GetCharacterMovement()->IsFalling())
	//{
	//	ControlRotation.Pitch = 0.0f;
	//}
	//// add movement in that direction
	//const FVector Direction = FRotationMatrix(ControlRotation).GetScaledAxis(EAxis::X);
	//AxisValue = FMath::Clamp(AxisValue, -1.0f, 1.0f);
	//AddMovementInput(Direction, AxisValue);
}

void AFPSCharacterBase::MoveRight(float AxisValue)
{
	FRotator ControlRotation = Controller->GetControlRotation();
	FRotator ControlRotationForward = FRotator(0.0f, ControlRotation.Yaw, 0.0f);
	FVector Direction = FRotationMatrix(ControlRotationForward).GetScaledAxis(EAxis::Y);
	AxisValue = FMath::Clamp(AxisValue, -1.0f, 1.0f);
	AddMovementInput(Direction, AxisValue);
}

void AFPSCharacterBase::StartSprint()
{
	UFPSCharacterMovementComponent* MovementComponent = Cast<UFPSCharacterMovementComponent>(GetCharacterMovement());
	if (MovementComponent)
		MovementComponent->bWantsToSprint = true;
}

void AFPSCharacterBase::StopSprint()
{
	UFPSCharacterMovementComponent* MovementComponent = Cast<UFPSCharacterMovementComponent>(GetCharacterMovement());
	if (MovementComponent)
		MovementComponent->bWantsToSprint = false;
}

void AFPSCharacterBase::RecalculateBaseEyeHeight()
{
	UFPSCharacterMovementComponent* MovementComponent = Cast<UFPSCharacterMovementComponent>(GetCharacterMovement());
	if (!MovementComponent || !CameraComponent)
	{
		return;
	}

	/*Need to move it a bit further down because the actual capsule and the character height will be different,
	 *so need to adjust the height when setting the relative location
	 */
	const float ComponentScale = GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	float HalfHeightAdjust = (OldUnscaledHalfHeight - MovementComponent->InternalCapsuleHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	//UE_LOG(LogTemp, Warning, TEXT("Base Eye Height = %f, Adjusted = %f"), BaseEyeHeight, BaseEyeHeight - ScaledHalfHeightAdjust);
	float NewRelativeLoc = MovementComponent->bCrouchMaintainsBaseLocation ? (BaseEyeHeight - ScaledHalfHeightAdjust) : BaseEyeHeight;
	CameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, NewRelativeLoc));
}

void AFPSCharacterBase::CapsuleAdjusted(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	RecalculateBaseEyeHeight();

	const ACharacter* DefaultChar = GetDefault<ACharacter>(GetClass());
	if (GetMesh() && DefaultChar->GetMesh())
	{
		GetMesh()->RelativeLocation.Z = DefaultChar->GetMesh()->RelativeLocation.Z + HalfHeightAdjust;
		BaseTranslationOffset.Z = GetMesh()->RelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->GetBaseTranslationOffset().Z + HalfHeightAdjust;
	}
}

bool AFPSCharacterBase::CanCrouch()
{
	return GetCharacterMovement() && GetCharacterMovement()->CanEverCrouch() && GetRootComponent() && !GetRootComponent()->IsSimulatingPhysics();
}

void AFPSCharacterBase::ToggleCrouch()
{
	if (GetCharacterMovement()->bWantsToCrouch)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void AFPSCharacterBase::Crouch(bool bClientSimulation /*= false*/)
{
	UFPSCharacterMovementComponent* MovementComponent = Cast<UFPSCharacterMovementComponent>(GetCharacterMovement());
	if (MovementComponent)
	{
		if (CanCrouch())
		{
			MovementComponent->bWantsToCrouch = true;
			//MovementComponent->bCheckCrouch = true;
		}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		else if (!MovementComponent->CanEverCrouch())
		{
			UE_LOG(LogFPSCharacter, Log, TEXT("%s is trying to crouch, but crouching is disabled on this character! (check CharacterMovement NavAgentSettings)"), *GetName());
		}
#endif
	}
}

void AFPSCharacterBase::UnCrouch(bool bClientSimulation /*= false*/)
{
	UFPSCharacterMovementComponent* MovementComponent = Cast<UFPSCharacterMovementComponent>(GetCharacterMovement());
	if (MovementComponent)
	{
		MovementComponent->bWantsToCrouch = false;
		//MovementComponent->bCheckCrouch = true;
	}
}
