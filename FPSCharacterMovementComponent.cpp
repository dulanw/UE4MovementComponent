// Fill out your copyright notice in the Description page of Project Settings.

#include "FPSCharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Net/UnrealNetwork.h"
#include "Math/TransformNonVectorized.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSCharacterMovement, Log, All);

/**
 * Character stats
 */
DECLARE_CYCLE_STAT(TEXT("Char PhysProne"), STAT_CharPhysProne, STATGROUP_Character);

// Defines for build configs
#if DO_CHECK && !UE_BUILD_SHIPPING // Disable even if checks in shipping are enabled.
#define devCode( Code )		checkCode( Code )
#else
#define devCode(...)
#endif

UFPSCharacterMovementComponent::UFPSCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CurrentMovementChange = EMovementChange::MOVE_CHANGE_NONE;

	NavAgentProps.bCanCrouch = true;
	CrouchedHalfHeight = 50.0f;
	CurrentCrouchAlpha = 0.0f;
	CrouchTime = 2.0f;

	bCanSprint = true;
	MaxSprintTime = -1;
	MaxWalkSpeedSprint = 800.0f;
	SprintSideMovementMultiplier = 0.1f;
	
	static ConstructorHelpers::FObjectFinder<UCurveFloat> SprintAccelerationCurveClass(TEXT("CurveFloat'/Game/Player/BP_SprintAccCurve.BP_SprintAccCurve'"));
	if (SprintAccelerationCurveClass.Object != NULL)
	{
		SprintAccelerationCurve = SprintAccelerationCurveClass.Object;
	}

	bAutoRegisterProneUpdatedComponent = true;
	bCanEverProne = false;

	/*need this so we can check if the character collide during rotation when prone, override PhysicsRotation(float DeltaTime)*/
	bUseControllerDesiredRotation = true;

	SetIsReplicated(true);
}

void UFPSCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (bAutoRegisterProneUpdatedComponent && bCanEverProne)
	{
		UCapsuleComponent* UpdatedCapsule = Cast<UCapsuleComponent>(UpdatedComponent);
		if (UpdatedCapsule)
		{
			ProneComponent = NewObject<UCapsuleComponent>(this);
			ProneComponent->AttachTo(UpdatedComponent);
			ProneComponent->RegisterComponent();

			float UnScaledRadius = UpdatedCapsule->GetUnscaledCapsuleRadius();
			float UnScaledHeight = UpdatedCapsule->GetUnscaledCapsuleHalfHeight();

			ProneComponent->SetCapsuleRadius(UnScaledRadius);
			ProneComponent->SetCapsuleHalfHeight(UnScaledHeight);
			float ScaledRadius = UpdatedCapsule->GetScaledCapsuleRadius();
			float ScaledHeight = UpdatedCapsule->GetScaledCapsuleHalfHeight();

			FRotator RelRot(90.0f, 0.0f, 0.0f);
			ProneComponent->SetRelativeRotation(RelRot);
			ProneComponent->SetRelativeLocation(FVector(ScaledRadius - ScaledHeight, 0.0f, ScaledRadius - ScaledHeight));

			ProneComponent->SetCollisionProfileName(TEXT("BlockAll"));

			ProneComponent->bHiddenInGame = false;
			ProneComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		}
	}
}

float UFPSCharacterMovementComponent::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();
	if (bIsSprinting)
		MaxSpeed = MaxWalkSpeedSprint;

	return MaxSpeed;
}

float UFPSCharacterMovementComponent::GetMaxAcceleration() const
{
	float CurrentMaxAccel = Super::GetMaxAcceleration();
	if (bIsSprinting)
	{
		float CurrentSpeed = Velocity.Size();
		float MaxSpeed = GetMaxSpeed();
		float SprintMultiplier = SprintAccelerationCurve->GetFloatValue(CurrentSpeed / MaxSpeed);

		CurrentMaxAccel *= SprintMultiplier;
	}
	return CurrentMaxAccel;
}

void UFPSCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	//Super::UpdateCharacterStateBeforeMovement(DeltaSeconds); //no need to do it here since the crouch is checked below,

	// Check for a change in crouch state. Players toggle crouch by changing bWantsToCrouch.
	const bool bIsCrouching = IsCrouching();
	if ((!bIsCrouching && bWantsToCrouch && CanCrouchInCurrentState()) || bWantsToCrouch && CurrentMovementChange != EMovementChange::MOVE_CHANGE_NONE)
	{
		Crouch(false, DeltaSeconds);
	}
	else if ((bIsCrouching && (!bWantsToCrouch || !CanCrouchInCurrentState())) || (!bWantsToCrouch && CurrentMovementChange != EMovementChange::MOVE_CHANGE_NONE))
	{
		UnCrouch(false, DeltaSeconds);
	}

	float DirectionDot = FVector::DotProduct(PawnOwner->GetActorForwardVector().GetSafeNormal2D(), Acceleration.GetSafeNormal2D());

	bool IsMovingForward = (DirectionDot > 0.2f) ? true : false;

	if (bIsSprinting && (!bWantsToSprint || !IsMovingOnGround() || !IsMovingForward || !bCanSprint))
	{
		bIsSprinting = false;
	}
	else if (IsMovingForward && bWantsToSprint && IsMovingOnGround() && bCanSprint) //#TODO check if CanSprint()
	{
		bWantsToCrouch = false;
		if (bIsCrouching)
		{
			UnCrouch(false, DeltaSeconds);
		}

		bIsSprinting = true;

		Acceleration = PawnOwner->GetActorForwardVector().GetSafeNormal() * GetMaxAcceleration();
	}
}

void UFPSCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UFPSCharacterMovementComponent, bIsSprinting, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UFPSCharacterMovementComponent, CurrentMovementChange, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UFPSCharacterMovementComponent, CurrentCapsuleHalfHeight, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UFPSCharacterMovementComponent, bIsProne, COND_SimulatedOnly);
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
	bWantsToProne = (Flags&FSavedMove_Character::FLAG_Custom_1) != 0;
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

	// See if collision is already at desired size.
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == CrouchedHalfHeight)
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = true;
			CurrentMovementChange = EMovementChange::MOVE_CHANGE_NONE;
		}
		CharacterOwner->OnStartCrouch(0.f, 0.f);
		return;
	}

	// restore collision size before crouching
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	if (bClientSimulation && CharacterOwner->Role == ROLE_SimulatedProxy)
	{
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();

	// Height is not allowed to be smaller than radius.
	float ClampedCrouchedHalfHeight;
	if (!bClientSimulation)
	{
		float DefaultStandingHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		//reset the crouch height to whatever the value is
		//add a check to see if the player is coming up from the prone positions
		if (CurrentMovementChange == EMovementChange::MOVE_CHANGE_NONE && bWantsToCrouch && !CharacterOwner->bIsCrouched)
		{
			CurrentMovementChange = EMovementChange::STAND_TO_CROUCH;
			CurrentCrouchAlpha = 0.0f;
			CurrentCapsuleHalfHeight = DefaultStandingHalfHeight;
			bWantsToSprint = false;
		}

		CurrentCrouchAlpha = FMath::Clamp(CurrentCrouchAlpha + (DeltaTime/CrouchTime), 0.0f, 1.0f);
		ClampedCrouchedHalfHeight = FMath::Lerp(DefaultStandingHalfHeight, CrouchedHalfHeight, CurrentCrouchAlpha);
		ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, ClampedCrouchedHalfHeight);
		CurrentCapsuleHalfHeight = ClampedCrouchedHalfHeight;
	}
	else
	{
		ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, CurrentCapsuleHalfHeight);
	}


	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	float HalfHeightAdjust = (OldUnscaledHalfHeight - ClampedCrouchedHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	if (!bClientSimulation)
	{
		// Crouching to a larger height? (this is rare)
		if (ClampedCrouchedHalfHeight > OldUnscaledHalfHeight)
		{
			FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);
			const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() - FVector(0.f, 0.f, ScaledHalfHeightAdjust), FQuat::Identity,
				UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

			// If encroached, cancel
			if (bEncroached)
			{
				CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
				return;
			}
		}

		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
			UpdatedComponent->MoveComponent(FVector(0.f, 0.f, -ScaledHalfHeightAdjust), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}

		//only set bIsCrouched to true if its fully crouched
		if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == CrouchedHalfHeight)
		{
			CharacterOwner->bIsCrouched = true;
			CurrentMovementChange = EMovementChange::MOVE_CHANGE_NONE;
		}		
	}

	bForceNextFloorCheck = true;

	// OnStartCrouch takes the change from the Default size, not the current one (though they are usually the same).
	const float MeshAdjust = ScaledHalfHeightAdjust;
	HalfHeightAdjust = (DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - ClampedCrouchedHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

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
}

void UFPSCharacterMovementComponent::UnCrouch(bool bClientSimulation /*= false*/, float DeltaTime /*= 0.0f*/)
{
	if (!HasValidData())
	{
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	// See if collision is already at desired size.
	float DefaultStandingHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == DefaultStandingHalfHeight)
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = false;
			CurrentMovementChange = EMovementChange::MOVE_CHANGE_NONE;
		}
		CharacterOwner->OnEndCrouch(0.f, 0.f);
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

	/*New Code to add smooth crouch*/
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	float ClampedCrouchedHalfHeight;
	float newCrouchAlpha;
	if (!bClientSimulation)
	{
		//reset the crouch height to whatever the value is
		//add a check to see if the player is coming up from the prone positions
		if (CurrentMovementChange == EMovementChange::MOVE_CHANGE_NONE && !bWantsToCrouch && CharacterOwner->bIsCrouched)
		{
			CurrentMovementChange = EMovementChange::CROUCH_TO_STAND;
			CurrentCrouchAlpha = 1.0f;
			CurrentCapsuleHalfHeight = CrouchedHalfHeight;
		}

		newCrouchAlpha = FMath::Clamp(CurrentCrouchAlpha - (DeltaTime / CrouchTime), 0.0f, 1.0f);
		ClampedCrouchedHalfHeight = FMath::Lerp(DefaultStandingHalfHeight, CrouchedHalfHeight, CurrentCrouchAlpha);
		ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, ClampedCrouchedHalfHeight);
	}
	else
	{
		ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, CurrentCapsuleHalfHeight);
	}

	/*calculate the amount to increase by*/
	const float HalfHeightAdjust = ClampedCrouchedHalfHeight - OldUnscaledHalfHeight;

	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	// Grow to uncrouched size.
	check(CharacterOwner->GetCapsuleComponent());

	if (!bClientSimulation)
	{
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
			FVector StandingLocation = PawnLocation + FVector(0.f, 0.f, StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight);
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
			return;
		}
		else
		{
			CurrentCrouchAlpha = newCrouchAlpha;
			CurrentCapsuleHalfHeight = ClampedCrouchedHalfHeight;
		}
	}
	else
	{
		bShrinkProxyCapsule = true;
	}

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), ClampedCrouchedHalfHeight, true);

	//only set bIsCrouched to true if its fully crouched
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == DefaultStandingHalfHeight)
	{
		CharacterOwner->bIsCrouched = false;
		CurrentMovementChange = EMovementChange::MOVE_CHANGE_NONE;
	}

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

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
}

void UFPSCharacterMovementComponent::OnRep_OnCapsuleHalfHeight()
{
	// restore collision size before crouching
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	if (CurrentMovementChange == EMovementChange::STAND_TO_CROUCH && !CharacterOwner->bIsCrouched)
	{
		bWantsToCrouch = true;
		Crouch(true);
	}
	else if (CurrentMovementChange == EMovementChange::CROUCH_TO_STAND)
	{
		bWantsToCrouch = false;
		UnCrouch(true);
	}
	bNetworkUpdateReceived = true;
}

void FSavedMove_Character_FPS::Clear()
{
	Super::Clear();
	bSavedWantsToSprint = false;
	bSavedWantsToProne = false;
}

uint8 FSavedMove_Character_FPS::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint)
	{
		Result |= FLAG_Custom_0;
	}
	if (bSavedWantsToProne)
	{
		Result |= FLAG_Custom_1;
	}

	return Result;
}

bool FSavedMove_Character_FPS::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (bSavedWantsToSprint != ((FSavedMove_Character_FPS*)NewMove.Get())->bSavedWantsToSprint)
		return false;

	if (bSavedWantsToProne != ((FSavedMove_Character_FPS*)NewMove.Get())->bSavedWantsToProne)
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
		bSavedWantsToProne = FPSMov->bWantsToProne;
		SavedCurrentCapsuleHalfHeight = FPSMov->CurrentCapsuleHalfHeight;
	}
}

void FSavedMove_Character_FPS::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	UFPSCharacterMovementComponent* FPSMov = Cast<UFPSCharacterMovementComponent>(Character->GetCharacterMovement());
	if (FPSMov)
	{
		FPSMov->bWantsToSprint = bSavedWantsToSprint;
		FPSMov->bWantsToProne = bSavedWantsToProne;
		FPSMov->CurrentCapsuleHalfHeight = SavedCurrentCapsuleHalfHeight;
	}
}

bool UFPSCharacterMovementComponent::CanWalkOffLedges() const
{
	//#TODO disable character walking off ledges when prone or make them stand up when falling
	return Super::CanWalkOffLedges();
}

void UFPSCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!bIsProne)
	{
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	if (!(bOrientRotationToMovement || bUseControllerDesiredRotation))
	{
		return;
	}

	if (!HasValidData() || (!CharacterOwner->Controller && !bRunPhysicsWithNoController))
	{
		return;
	}

	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): CurrentRotation"));

	FRotator DeltaRot = GetDeltaRotation(DeltaTime);
	DeltaRot.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): GetDeltaRotation"));

	FRotator DesiredRotation = CurrentRotation;
	if (bOrientRotationToMovement)
	{
		DesiredRotation = ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRot);
	}
	else if (CharacterOwner->Controller && bUseControllerDesiredRotation)
	{
		DesiredRotation = CharacterOwner->Controller->GetDesiredRotation();
	}
	else
	{
		return;
	}

	if (ShouldRemainVertical())
	{
		DesiredRotation.Pitch = 0.f;
		DesiredRotation.Yaw = FRotator::NormalizeAxis(DesiredRotation.Yaw);
		DesiredRotation.Roll = 0.f;
	}
	else
	{
		DesiredRotation.Normalize();
	}

	// Accumulate a desired new rotation.
	const float AngleTolerance = 1e-3f;

	if (!CurrentRotation.Equals(DesiredRotation, AngleTolerance))
	{
		// PITCH
		if (!FMath::IsNearlyEqual(CurrentRotation.Pitch, DesiredRotation.Pitch, AngleTolerance))
		{
			DesiredRotation.Pitch = FMath::FixedTurn(CurrentRotation.Pitch, DesiredRotation.Pitch, DeltaRot.Pitch);
		}

		// YAW
		if (!FMath::IsNearlyEqual(CurrentRotation.Yaw, DesiredRotation.Yaw, AngleTolerance))
		{
			DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);
		}

		// ROLL
		if (!FMath::IsNearlyEqual(CurrentRotation.Roll, DesiredRotation.Roll, AngleTolerance))
		{
			DesiredRotation.Roll = FMath::FixedTurn(CurrentRotation.Roll, DesiredRotation.Roll, DeltaRot.Roll);
		}

		

		// Set the new rotation.
		DesiredRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): DesiredRotation"));

		FHitResult Hit(1.0f);
		//if the rotation was blocked by something
		SafeMoveProneComponent(FVector::ZeroVector, DesiredRotation.Quaternion(), true, Hit);

		if (Hit.bBlockingHit)
		{
			/*Need to get the current control rotation since it checks for ShouldRemainVertical() above which resets the yaw*/
			FRotator NewRotation = CharacterOwner->Controller->GetControlRotation();
			FRotator CompRot = UpdatedComponent->GetComponentRotation();
			/*set the view angle to whatever it would be when collided otherwise the view would rotate while capsule will be stuck,
			 *no need to lerp pitch since the capsule will always have 0 pitch and 0 roll.
			 */
			//NewRotation.Pitch = FMath::Lerp(CurrentRotation.Pitch, DesiredRotation.Pitch, Hit.Time);

			// YAW
			if (!FMath::IsNearlyEqual(NewRotation.Yaw, CompRot.Yaw, AngleTolerance))
			{
				NewRotation.Yaw = CompRot.Yaw;
			}

			// ROLL
			if (!FMath::IsNearlyEqual(NewRotation.Roll, CompRot.Roll, AngleTolerance))
			{
				NewRotation.Roll = CompRot.Roll;
			}

			NewRotation.Yaw = FMath::Lerp(CurrentRotation.Yaw, NewRotation.Yaw, Hit.Time);
			NewRotation.Roll = FMath::Lerp(CurrentRotation.Roll, NewRotation.Roll, Hit.Time);

			CharacterOwner->Controller->SetControlRotation(NewRotation);
		}
	}
	
}

void UFPSCharacterMovementComponent::PhysWalking(float deltaTime, int32 Iterations)
{
	if (!bIsProne || !ProneComponent)
	{
		Super::PhysWalking(deltaTime, Iterations);
	}
	else
	{
		PhysProne(deltaTime, Iterations);
	}
}

void UFPSCharacterMovementComponent::PhysProne(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysProne);


	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->Role != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	if (!UpdatedComponent->IsQueryCollisionEnabled())
	{
		SetMovementMode(MOVE_Walking);
		return;
	}

	devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN before Iteration (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || (CharacterOwner->Role == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Save current values
		UPrimitiveComponent * const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != NULL) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		RestorePreAdditiveRootMotionVelocity();

		// Ensure velocity is horizontal.
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;
		Acceleration.Z = 0.f;

		// Apply acceleration
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			CalcVelocity(timeTick, GroundFriction, false, GetMaxBrakingDeceleration());
			devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after CalcVelocity (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));
		}

		ApplyRootMotionToVelocity(timeTick);
		devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after Root Motion application (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

		if (IsFalling())
		{
			// Root motion could have put us into Falling.
			// No movement has taken place this movement tick so we pass on full time/past iteration count
			StartNewPhysics(remainingTime + timeTick, Iterations - 1);
			return;
		}

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;

		if (bZeroDelta)
		{
			remainingTime = 0.f;
		}
		else
		{
			// try to move forward
			MoveAlongFloorProne(MoveVelocity, timeTick, &StepDownResult);

			if (IsFalling())
			{
				// pawn decided to jump up
				const float DesiredDist = Delta.Size();
				if (DesiredDist > KINDA_SMALL_NUMBER)
				{
					const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size2D();
					remainingTime += timeTick * (1.f - FMath::Min(1.f, ActualDist / DesiredDist));
				}
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
		}

		// Update floor.
		// StepUp might have already done it for us.
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);
		}

		// check for ledges here
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// calculate possible alternate movement
			const FVector GravDir = FVector(0.f, 0.f, -1.f);
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, GravDir);
			if (!NewDelta.IsZero())
			{
				// first revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				continue;
			}
			else
			{
				// see if it is OK to jump
				// @todo collision : only thing that can be problem is that oldbase has world collision on
				bool bMustJump = bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;

				// revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.f;
				break;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					CharacterOwner->OnWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + FVector(0.f, 0.f, MAX_FLOOR_DIST);
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// check if just entered water
			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;
			}
		}


		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround())
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
	}

	if (IsMovingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}

void UFPSCharacterMovementComponent::MoveAlongFloorProne(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult /*= NULL*/)
{
	if (!CurrentFloor.IsWalkableFloor())
	{
		return;
	}

	// Move along the current floor
	const FVector Delta = FVector(InVelocity.X, InVelocity.Y, 0.f) * DeltaSeconds;
	FHitResult Hit(1.f);
	FVector RampVector = ComputeGroundMovementDelta(Delta, CurrentFloor.HitResult, CurrentFloor.bLineTrace);
	SafeMoveProneComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);
	float LastMoveTimeSlice = DeltaSeconds;

	if (Hit.bStartPenetrating)
	{
		// Allow this hit to be used as an impact we can deflect off, otherwise we do nothing the rest of the update and appear to hitch.
		HandleImpact(Hit);
		SlideAlongSurface(Delta, 1.f, Hit.Normal, Hit, true);

		if (Hit.bStartPenetrating)
		{
			OnCharacterStuckInGeometry(&Hit);
		}
	}
	else if (Hit.IsValidBlockingHit())
	{
		// We impacted something (most likely another ramp, but possibly a barrier).
		float PercentTimeApplied = Hit.Time;
		if ((Hit.Time > 0.f) && (Hit.Normal.Z > KINDA_SMALL_NUMBER) && IsWalkable(Hit))
		{
			// Another walkable ramp.
			const float InitialPercentRemaining = 1.f - PercentTimeApplied;
			RampVector = ComputeGroundMovementDelta(Delta * InitialPercentRemaining, Hit, false);
			LastMoveTimeSlice = InitialPercentRemaining * LastMoveTimeSlice;
			SafeMoveProneComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);

			const float SecondHitPercent = Hit.Time * InitialPercentRemaining;
			PercentTimeApplied = FMath::Clamp(PercentTimeApplied + SecondHitPercent, 0.f, 1.f);
		}

		if (Hit.IsValidBlockingHit())
		{
			if (CanStepUp(Hit) || (CharacterOwner->GetMovementBase() != NULL && CharacterOwner->GetMovementBase()->GetOwner() == Hit.GetActor()))
			{
				// hit a barrier, try to step up
				const FVector GravDir(0.f, 0.f, -1.f);
				if (!StepUp(GravDir, Delta * (1.f - PercentTimeApplied), Hit, OutStepDownResult))
				{
					UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("- StepUp (ImpactNormal %s, Normal %s"), *Hit.ImpactNormal.ToString(), *Hit.Normal.ToString());
					HandleImpact(Hit, LastMoveTimeSlice, RampVector);
					SlideAlongSurface(Delta, 1.f - PercentTimeApplied, Hit.Normal, Hit, true);
				}
				else
				{
					// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments.
					UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("+ StepUp (ImpactNormal %s, Normal %s"), *Hit.ImpactNormal.ToString(), *Hit.Normal.ToString());
					bJustTeleported |= !bMaintainHorizontalGroundVelocity;
				}
			}
			else if (Hit.Component.IsValid() && !Hit.Component.Get()->CanCharacterStepUp(CharacterOwner))
			{
				HandleImpact(Hit, LastMoveTimeSlice, RampVector);
				SlideAlongSurface(Delta, 1.f - PercentTimeApplied, Hit.Normal, Hit, true);
			}
		}
	}
}

namespace MovementComponentCVars
{
	// Typically we want to depenetrate regardless of direction, so we can get all the way out of penetration quickly.
	// Our rules for "moving with depenetration normal" only get us so far out of the object. We'd prefer to pop out by the full MTD amount.
	// Depenetration moves (in ResolvePenetration) then ignore blocking overlaps to be able to move out by the MTD amount.
	static int32 MoveIgnoreFirstBlockingOverlap = 0;
	static FAutoConsoleVariableRef CVarMoveIgnoreFirstBlockingOverlap(
		TEXT("p.MoveIgnoreFirstBlockingOverlap"),
		MoveIgnoreFirstBlockingOverlap,
		TEXT("Whether to ignore the first blocking overlap in SafeMoveUpdatedComponent (if moving out from object and starting in penetration).\n")
		TEXT("The 'p.InitialOverlapTolerance' setting determines the 'move out' rules, but by default we always try to depenetrate first (not ignore the hit).\n")
		TEXT("0: Disable (do not ignore), 1: Enable (ignore)"),
		ECVF_Default);
}

bool UFPSCharacterMovementComponent::SafeMoveProneComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport /*= ETeleportType::None*/)
{

	if (UpdatedComponent == NULL)
	{
		OutHit.Reset(1.f);
		return false;
	}

	bool bMoveResult = false;

	// Scope for move flags
	{
		// Conditionally ignore blocking overlaps (based on CVar)
		const EMoveComponentFlags IncludeBlockingOverlapsWithoutEvents = (MOVECOMP_NeverIgnoreBlockingOverlaps | MOVECOMP_DisableBlockingOverlapDispatch);
		TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, MovementComponentCVars::MoveIgnoreFirstBlockingOverlap ? MoveComponentFlags : (MoveComponentFlags | IncludeBlockingOverlapsWithoutEvents));
		bMoveResult = MoveProneComponent(Delta, NewRotation, bSweep, &OutHit, Teleport);
	}

	// Handle initial penetrations
	if (OutHit.bStartPenetrating && UpdatedComponent)
	{
		const FVector RequestedAdjustment = GetPenetrationAdjustment(OutHit);
		if (ResolvePronePenetration(RequestedAdjustment, OutHit, NewRotation))
		{
			// Retry original move
			bMoveResult = MoveProneComponent(Delta, NewRotation, bSweep, &OutHit, Teleport);
		}
	}

	return bMoveResult;
}

bool UFPSCharacterMovementComponent::MoveProneComponentImpl(const FVector& Delta, const FQuat& Rotation, bool bSweep, FHitResult* OutHit /*= NULL*/, ETeleportType Teleport /*= ETeleportType::None*/)
{
	if (!UpdatedComponent || !ProneComponent)
		return false;
	
	FVector NewDelta = ConstrainDirectionToPlane(Delta);
	FQuat NewRotation = Rotation;

	FHitResult Hit(1.0f);
	// test if our move will not hit something with additional collisions
	const bool bMoved = bSweep ? SimulateProneComponent(NewDelta, NewRotation, &Hit) : true;

	if (!bMoved)
	{
		NewDelta *= Hit.Time; // adjust delta to move as much as possible to location before the hit based on hit time
		NewRotation = FQuat::Slerp(UpdatedComponent->GetComponentQuat(), Rotation, Hit.Time); // adjust rotation
	}

	if (OutHit)
		*OutHit = Hit;

	// We move updated component without sweep because sweep is used on additional collisions only
	UpdatedComponent->MoveComponent(NewDelta, NewRotation, false, nullptr, MoveComponentFlags, ETeleportType::TeleportPhysics);

	// Force transform update of AdditionalUpdatedComponents after any move/turn happened
	UpdatedComponent->UpdateChildTransforms(EUpdateTransformFlags::PropagateFromParent, ETeleportType::TeleportPhysics);

	RotateProneComponent();

	return bMoved;
}

namespace SimulateProneComponent
{
	static float InitialOverlapToleranceCVar = 0.0f;
	static FAutoConsoleVariableRef CVarInitialOverlapTolerance(
		TEXT("p.InitialOverlapTolerance"),
		InitialOverlapToleranceCVar,
		TEXT("Tolerance for initial overlapping test in PrimitiveComponent movement.\n")
		TEXT("Normals within this tolerance are ignored if moving out of the object.\n")
		TEXT("Dot product of movement direction and surface normal."),
		ECVF_Default);

	static void PullBackHit(FHitResult& Hit, const FVector& Start, const FVector& End, const float Dist)
	{
		const float DesiredTimeBack = FMath::Clamp(0.1f, 0.1f / Dist, 1.f / Dist) + 0.001f;
		Hit.Time = FMath::Clamp(Hit.Time - DesiredTimeBack, 0.f, 1.f);
	}

	static bool ShouldIgnoreHitResult(const UWorld* InWorld, FHitResult const& TestHit, FVector const& MovementDirDenormalized, const AActor* MovingActor, EMoveComponentFlags MoveFlags)
	{
		if (TestHit.bBlockingHit)
		{
			// check "ignore bases" functionality
			if ((MoveFlags & MOVECOMP_IgnoreBases) && MovingActor)	//we let overlap components go through because their overlap is still needed and will cause beginOverlap/endOverlap events
			{
				// ignore if there's a base relationship between moving actor and hit actor
				AActor const* const HitActor = TestHit.GetActor();
				if (HitActor)
				{
					if (MovingActor->IsBasedOnActor(HitActor) || HitActor->IsBasedOnActor(MovingActor))
					{
						return true;
					}
				}
			}

			// If we started penetrating, we may want to ignore it if we are moving out of penetration.
			// This helps prevent getting stuck in walls.
			if (TestHit.bStartPenetrating && !(MoveFlags & MOVECOMP_NeverIgnoreBlockingOverlaps))
			{
				const float DotTolerance = InitialOverlapToleranceCVar;

				// Dot product of movement direction against 'exit' direction
				const FVector MovementDir = MovementDirDenormalized.GetSafeNormal();
				const float MoveDot = (TestHit.ImpactNormal | MovementDir);

				const bool bMovingOut = MoveDot > DotTolerance;

				// If we are moving out, ignore this result!
				if (bMovingOut)
				{
					return true;
				}
			}
		}

		return false;
	}
}

bool UFPSCharacterMovementComponent::SimulateProneComponent(const FVector& NewDelta, const FQuat& Rotation, FHitResult* OutHit)
{
	const FQuat DeltaQuat = Rotation * UpdatedComponent->GetComponentQuat().Inverse(); // find delta rotation of the root component
	const FQuat NewCompQuat = DeltaQuat * ProneComponent->GetComponentQuat(); // calc new rotation for this component
	const FVector TraceStart = ProneComponent->GetComponentLocation();
	const FVector DeltaLocation = TraceStart - UpdatedComponent->GetComponentLocation();
	const FVector DeltaDir = DeltaLocation.GetSafeNormal();
	const float DirSize = DeltaLocation.Size();
	const FVector NewDir = DeltaQuat.RotateVector(DeltaDir); // turn direction vector on delta rotation
	const FVector NewComponentLocation = UpdatedComponent->GetComponentLocation() + NewDir * DirSize;
	const FVector TraceEnd = NewComponentLocation + NewDelta;

	TArray<FHitResult> Hits;
	Hits.Empty();

	FComponentQueryParams QueryParams(TEXT("SimulateMoveComponent"), GetOwner());
	FCollisionResponseParams ResponseParam;
	ProneComponent->InitSweepCollisionParams(QueryParams, ResponseParam);

	const bool bHadBlockingHit = GetWorld()->ComponentSweepMulti(Hits, ProneComponent, TraceStart, TraceEnd, NewCompQuat, QueryParams);


	if (Hits.Num() > 0)
	{
		const float NewDeltaSize = NewDelta.Size();
		for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
		{
			SimulateProneComponent::PullBackHit(Hits[HitIdx], TraceStart, TraceEnd, NewDeltaSize);
		}
	}

	if (bHadBlockingHit)
	{
		FHitResult BlockingHit(NoInit);
		BlockingHit.bBlockingHit = false;
		BlockingHit.Time = 1.f;

		int32 BlockingHitIndex = INDEX_NONE;
		float BlockingHitNormalDotDelta = BIG_NUMBER;
		for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
		{
			const FHitResult& TestHit = Hits[HitIdx];

			if (TestHit.bBlockingHit)
			{
				if (!SimulateProneComponent::ShouldIgnoreHitResult(GetWorld(), TestHit, NewDelta, GetOwner(), MoveComponentFlags))
				{
					if (TestHit.Time == 0.f)
					{
						// We may have multiple initial hits, and want to choose the one with the normal most opposed to our movement.
						const float NormalDotDelta = (TestHit.ImpactNormal | NewDelta);
						if (NormalDotDelta < BlockingHitNormalDotDelta)
						{
							BlockingHitNormalDotDelta = NormalDotDelta;
							BlockingHitIndex = HitIdx;
						}
					}
					else if (BlockingHitIndex == INDEX_NONE)
					{
						// First non-overlapping blocking hit should be used, if an overlapping hit was not.
						// This should be the only non-overlapping blocking hit, and last in the results.
						BlockingHitIndex = HitIdx;
						break;
					}
				}
			}
		}

		// Update blocking hit, if there was a valid one.
		if (BlockingHitIndex >= 0)
		{
			BlockingHit = Hits[BlockingHitIndex];

			if (OutHit)
			{
				*OutHit = BlockingHit;
			}

			return false;
		}
		else
		{
			return true;
		}
	}

	return true;
}

bool UFPSCharacterMovementComponent::ResolvePronePenetration(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotation)
{
	// If movement occurs, mark that we teleported, so we don't incorrectly adjust velocity based on a potentially very different movement than our movement direction.
	bJustTeleported |= ResolvePronePenetrationImpl(Adjustment, Hit, NewRotation);
	return bJustTeleported;
}

namespace MovementComponentCVars
{
	static float PenetrationOverlapCheckInflation = 0.100f;
	static FAutoConsoleVariableRef CVarPenetrationOverlapCheckInflation(TEXT("p.PenetrationOverlapCheckInflation"),
		PenetrationOverlapCheckInflation,
		TEXT("Inflation added to object when checking if a location is free of blocking collision.\n")
		TEXT("Distance added to inflation in penetration overlap check."),
		ECVF_Default);
}

bool UFPSCharacterMovementComponent::ResolvePronePenetrationImpl(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat)
{
	// SceneComponent can't be in penetration, so this function really only applies to PrimitiveComponent.
	const FVector Adjustment = ConstrainDirectionToPlane(ProposedAdjustment);
	if (!Adjustment.IsZero() && UpdatedPrimitive && ProneComponent)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_MovementComponent_ResolvePenetration);
		// See if we can fit at the adjusted location without overlapping anything.
		AActor* ActorOwner = UpdatedComponent->GetOwner();
		if (!ActorOwner)
		{
			return false;
		}

/*
		UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("ResolvePenetration: %s.%s at location %s inside %s.%s at location %s by %.3f (netmode: %d)"),
			*ActorOwner->GetName(),
			*UpdatedComponent->GetName(),
			*UpdatedComponent->GetComponentLocation().ToString(),
			*GetNameSafe(Hit.GetActor()),
			*GetNameSafe(Hit.GetComponent()),
			Hit.Component.IsValid() ? *Hit.GetComponent()->GetComponentLocation().ToString() : TEXT("<unknown>"),
			Hit.PenetrationDepth,
			(uint32)GetNetMode());*/

		// We really want to make sure that precision differences or differences between the overlap test and sweep tests don't put us into another overlap,
		// so make the overlap test a bit more restrictive.
		const float OverlapInflation = MovementComponentCVars::PenetrationOverlapCheckInflation;
		bool bEncroached = OverlapTest(Hit.TraceStart + Adjustment, ProneComponent->GetComponentQuat(), ProneComponent->GetCollisionObjectType(), ProneComponent->GetCollisionShape(OverlapInflation), ActorOwner);
		if (!bEncroached)
		{
			// Move without sweeping.
			MoveProneComponent(Adjustment, NewRotationQuat, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("ResolvePenetration:   teleport by %s"), *Adjustment.ToString());
			return true;
		}
		else
		{
			// Disable MOVECOMP_NeverIgnoreBlockingOverlaps if it is enabled, otherwise we wouldn't be able to sweep out of the object to fix the penetration.
			TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, EMoveComponentFlags(MoveComponentFlags & (~MOVECOMP_NeverIgnoreBlockingOverlaps)));

			// Try sweeping as far as possible...
			FHitResult SweepOutHit(1.f);
			bool bMoved = MoveProneComponent(Adjustment, NewRotationQuat, true, &SweepOutHit, ETeleportType::TeleportPhysics);
			UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (success = %d)"), *Adjustment.ToString(), bMoved);

			// Still stuck?
			if (!bMoved && SweepOutHit.bStartPenetrating)
			{
				// Combine two MTD results to get a new direction that gets out of multiple surfaces.
				const FVector SecondMTD = GetPenetrationAdjustment(SweepOutHit);
				const FVector CombinedMTD = Adjustment + SecondMTD;
				if (SecondMTD != Adjustment && !CombinedMTD.IsZero())
				{
					bMoved = MoveProneComponent(CombinedMTD, NewRotationQuat, true, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (MTD combo success = %d)"), *CombinedMTD.ToString(), bMoved);
				}
			}

			// Still stuck?
			if (!bMoved)
			{
				// Try moving the proposed adjustment plus the attempted move direction. This can sometimes get out of penetrations with multiple objects
				const FVector MoveDelta = ConstrainDirectionToPlane(Hit.TraceEnd - Hit.TraceStart);
				if (!MoveDelta.IsZero())
				{
					bMoved = MoveProneComponent(Adjustment + MoveDelta, NewRotationQuat, true, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogFPSCharacterMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (adjusted attempt success = %d)"), *(Adjustment + MoveDelta).ToString(), bMoved);
				}
			}

			return bMoved;
		}
	}

	return false;
}

float UFPSCharacterMovementComponent::SlideAlongSurfaceProne(const FVector& Delta, float Time, const FVector& InNormal, FHitResult &Hit, bool bHandleImpact /*= false*/)
{
	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}

	FVector Normal(InNormal);
	if (IsMovingOnGround())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (Normal.Z > 0.f)
		{
			if (!IsWalkable(Hit))
			{
				Normal = Normal.GetSafeNormal2D();
			}
		}
		else if (Normal.Z < -KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (FloorNormal.Z < 1.f - DELTA);
				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}

				Normal = Normal.GetSafeNormal2D();
			}
		}
	}

	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}

	float PercentTimeApplied = 0.f;
	const FVector OldHitNormal = Normal;

	FVector SlideDelta = ComputeSlideVector(Delta, Time, Normal, Hit);

	if ((SlideDelta | Delta) > 0.f)
	{
		const FQuat Rotation = UpdatedComponent->GetComponentQuat();
		SafeMoveProneComponent(SlideDelta, Rotation, true, Hit);

		const float FirstHitPercent = Hit.Time;
		PercentTimeApplied = FirstHitPercent;
		if (Hit.IsValidBlockingHit())
		{
			// Notify first impact
			if (bHandleImpact)
			{
				HandleImpact(Hit, FirstHitPercent * Time, SlideDelta);
			}

			// Compute new slide normal when hitting multiple surfaces.
			TwoWallAdjust(SlideDelta, Hit, OldHitNormal);

			// Only proceed if the new direction is of significant length and not in reverse of original attempted move.
			if (!SlideDelta.IsNearlyZero(1e-3f) && (SlideDelta | Delta) > 0.f)
			{
				// Perform second move
				SafeMoveProneComponent(SlideDelta, Rotation, true, Hit);
				const float SecondHitPercent = Hit.Time * (1.f - FirstHitPercent);
				PercentTimeApplied += SecondHitPercent;

				// Notify second impact
				if (bHandleImpact && Hit.bBlockingHit)
				{
					HandleImpact(Hit, SecondHitPercent * Time, SlideDelta);
				}
			}
		}

		return FMath::Clamp(PercentTimeApplied, 0.f, 1.f);
	}

	return 0.f;
}

void UFPSCharacterMovementComponent::RotateProneComponent()
{
/*
	UCapsuleComponent* UpdatedCapsule = Cast<UCapsuleComponent>(UpdatedComponent);

	float ScaledRadius = UpdatedCapsule->GetScaledCapsuleRadius();
	float ScaledHeight = UpdatedCapsule->GetScaledCapsuleHalfHeight();

	FVector HeadLocation = UpdatedComponent FVector(ScaledRadius - ScaledHeight, 0.0f, ScaledRadius - ScaledHeight);
	FHitResult HeadHit(1.0f);
	FindFloor(HeadLocation, HeadHit, true, )*/

	// Increase height check slightly if walking, to prevent floor height adjustment from later invalidating the floor result.
	const float HeightCheckAdjust = (IsMovingOnGround() ? MAX_FLOOR_DIST + KINDA_SMALL_NUMBER : -MAX_FLOOR_DIST);

	float FloorSweepTraceDist = FMath::Max(MAX_FLOOR_DIST, MaxStepHeight + HeightCheckAdjust);

	
}

void UFPSCharacterMovementComponent::ComputeFloorDist(const FVector& CapsuleLocation, float SweepDistance, float CapsuleHeight, float SweepRadius, const FHitResult* DownwardSweepResult /*= NULL*/) const
{

}

