// Copyright 2019 Dulan Wettasinghe. All Rights Reserved.

#include "FPSCharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Net/UnrealNetwork.h"
#include "Math/TransformNonVectorized.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Player/FPSCharacterBase.h"

#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSCharacterMovement, Log, All);

/**
 * Character stats
 */

// Defines for build configs
#if DO_CHECK && !UE_BUILD_SHIPPING // Disable even if checks in shipping are enabled.
#define devCode( Code )		checkCode( Code )
#else
#define devCode(...)
#endif

UFPSCharacterMovementComponent::UFPSCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CurrentTransition = None;

	NavAgentProps.bCanCrouch = true;
	CrouchedHalfHeight = 60.0f;
	CrouchTime = 2.0f;

	bCanSprint = true;
	MaxSprintTime = -1.0f;
	MaxSprintSpeed = 800.0f;
	MaxWalkSpeedProne = 300.0f;
	SprintSideMultiplier = 0.1f;
	
	static ConstructorHelpers::FObjectFinder<UCurveFloat> SprintAccelerationCurveClass(TEXT("CurveFloat'/Game/Player/BP_SprintAccCurve.BP_SprintAccCurve'"));
	if (SprintAccelerationCurveClass.Object != NULL)
	{
		SprintAccelerationCurve = SprintAccelerationCurveClass.Object;
	}
}

float UFPSCharacterMovementComponent::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();
	if (IsSprinting())
		MaxSpeed = MaxSprintSpeed;

	return MaxSpeed;
}

float UFPSCharacterMovementComponent::GetMaxAcceleration() const
{
	float CurrentMaxAccel = Super::GetMaxAcceleration();
	if (IsSprinting())
	{
		float CurrentSpeed = Velocity.Size();
		float SprintMultiplier = SprintAccelerationCurve->GetFloatValue(CurrentSpeed / GetMaxSpeed());

		CurrentMaxAccel *= SprintMultiplier;
	}
	return CurrentMaxAccel;
}

void UFPSCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	//UE_LOG(LogTemp, Warning, TEXT("current state: %d bWantsToCrouch %d"), CurrentTransition.GetValue(), bWantsToCrouch);
	//Super::UpdateCharacterStateBeforeMovement(DeltaSeconds); //no need to do it here since the crouch is checked below,
	// Check for a change in crouch state. Players toggle crouch by changing bWantsToCrouch.
	const bool bIsCrouching = IsCrouching();
	const bool bIsSprinting = IsSprinting();
	const bool bPressedJump = CharacterOwner->bPressedJump;

	if (bPressedJump && (CurrentTransition != None || bIsCrouching))
	{
		CharacterOwner->bPressedJump = false;
		bWantsToCrouch = false;
		bCheckCrouch = true;
	}

	bool bIsMovingForward = IsMovingForward();
	if (bIsSprinting && (!bWantsToSprint || !IsMovingOnGround() || !bIsMovingForward || !bCanSprint || bWantsToCrouch))
	{
		FPSCharacterOwner->bIsSprinting = false;

		if (bWantsToCrouch)
			bWantsToSprint = false;
	}
	else if (bIsMovingForward && bWantsToSprint && IsMovingOnGround() && bCanSprint) //#TODO check if CanSprint()
	{
		/*Comment out these 2 lines if you want the player to be able to run while crouched and prone*/
		bWantsToCrouch = false;

		if (CurrentTransition == None && !bIsCrouching)
			FPSCharacterOwner->bIsSprinting = true;
	}

	if (CanCrouchInCurrentState() && bWantsToCrouch && !(bIsCrouching && CurrentTransition == None))
	{
		Crouch(false, DeltaSeconds);
	}
	//we want to carry on with prone if we press crouch and we can't crouch at this time i.e. we are trying to crouch from prone position
	else if (!CanCrouchInCurrentState() || bWantsToSprint ||  (!bWantsToCrouch && (bIsCrouching || CurrentTransition != None)))
	{
		UnCrouch(false, DeltaSeconds);
	}
	//#TODO CHECK FOR PRONE
}

bool UFPSCharacterMovementComponent::IsMovingForward()
{
	//float DirectionDot = FVector::DotProduct(PawnOwner->GetActorForwardVector().GetSafeNormal2D(), Acceleration.GetSafeNormal2D());
	//bool IsMovingForward = (DirectionDot > 0.2f) ? true : false;

	AController* PawnController = PawnOwner->Controller;
	if (!PawnController)
		return false;

	FRotator ControlRotation = PawnController->GetControlRotation();
	FRotator ControlRotationForward = FRotator(0.0f, ControlRotation.Yaw, 0.0f);
	
	float DirectionDot = FVector::DotProduct(ControlRotationForward.Vector().GetSafeNormal2D(), Acceleration.GetSafeNormal2D());
	//UE_LOG(LogTemp, Warning, TEXT("%f"), DirectionDot);

	return (DirectionDot < 0.7071f) ? false : true;
}


void UFPSCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	if (CharacterOwner->Role != ROLE_SimulatedProxy)
		return;
	
	if (bCheckCrouch)
	{
		if (CharacterOwner->bIsCrouched)
		{
			Crouch(true, DeltaSeconds);
		}
		else
		{
			UnCrouch(true, DeltaSeconds);
		}
	}
}

FSavedMovePtr FNetworkPredictionData_Client_Character_FPS::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Character_FPS());
}

class FNetworkPredictionData_Client* UFPSCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UFPSCharacterMovementComponent* MutableThis = const_cast<UFPSCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Character_FPS(*this);
	}

	return ClientPredictionData;
}

void UFPSCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags&FSavedMove_Character::FLAG_Custom_0) != 0;
}

bool UFPSCharacterMovementComponent::IsSprinting() const
{
	return FPSCharacterOwner && FPSCharacterOwner->bIsSprinting;
}

void UFPSCharacterMovementComponent::Crouch(bool bClientSimulation /*= false*/, float DeltaTime /*= 0.0f*/)
{
	if (!HasValidData())
	{
		return;
	}

	if (!bClientSimulation && !CanCrouchInCurrentState())
	{
		return;
	}

	/*So we can force the player the crouch*/
	if (!bCheckCrouch && bClientSimulation)
	{
		return;
	}

	// restore collision size before crouching
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	if (bClientSimulation && CharacterOwner->Role == ROLE_SimulatedProxy)
	{
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		bShrinkProxyCapsule = true;
	}

	if (!bClientSimulation)
	{
		CharacterOwner->bIsCrouched = true;
	}

	//The radius before we change anything
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	float DefaultStandingHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

	//Interp speed, the default interpSpeed is the same, so if coming out of a prone to crouch might be quicker since the change in height is different
	float InterpSpeed = (DefaultStandingHalfHeight - CrouchedHalfHeight) / CrouchTime;

	/*If we are already going from standing to crouch then keep it the same, or change to it if we are we standing back up and decide to crouch*/
	if (CurrentTransition == Stand_to_Crouch || CurrentTransition == Crouch_to_Stand || (IsCrouching() && CurrentTransition == None))
	{
		InterpSpeed = (DefaultStandingHalfHeight - CrouchedHalfHeight) / CrouchTime;
		CurrentTransition = Stand_to_Crouch;
	}

	float ClampedCharacterHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, FMath::FInterpConstantTo(InternalCapsuleHeight, CrouchedHalfHeight, DeltaTime, InterpSpeed));
	InternalCapsuleHeight = ClampedCharacterHalfHeight;

	if (CurrentTransition == Stand_to_Crouch)
	{
		float NormalisedAlpha = (DefaultStandingHalfHeight - InternalCapsuleHeight) / (DefaultStandingHalfHeight - CrouchedHalfHeight);
		FPSCharacterOwner->BaseEyeHeight = FMath::Lerp(FPSCharacterOwner->DefaultEyeHeight, FPSCharacterOwner->CrouchedEyeHeight, NormalisedAlpha);
	}


	//Shrink the capsule if we are fully crouched
	if (ClampedCharacterHalfHeight != CrouchedHalfHeight)
	{
		FPSCharacterOwner->RecalculateBaseEyeHeight();
		//FPSCharacterOwner->CapsuleAdjusted(0.f, 0.f);
	}
	else
	{
		ShrinkCapsule(CrouchedHalfHeight, bClientSimulation);
		CurrentTransition = EMovementTransition::None;
		bCheckCrouch = false;
	}
}

void UFPSCharacterMovementComponent::UnCrouch(bool bClientSimulation /*= false*/, float DeltaTime /*= 0.0f*/)
{
	if (!HasValidData())
	{
		return;
	}

	/*This might be called when !CanCrouchInCurrentState(), which wouldn't set bCheckCrouch*/
	if (!bCheckCrouch && bClientSimulation)
	{
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	if (!bClientSimulation && CharacterOwner->bIsCrouched)
	{	
		// See if collision is already at desired size.
		float DefaultStandingHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

		if (ExpandCapsule(DefaultStandingHalfHeight, bClientSimulation))
		{
			CharacterOwner->bIsCrouched = false;
		}
		else
		{
			//#TODO broadcast uncrouch blocked??
#if !(UE_BUILD_SHIPPING)
			UE_LOG(LogFPSCharacterMovement, Warning, TEXT("UnCrouch Blocked!!"));
#endif // !(UE_BUILD_SHIPPING)
			return;
		}
	}
	else if (bClientSimulation && !CharacterOwner->bIsCrouched)
	{
		// See if collision is already at desired size.
		float DefaultStandingHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

		ExpandCapsule(DefaultStandingHalfHeight, bClientSimulation);
	}

	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	float DefaultStandingHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	
	//Interp speed, the default interpSpeed is the same, so if coming out of a prone to crouch might be quicker since the change in height is different
	float InterpSpeed = (DefaultStandingHalfHeight - CrouchedHalfHeight) / CrouchTime;

	if (CurrentTransition == Stand_to_Crouch || CurrentTransition == Crouch_to_Stand || (!IsCrouching() && CurrentTransition == None))
	{
		InterpSpeed = (DefaultStandingHalfHeight - CrouchedHalfHeight) / CrouchTime;
		CurrentTransition = Crouch_to_Stand;
	}


	float ClampedCharacterHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, FMath::FInterpConstantTo(InternalCapsuleHeight, DefaultStandingHalfHeight, DeltaTime, InterpSpeed));
	InternalCapsuleHeight = ClampedCharacterHalfHeight;

	if (CurrentTransition == Crouch_to_Stand)
	{
		float NormalisedAlpha = (InternalCapsuleHeight - CrouchedHalfHeight) / (DefaultStandingHalfHeight - CrouchedHalfHeight);
		FPSCharacterOwner->BaseEyeHeight = FMath::Lerp(FPSCharacterOwner->CrouchedEyeHeight, FPSCharacterOwner->DefaultEyeHeight, NormalisedAlpha);
	}


	//Shrink the capsule if we are fully crouched
	if (ClampedCharacterHalfHeight != DefaultStandingHalfHeight)
	{
		FPSCharacterOwner->RecalculateBaseEyeHeight();
		//FPSCharacterOwner->CapsuleAdjusted(0.f, 0.f);
	}
	else
	{
		CurrentTransition = EMovementTransition::None;
		bCheckCrouch = false;
	}
}

bool UFPSCharacterMovementComponent::ShrinkCapsule(float NewUnscaledHalfHeight, bool bClientSimulation)
{
	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();

	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, NewUnscaledHalfHeight);
	float HalfHeightAdjust = (OldUnscaledHalfHeight - NewUnscaledHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	//UE_LOG(LogTemp, Warning, TEXT("Shrink Capsule Size %f"), CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());

	if (!bClientSimulation)
	{
		// Crouching to a larger height? return false, use ExpandCapsule for it.
		if (NewUnscaledHalfHeight > OldUnscaledHalfHeight)
		{

#if !(UE_BUILD_SHIPPING)
			UE_LOG(LogFPSCharacterMovement, Error, TEXT("Trying to Expand Capsule with ShrinkCapsule() Method, Use ExpandCapsule() instead"));
#endif // !(UE_BUILD_SHIPPING)

			FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);
			const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() - FVector(0.f, 0.f, ScaledHalfHeightAdjust), FQuat::Identity,
				UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

			// If encroached, cancel
			if (bEncroached)
			{
				CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
				return false;
			}
		}

		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
			UpdatedComponent->MoveComponent(FVector(0.f, 0.f, -ScaledHalfHeightAdjust), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}
	}

	bForceNextFloorCheck = true;

	// CapsuleAdjusted takes the change from the Default size, not the current one (though they are usually the same).
	const float MeshAdjust = ScaledHalfHeightAdjust;
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	HalfHeightAdjust = (DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - NewUnscaledHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	AdjustProxyCapsuleSize();
	FPSCharacterOwner->CapsuleAdjusted(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if (bClientSimulation && CharacterOwner->Role == ROLE_SimulatedProxy)
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData && ClientData->MeshTranslationOffset.Z != 0.f)
		{
			ClientData->MeshTranslationOffset -= FVector(0.f, 0.f, MeshAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}

	return true;
}

bool UFPSCharacterMovementComponent::ExpandCapsule(float NewUnscaledHalfHeight, bool bClientSimulation)
{
	const float CurrentHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();

	/*calculate the amount to increase by*/
	const float HalfHeightAdjust = NewUnscaledHalfHeight - OldUnscaledHalfHeight;

	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	// Grow to uncrouched size.
	check(CharacterOwner->GetCapsuleComponent());

	if (!bClientSimulation)
	{
		if (NewUnscaledHalfHeight < OldUnscaledHalfHeight)
		{
#if !(UE_BUILD_SHIPPING)
			UE_LOG(LogFPSCharacterMovement, Error, TEXT("Trying to Shrink Capsule with ExpandCapsule() Method, Use ShrinkCapsule() instead"));
#endif // !(UE_BUILD_SHIPPING)
		}

		// Try to stay in place and see if the larger capsule fits. We use a slightly taller capsule to avoid penetration.
		const UWorld* MyWorld = GetWorld();
		const float SweepInflation = KINDA_SMALL_NUMBER * 10.f;
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and standing size
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by negative amount, so actually grow it.
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
		bool bEncroached = true;

		if (!bCrouchMaintainsBaseLocation)
		{
			// Expand in place
			bEncroached = MyWorld->OverlapBlockingTestByChannel(PawnLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				// Try adjusting capsule position to see if we can avoid encroachment.
				if (ScaledHalfHeightAdjust > 0.f)
				{
					// Shrink to a short capsule, sweep down to base to find where that would hit something, and then try to stand up from there.
					float PawnRadius, PawnHalfHeight;
					CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
					const float ShrinkHalfHeight = PawnHalfHeight - PawnRadius;
					const float TraceDist = PawnHalfHeight - ShrinkHalfHeight;
					const FVector Down = FVector(0.f, 0.f, -TraceDist);

					FHitResult Hit(1.f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					const bool bBlockingHit = MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + Down, FQuat::Identity, CollisionChannel, ShortCapsuleShape, CapsuleParams);
					if (Hit.bStartPenetrating)
					{
						bEncroached = true;
					}
					else
					{
						// Compute where the base of the sweep ended up, and see if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector NewLoc = FVector(PawnLocation.X, PawnLocation.Y, PawnLocation.Z - DistanceToBase + StandingCapsuleShape.Capsule.HalfHeight + SweepInflation + MIN_FLOOR_DIST / 2.f);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						}
					}
				}
			}
		}
		else
		{
			// Expand while keeping base location the same.
			FVector StandingLocation = PawnLocation + FVector(0.f, 0.f, StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentHalfHeight);
			bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				if (IsMovingOnGround())
				{
					// Something might be just barely overhead, try moving down closer to the floor to avoid it.
					const float MinFloorDist = KINDA_SMALL_NUMBER * 10.f;
					if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
					{
						StandingLocation.Z -= CurrentFloor.FloorDist - MinFloorDist;
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
					}
				}
			}

			if (!bEncroached)
			{
				// Commit the change in location.
				UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
				bForceNextFloorCheck = true;
			}
		}

		// If still encroached then abort.
		if (bEncroached)
		{
			return false;
		}
	}
	else
	{
		bShrinkProxyCapsule = true;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), NewUnscaledHalfHeight, true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	FPSCharacterOwner->CapsuleAdjusted(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if (bClientSimulation && CharacterOwner->Role == ROLE_SimulatedProxy)
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData && ClientData->MeshTranslationOffset.Z != 0.f)
		{
			ClientData->MeshTranslationOffset += FVector(0.f, 0.f, MeshAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}

	return true;
}

void UFPSCharacterMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	Super::SetUpdatedComponent(NewUpdatedComponent);

	if (CharacterOwner)
	{
		FPSCharacterOwner = Cast<AFPSCharacterBase>(CharacterOwner);
	}

	UCapsuleComponent* UpdatedCapsule = Cast<UCapsuleComponent>(NewUpdatedComponent);
	{
		InternalCapsuleHeight = UpdatedCapsule->GetUnscaledCapsuleHalfHeight();
	}
}

void UFPSCharacterMovementComponent::PostLoad()
{
	Super::PostLoad();

	if (CharacterOwner)
	{
		FPSCharacterOwner = Cast<AFPSCharacterBase>(CharacterOwner);
	}
}

void FSavedMove_Character_FPS::Clear()
{
	Super::Clear();
	bSavedWantsToSprint = false;
	SavedCapsuleHeight = 0.0f;
	SavedTransition = None;
}

uint8 FSavedMove_Character_FPS::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint)
	{
		Result |= FLAG_Custom_0;
	}

	return Result;
}

bool FSavedMove_Character_FPS::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (bSavedWantsToSprint != ((FSavedMove_Character_FPS*)NewMove.Get())->bSavedWantsToSprint)
		return false;

	if (SavedCapsuleHeight != ((FSavedMove_Character_FPS*)NewMove.Get())->SavedCapsuleHeight)
		return false;

	if (SavedTransition != ((FSavedMove_Character_FPS*)NewMove.Get())->SavedTransition)
		return false;

	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FSavedMove_Character_FPS::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character & ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	UFPSCharacterMovementComponent* FPSMov = Cast<UFPSCharacterMovementComponent>(Character->GetCharacterMovement());
	if (FPSMov)
	{
		bSavedWantsToSprint = FPSMov->bWantsToSprint;
		SavedTransition = FPSMov->CurrentTransition;
		SavedCapsuleHeight = FPSMov->InternalCapsuleHeight;
	}
}

void FSavedMove_Character_FPS::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	UFPSCharacterMovementComponent* FPSMov = Cast<UFPSCharacterMovementComponent>(Character->GetCharacterMovement());
	if (FPSMov)
	{
		FPSMov->bWantsToSprint = bSavedWantsToSprint;
		FPSMov->CurrentTransition = SavedTransition;
		FPSMov->InternalCapsuleHeight = SavedCapsuleHeight;
	}
}























