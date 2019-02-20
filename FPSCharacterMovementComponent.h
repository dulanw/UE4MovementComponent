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
enum EMovementChange
{
	/**No change in state*/
	MOVE_CHANGE_NONE		UMETA(DisplayName="None"),

	/**character is going into a crouch*/
	STAND_TO_CROUCH	UMETA(DisplayName="Stand to Crouch"),

	/**character is trying to come out of crouch or stand */
	CROUCH_TO_STAND	UMETA(DisplayName = "Crouch to Stand")
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
	uint8 bSavedWantsToProne : 1;
	float SavedCurrentCapsuleHalfHeight;

	//#TODO maybe save the current capsule height as well
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

UCLASS()
class UFPSCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:

	/**
	 * UObject constructor.
	 */
	UFPSCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

	//virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	/** Overridden to auto-register the updated component if it starts NULL, and we can find a root component on our owner. */
	virtual void InitializeComponent() override;

	/* return the Max Speed for the current state. */
	virtual float GetMaxSpeed() const override;

	/** @return Maximum acceleration for the current state. */
	virtual float GetMaxAcceleration() const override;

	/** Update the character state in PerformMovement right before doing the actual position change */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/*VARIABLE REPLICATION using component, don't know if there is any overhead but according to the documentation page there shouldn't be any.*/
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Client_Character. */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Unpack compressed flags from a saved move and set state accordingly. See FSavedMove_Character. */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	/** @return whether this pawn is currently allowed to walk off ledges */
	virtual bool CanWalkOffLedges() const override;

	/** Perform rotation over deltaTime */
	virtual void PhysicsRotation(float DeltaTime) override;

public:
	/*current movement change, i.e standing up from crouch or prone or none if not changing*/
	UPROPERTY(BlueprintReadOnly, Replicated)
	TEnumAsByte<EMovementChange> CurrentMovementChange;

	/*CurrentCrouchHeight can be used for animations etc, and used to set the height of the simulated (proxy) capsule height*/
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_OnCapsuleHalfHeight, Category = "Crouch")
	float CurrentCapsuleHalfHeight;

public:
	/*SPRINTING START*/
	/* does the character want to sprint, set to true from StartSpriting.
	 * set to true in StartSprint and false in StopSprint.
	 * if held down, it will start automatically sprinting the next time its possible to sprint, check in CanSprint
	 */
	uint8 bWantsToSprint : 1;

	//#TODO add a cool down timer
	/*Max sprint time before cool down sets in, -1 for unlimited*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Sprint")
	float MaxSprintTime;

	/** Set by character movement to specify that this Character is currently sprinting.
	 * this can be used for animations i.e. playing a running animations when holding a weapon.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Sprint")
	uint8 bIsSprinting : 1;

	/*set the max speed to the normal Walking walking speed multiplied by this amount when sprinting*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Sprint", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaxWalkSpeedSprint;

	/*the amount you can move to the side, 1 will allow the player to sprint sideways*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Sprint", meta = (ClampMin = "1.0", ClampMax = "0.0"))
	float SprintSideMovementMultiplier;

	/*The maximum Accleration calculated using the currentSpeed/maximum speed,
	 *this will give a 2x boost multiplier if the player is moving too slow at the start
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Sprint")
	UCurveFloat* SprintAccelerationCurve;

	/** If true, this Pawn is capable of sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MovementProperties)
	uint8 bCanSprint : 1;
	/*SPRINTING END*/

public:
	/*CROUCH START*/
	/*The current half height of the player capsule is calculated using this as x value for CrouchAlphaCurve.
	 *
	 */
	float CurrentCrouchAlpha;

	/*The Time taken to crouch, the change in height doesn't matter since its calculated*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Crouch", meta = (ClampMin = "0.1"))
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
	/*CROUCH END*/

public:
	/*PRONE START*/

	/* does the character want to sprint, set to true from StartSpriting.
	 * set to true in StartSprint and false in StopSprint.
	 * if held down, it will start automatically sprinting the next time its possible to sprint, check in CanSprint
	 */
	uint8 bWantsToProne : 1;

	/** If true, this Pawn is capable of prone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prone")
	uint8 bCanEverProne : 1;

	/**
	 * The component we move and update for the prone.
	 * If this is null at startup and bAutoRegisterUpdatedComponent is true, the owning Actor's root component will automatically be set as our UpdatedComponent at startup.
	 * @see bAutoRegisterUpdatedComponent, SetUpdatedComponent(), UpdatedPrimitive
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, DuplicateTransient, Category = "Prone")
	UCapsuleComponent* ProneComponent;

	/** If true, registers the owner's Root component as the UpdatedComponent if there is not one currently assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MovementComponent)
	uint8 bAutoRegisterProneUpdatedComponent : 1;

	/** Set by character movement to specify that this Character is currently sprinting.
	 * this can be used for animations i.e. playing a running animations when holding a weapon.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Prone")
	uint8 bIsProne : 1;

protected:
	/*override, call PhysProne if the character is currently prone*/
	virtual void PhysProne(float deltaTime, int32 Iterations);

	/**
	 * Move along the floor, using CurrentFloor and ComputeGroundMovementDelta() to get a movement direction.
	 * If a second walkable surface is hit, it will also be moved along using the same approach.
	 *
	 * @param InVelocity:			Velocity of movement
	 * @param DeltaSeconds:			Time over which movement occurs
	 * @param OutStepDownResult:	[Out] If non-null, and a floor check is performed, this will be updated to reflect that result.
	 */
	virtual void MoveAlongFloorProne(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult = NULL);

	/**
	 * Calls MoveUpdatedComponent(), handling initial penetrations by calling ResolvePenetration().
	 * If this adjustment succeeds, the original movement will be attempted again.
	 * @note The overload taking rotation as an FQuat is slightly faster than the version using FRotator (which will be converted to an FQuat).
	 * @note The 'Teleport' flag is currently always treated as 'None' (not teleporting) when used in an active FScopedMovementUpdate.
	 * @return result of the final MoveUpdatedComponent() call.
	 */
	virtual bool SafeMoveProneComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport = ETeleportType::None);

	bool MoveProneComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, ETeleportType Teleport = ETeleportType::None);
	bool MoveProneComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* OutHit = NULL, ETeleportType Teleport = ETeleportType::None);

	virtual bool MoveProneComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, ETeleportType Teleport = ETeleportType::None);

	/*@return true if the component moved without getting blocked, false if it was blocked by something*/
	virtual bool SimulateProneComponent(const FVector& NewDelta, const FQuat& Rotation, FHitResult* OutHit);

	virtual bool ResolvePronePenetration(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotation);
	virtual bool ResolvePronePenetrationImpl(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat);

	/**
	 * Slide smoothly along a surface, and slide away from multiple impacts using TwoWallAdjust if necessary. Calls HandleImpact for each surface hit, if requested.
	 * Uses SafeMoveUpdatedComponent() for movement, and ComputeSlideVector() to determine the slide direction.
	 * @param Delta:	Attempted movement vector.
	 * @param Time:		Percent of Delta to apply (between 0 and 1). Usually equal to the remaining time after a collision: (1.0 - Hit.Time).
	 * @param Normal:	Normal opposing movement, along which we will slide.
	 * @param Hit:		[In] HitResult of the attempted move that resulted in the impact triggering the slide. [Out] HitResult of last attempted move.
	 * @param bHandleImpact:	Whether to call HandleImpact on each hit.
	 * @return The percentage of requested distance (Delta * Percent) actually applied (between 0 and 1). 0 if no movement occurred, non-zero if movement occurred.
	 */
	virtual float SlideAlongSurfaceProne(const FVector& Delta, float Time, const FVector& Normal, FHitResult &Hit, bool bHandleImpact = false);

	virtual void RotateProneComponent();


	/**
	 * Compute distance to the floor from bottom sphere of capsule and store the result in OutFloorResult.
	 * This distance is the swept distance of the capsule to the first point impacted by the lower hemisphere, or distance from the bottom of the capsule in the case of a line trace.
	 * This function does not care if collision is disabled on the capsule (unlike FindFloor).
	 * @see FindFloor
	 *
	 * @param CapsuleLocation:	Location of the capsule used for the query
	 * @param LineDistance:		If non-zero, max distance to test for a simple line check from the capsule base. Used only if the sweep test fails to find a walkable floor, and only returns a valid result if the impact normal is a walkable normal.
	 * @param SweepDistance:	If non-zero, max distance to use when sweeping a capsule downwards for the test. MUST be greater than or equal to the line distance.
	 * @param OutFloorResult:	Result of the floor check. The HitResult will contain the valid sweep or line test upon success, or the result of the sweep upon failure.
	 * @param SweepRadius:		The radius to use for sweep tests. Should be <= capsule radius.
	 * @param DownwardSweepResult:	If non-null and it contains valid blocking hit info, this will be used as the result of a downward sweep test instead of doing it as part of the update.
	 */
	virtual void ComputeFloorDist(const FVector& CapsuleLocation, float SweepDistance, float CapsuleHeight, float SweepRadius, const FHitResult* DownwardSweepResult = NULL) const;


	/*PRONE END*/

protected:
	/*called when currentHalfHeight replicates, this is calls crouch and adjusts the capsule height*/
	UFUNCTION()
	void OnRep_OnCapsuleHalfHeight();

	/*override, call PhysProne if the character is currently prone*/
	virtual void PhysWalking(float deltaTime, int32 Iterations) override;
};


/*INLINE*/
FORCEINLINE bool UFPSCharacterMovementComponent::MoveProneComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit /*= NULL*/, ETeleportType Teleport /*= ETeleportType::None*/)
{
	return MoveProneComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
}

FORCEINLINE bool UFPSCharacterMovementComponent::MoveProneComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* OutHit /*= NULL*/, ETeleportType Teleport /*= ETeleportType::None*/)
{
	return MoveProneComponentImpl(Delta, NewRotation.Quaternion(), bSweep, OutHit, Teleport);
}
