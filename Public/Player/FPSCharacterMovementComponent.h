// Copyright 2019 Dulan Wettasinghe. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FPSCharacterMovementComponent.generated.h"

 /*Bit masks used by GetCompressedFlags() to encode movement information.
 *							FLAG_JumpPressed = 0x01,	// Jump pressed
 *							FLAG_WantsToCrouch = 0x02,	// Wants to crouch
 *							FLAG_Reserved_1 = 0x04,	// Reserved for future use
 *							FLAG_Reserved_2 = 0x08,	// Reserved for future use
 *							Remaining bit masks are available for custom flags.
 *							FLAG_Custom_0 = 0x10, // Sprinting
 *							FLAG_Custom_1 = 0x20
 */

//=============================================================================
/**
 * CharacterMovementComponent handles movement logic for the associated Character owner.
 * It supports various movement modes including: walking, falling, swimming, flying, custom.
 *
 * Movement is affected primarily by current Velocity and Acceleration. Acceleration is updated each frame
 * based on the input vector accumulated thus far (see UPawnMovementComponent::GetPendingInputVector()).
 *
 * Networking is fully implemented, with server-client correction and prediction included.
 *
 * @see ACharacter, UPawnMovementComponent
 * @see https://docs.unrealengine.com/latest/INT/Gameplay/Framework/Pawn/Character/
 */


/** Movement modes for Characters. */
UENUM(BlueprintType)
enum EMovementTransition
{
	None,
	Stand_to_Crouch,
	Crouch_to_Stand
};

class FSavedMove_Character_FPS : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character & ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;

	uint8 bSavedWantsToSprint : 1;
	float SavedCapsuleHeight;
	TEnumAsByte<EMovementTransition> SavedTransition;
};

class FNetworkPredictionData_Client_Character_FPS : public FNetworkPredictionData_Client_Character
{
public:
	FNetworkPredictionData_Client_Character_FPS(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {};
	typedef FNetworkPredictionData_Client_Character Super;
	virtual FSavedMovePtr AllocateNewMove() override;
};

class UCurveFloat;
class UCapsuleComponent;
class AFPSCharacterBase;

UCLASS()
class UFPSCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:

	/**
	 * UObject constructor.
	 */
	UFPSCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

	/* return the Max Speed for the current state. */
	virtual float GetMaxSpeed() const override;

	/** @return Maximum acceleration for the current state. */
	virtual float GetMaxAcceleration() const override;

	/** Update the character state in PerformMovement right before doing the actual position change
	 * Using OnMovementUpdated to do the stuff in here for the simulated proxy since this is not called on the simulated, need a tick.
	 */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Client_Character. */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Unpack compressed flags from a saved move and set state accordingly. See FSavedMove_Character. */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	/*returns true if the capsule was shrunk successfully*/
	virtual bool ShrinkCapsule(float NewUnscaledHalfHeight, bool bClientSimulation);
	/*returns true if the capsule was expanded successfully or false if it hits something*/
	virtual bool ExpandCapsule(float NewUnscaledHalfHeight, bool bClientSimulation);

	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;
	virtual void PostLoad() override;

	AFPSCharacterBase* GetFPSOwner() { return FPSCharacterOwner; }

protected:
	/**FPS Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
	AFPSCharacterBase* FPSCharacterOwner = nullptr;

	virtual bool IsMovingForward();

	/**
	 * Event triggered at the end of a movement update. If scoped movement updates are enabled (bEnableScopedMovementUpdates), this is within such a scope.
	 * If that is not desired, bind to the CharacterOwner's OnMovementUpdated event instead, as that is triggered after the scoped movement update.
	 * Using this instead of SimulateTick because it's not marked virtual, need to check if Simulated proxy because this is called by owner and server.
	 * Update the capsule size, crouch, prone, vaulting etc in here for the simulated proxy, for everyone else it's done in UpdateCharacterStateBeforeMovement
	 */
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

public:
	/*current movement change, i.e standing up from crouch or prone or none if not changing*/
	TEnumAsByte<EMovementTransition> CurrentTransition;

	/*used for crouch eye height calculations*/
	float InternalCapsuleHeight;

public:
	/* does the character want to sprint, set to true from StartSpriting.
	 * set to true in StartSprint and false in StopSprint.
	 * if held down, it will start automatically sprinting the next time its possible to sprint, check in CanSprint
	 */
	uint8 bWantsToSprint : 1;

	//#TODO add a cool down timer
	/*Max sprint time before cool down sets in, -1 for unlimited NOT USED FOR NOW*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = Sprint)
	float MaxSprintTime;

	/*set the max speed to the normal Walking walking speed multiplied by this amount when sprinting*/
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float MaxSprintSpeed;

	/** The maximum ground speed when walking and crouched. */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float MaxWalkSpeedProne;

	/*the amount you can move to the side, 1 will allow the player to sprint sideways*/
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SprintSideMultiplier;

	/*The maximum Accleration calculated using the currentSpeed/maximum speed,
	 *this will give a 2x boost multiplier if the player is moving too slow at the start
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Sprint")
	UCurveFloat* SprintAccelerationCurve;

	/** If true, this Pawn is capable of sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MovementProperties)
	uint8 bCanSprint : 1;

	virtual bool IsSprinting() const;

public:
	/*This is set to true along side bWantsToCrouch or when bIsCrouched is replicated to the SimulatedProxy
	 *Set to false when crouching is completed and doesn't need to call Crouch or Uncrouch everytick.
	 */
	uint8 bCheckCrouch : 1;

	/*The Time taken to crouch, the change in height doesn't matter since its calculated*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = Crouch, meta = (ClampMin = "0.1"))
	float CrouchTime;
	
	/**
	 * Checks if new capsule size fits (no encroachment), and call CharacterOwner->OnStartCrouch() if successful.
	 * In general you should set bWantsToCrouch instead to have the crouch persist during movement, or just use the crouch functions on the owning Character.
	 * @param	bClientSimulation	true when called when bIsCrouched is replicated to non owned clients, to update collision cylinder and offset.
	 */
	virtual void Crouch(bool bClientSimulation = false, float DeltaTime = 0.0f);

	/**
	 * Checks if default capsule size fits (no encroachment), and trigger OnEndCrouch() on the owner if successful.
	 * @param	bClientSimulation	true when called when bIsCrouched is replicated to non owned clients, to update collision cylinder and offset.
	 */
	virtual void UnCrouch(bool bClientSimulation = false, float DeltaTime = 0.0f);
};