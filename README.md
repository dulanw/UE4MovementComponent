# UE4MovementComponent

## Update
The movement component is getting reworked to make easily extensible, the prone movement is getting reworked and vaulting will be implemented soon.
Now your pawn will need to be a child of the FPSCharacterBase.

## Old
Custom Movement Component extends the default Character Movement Component adding crouch time, prone and sprinting.
Fully networked and ready for use with multiplayer, prone is current work-in-progress, works fine in flat plane/terrain.

This extends the default unreal character movement component created by Epic and if using the default character pawn you will need to create a child class and override the movement component using Super(ObjectInitializer.SetDefaultSubobjectClass<UFPSCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
  
 This is still not complete, sprint and crouch function works and all the variables are commented. 
 If you wish to complete the prone movement, then look at PhysProne method.
