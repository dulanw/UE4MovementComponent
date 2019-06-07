// Copyright 2019 Dulan Wettasinghe. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FPSCharacterBase.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class UFPSHitBoxesManager;

UCLASS()
class FPSGAME_API AFPSCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	/*Sets default values for this character's properties*/
	AFPSCharacterBase(const FObjectInitializer& ObjectInitializer);

	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** The default camera used for the player, the height is set to the BaseEyeHeight at BeginPlay, and adjusted to capsule size * BaseHeightCameraRatio during play */
	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* CameraComponent;

	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UFPSHitBoxesManager* HitBoxManager;

protected:
	/* Called when the game starts or when spawned*/
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

public:
	/*The default Eye height of the player, saved so we can set it when standing up after crouching*/
	float DefaultEyeHeight;

	/** Set by character movement to specify that this Character is currently crouched. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsSprinting, Category = Character)
	uint32 bIsSprinting : 1;

	/** Handle Crouching replicated from server */
	virtual void OnRep_IsCrouched() override;

	/** Handle sprinting replicated from server */
	UFUNCTION()
	virtual void OnRep_IsSprinting();

public:	
	/*Called every frame*/
	virtual void Tick(float DeltaTime) override;

	/*Called to bind functionality to input*/
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/*DEFAULT MOVEMENT*/
	void MoveForward(float Val);
	void MoveRight(float Val);


	void ToggleCrouch();
	/**
	 * Request the character to start crouching. The request is processed on the next update of the CharacterMovementComponent.
	 * @see OnStartCrouch
	 * @see IsCrouched
	 * @see CharacterMovement->WantsToCrouch
	 */
	virtual void Crouch(bool bClientSimulation = false) override;

	/**
	 * Request the character to stop crouching. The request is processed on the next update of the CharacterMovementComponent.
	 * @see OnEndCrouch
	 * @see IsCrouched
	 * @see CharacterMovement->WantsToCrouch
	 */
	virtual void UnCrouch(bool bClientSimulation = false) override;

	/*SPRINTING*/
	void StartSprint();
	void StopSprint();

	/*override RecalculateBaseEyeHeight also manually set the camera height manually since it doesn't seem to be updateing
	 *the base eye height might only be used when a camera component is not available.
	 */
	virtual void RecalculateBaseEyeHeight() override;

	/**
	 * Called when capsule size is changed
	 * @param	HalfHeightAdjust		difference between default collision half-height, and actual crouched capsule half-height.
	 * @param	ScaledHalfHeightAdjust	difference after component scale is taken in to account.
	 */
	virtual void CapsuleAdjusted(float HalfHeightAdjust, float ScaledHalfHeightAdjust);

	/** @return true if this character is currently able to crouch (and is not currently crouched) */
	virtual bool CanCrouch() override;
};
