// Copyright Komorebi Project. Player character Kael implementation with Enhanced Input, camera, combat, equipment sockets, driving state, and online pose replication.
#include "Character/KaelCharacter.h"

#include "Camera/CameraComponent.h"
#include "Character/FollowCameraComponent.h"
#include "Character/KaelAnimInstance.h"
#include "Character/KaelMovementComponent.h"
#include "Combat/BladeTrailComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

AKaelCharacter::AKaelCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UKaelMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Match player.js TUNING capsule: radius=28cm, height=180cm (half-height=90cm)
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(UKaelMovementComponent::kCapsuleRadiusCm, UKaelMovementComponent::kCapsuleHalfHeightCm);
	}

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -UKaelMovementComponent::kCapsuleHalfHeightCm));
		SkelMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	}

	FollowCamera = CreateDefaultSubobject<UFollowCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(RootComponent);

	ViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewCamera"));
	ViewCamera->SetupAttachment(FollowCamera);
	ViewCamera->bUsePawnControlRotation = false;
	ViewCamera->SetFieldOfView(52.0f);

	CombatComponent = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));

	BladeTrailComponent = CreateDefaultSubobject<UBladeTrailComponent>(TEXT("BladeTrailComponent"));
	BladeTrailComponent->SetupAttachment(RootComponent);

	KatanaMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("KatanaMesh"));
	KatanaMesh->SetupAttachment(GetMesh(), KatanaSocketName);
	KatanaMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SpiritLanternMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpiritLanternMesh"));
	SpiritLanternMesh->SetupAttachment(GetMesh(), HipLanternSocketName);
	SpiritLanternMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Hip mount offset matching combat.js (-0.155, -0.055, -0.055) m -> UE local cm
	SpiritLanternMesh->SetRelativeLocation(FVector(-5.5f, 15.5f, -5.5f));
	SpiritLanternMesh->SetRelativeRotation(FRotator(3.44f, -12.6f, -5.73f));
	SpiritLanternMesh->SetRelativeScale3D(FVector(0.55f));

	LanternLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternLight"));
	LanternLight->SetupAttachment(SpiritLanternMesh);
	LanternLight->SetLightColor(FLinearColor(1.0f, 0.78f, 0.36f, 1.0f));
	LanternLight->SetIntensity(2200.0f);
	LanternLight->SetAttenuationRadius(1100.0f);
	LanternLight->SetCastShadows(false);
}

void AKaelCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (FollowCamera && ViewCamera)
	{
		FollowCamera->SetCameraComponent(ViewCamera);
		FollowCamera->YawDeg = GetActorRotation().Yaw;
	}

	UpdateEquipmentAttachments();
	EnsureDefaultEnhancedInputAssets();
	RegisterInputMappingContext();
}

void AKaelCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	EnsureDefaultEnhancedInputAssets();
	RegisterInputMappingContext();
}

void AKaelCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AKaelCharacter, ReplicatedOnlinePose, COND_SkipOwner);
}

UKaelMovementComponent* AKaelCharacter::GetKaelMovement() const
{
	return Cast<UKaelMovementComponent>(GetCharacterMovement());
}

FVector AKaelCharacter::GetFeetWorldLocation() const
{
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : UKaelMovementComponent::kCapsuleHalfHeightCm;
	return GetActorLocation() - FVector(0.0f, 0.0f, HalfH);
}

void AKaelCharacter::SetRightHandLanternMode(bool bInRightHandMode)
{
	bRightHandLanternMode = bInRightHandMode;
	UpdateEquipmentAttachments();
}

void AKaelCharacter::UpdateEquipmentAttachments()
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh)
	{
		return;
	}

	if (KatanaMesh)
	{
		KatanaMesh->AttachToComponent(SkelMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, KatanaSocketName);
	}

	if (SpiritLanternMesh)
	{
		if (bRightHandLanternMode)
		{
			SpiritLanternMesh->AttachToComponent(SkelMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, RightHandLanternSocketName);
			SpiritLanternMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -12.0f));
			SpiritLanternMesh->SetRelativeRotation(FRotator::ZeroRotator);
			SpiritLanternMesh->SetRelativeScale3D(FVector(1.0f));
		}
		else
		{
			SpiritLanternMesh->AttachToComponent(SkelMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, HipLanternSocketName);
			SpiritLanternMesh->SetRelativeLocation(FVector(-5.5f, 15.5f, -5.5f));
			SpiritLanternMesh->SetRelativeRotation(FRotator(3.44f, -12.6f, -5.73f));
			SpiritLanternMesh->SetRelativeScale3D(FVector(0.55f));
		}
	}
}

void AKaelCharacter::TeleportAndSettle(FVector Location, float YawDeg)
{
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : UKaelMovementComponent::kCapsuleHalfHeightCm;
	const FVector CapsuleCenter = Location + FVector(0.0f, 0.0f, HalfH + 2.0f);
	SetActorLocationAndRotation(CapsuleCenter, FRotator(0.0f, YawDeg, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);

	if (UKaelMovementComponent* KaelMove = GetKaelMovement())
	{
		KaelMove->ResetMovementState();
	}
	if (CombatComponent)
	{
		CombatComponent->ClearFocus();
	}
	if (FollowCamera)
	{
		FollowCamera->YawDeg = YawDeg;
		FollowCamera->ResetPivot();
	}
	if (BladeTrailComponent)
	{
		BladeTrailComponent->ClearTrail();
	}
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UKaelAnimInstance* KaelAnim = Cast<UKaelAnimInstance>(SkelMesh->GetAnimInstance()))
		{
			KaelAnim->ResetFootIK();
		}
	}
}

void AKaelCharacter::SetDrivingState(bool bInDriving, APawn* VehiclePawn)
{
	bIsDriving = bInDriving;
	DrivenVehiclePawn = bInDriving ? VehiclePawn : nullptr;

	// Hide figure & equipment while keeping camera active (main.js line 272)
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetVisibility(!bInDriving, true);
	}
	if (BladeTrailComponent)
	{
		if (bInDriving)
		{
			BladeTrailComponent->ClearTrail();
		}
		BladeTrailComponent->SetVisibility(!bInDriving);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bInDriving ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}

	if (UKaelMovementComponent* KaelMove = GetKaelMovement())
	{
		if (bInDriving)
		{
			KaelMove->ResetMovementState();
			KaelMove->DisableMovement();
		}
		else
		{
			KaelMove->SetMovementMode(MOVE_Walking);
		}
	}

	if (CombatComponent && bInDriving)
	{
		CombatComponent->ClearFocus();
	}

	if (FollowCamera)
	{
		if (bInDriving)
		{
			FollowCamera->OnEnterVehicle(VehiclePawn);
		}
		else
		{
			FollowCamera->OnExitVehicle();
		}
	}
}

void AKaelCharacter::EnsureDefaultEnhancedInputAssets()
{
	auto EnsureAction = [this](TObjectPtr<UInputAction>& Slot, const TCHAR* Name, EInputActionValueType ValueType)
	{
		if (!Slot)
		{
			UInputAction* NewAction = NewObject<UInputAction>(this, Name);
			NewAction->ValueType = ValueType;
			Slot = NewAction;
		}
	};

	EnsureAction(IA_Move, TEXT("IA_Move_Runtime"), EInputActionValueType::Axis2D);
	EnsureAction(IA_Look, TEXT("IA_Look_Runtime"), EInputActionValueType::Axis2D);
	EnsureAction(IA_Zoom, TEXT("IA_Zoom_Runtime"), EInputActionValueType::Axis1D);
	EnsureAction(IA_Jump, TEXT("IA_Jump_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Sprint, TEXT("IA_Sprint_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_WalkToggle, TEXT("IA_WalkToggle_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Attack, TEXT("IA_Attack_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Heavy, TEXT("IA_Heavy_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Spark, TEXT("IA_Spark_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Dash, TEXT("IA_Dash_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Flare, TEXT("IA_Flare_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Whirl, TEXT("IA_Whirl_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Guard, TEXT("IA_Guard_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Evade, TEXT("IA_Evade_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_LockTarget, TEXT("IA_LockTarget_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_NextTarget, TEXT("IA_NextTarget_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Ride, TEXT("IA_Ride_Runtime"), EInputActionValueType::Boolean);
	EnsureAction(IA_Pause, TEXT("IA_Pause_Runtime"), EInputActionValueType::Boolean);

	if (DefaultMappingContext)
	{
		return;
	}

	// Build runtime fallback Enhanced Input Mapping Context matching legacy-web/src/figure/input.js 1:1
	UInputMappingContext* Ctx = NewObject<UInputMappingContext>(this, TEXT("IMC_KomorebiDefault"));
	auto AddSwizzle = [&](FEnhancedActionKeyMapping& Map)
	{
		UInputModifierSwizzleAxis* Swiz = NewObject<UInputModifierSwizzleAxis>(Ctx);
		Swiz->Order = EInputAxisSwizzle::YXZ;
		Map.Modifiers.Add(Swiz);
	};
	auto AddNegate = [&](FEnhancedActionKeyMapping& Map)
	{
		Map.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
	};

	// Movement: WASD + Arrows + Gamepad Left Stick (X = right, Y = forward)
	Ctx->MapKey(IA_Move, EKeys::D);
	Ctx->MapKey(IA_Move, EKeys::Right);
	{
		FEnhancedActionKeyMapping& MapA = Ctx->MapKey(IA_Move, EKeys::A);
		AddNegate(MapA);
		FEnhancedActionKeyMapping& MapL = Ctx->MapKey(IA_Move, EKeys::Left);
		AddNegate(MapL);
	}
	{
		FEnhancedActionKeyMapping& MapW = Ctx->MapKey(IA_Move, EKeys::W);
		AddSwizzle(MapW);
		FEnhancedActionKeyMapping& MapUp = Ctx->MapKey(IA_Move, EKeys::Up);
		AddSwizzle(MapUp);
	}
	{
		FEnhancedActionKeyMapping& MapS = Ctx->MapKey(IA_Move, EKeys::S);
		AddSwizzle(MapS);
		AddNegate(MapS);
		FEnhancedActionKeyMapping& MapDn = Ctx->MapKey(IA_Move, EKeys::Down);
		AddSwizzle(MapDn);
		AddNegate(MapDn);
	}
	Ctx->MapKey(IA_Move, EKeys::Gamepad_Left2D);

	// Look & Zoom
	Ctx->MapKey(IA_Look, EKeys::Mouse2D);
	Ctx->MapKey(IA_Look, EKeys::Gamepad_Right2D);
	Ctx->MapKey(IA_Zoom, EKeys::MouseWheelAxis);
	Ctx->MapKey(IA_Zoom, EKeys::Gamepad_DPad_Down);
	{
		FEnhancedActionKeyMapping& MapZoomIn = Ctx->MapKey(IA_Zoom, EKeys::Gamepad_DPad_Up);
		AddNegate(MapZoomIn);
	}

	// Gait & Jump
	Ctx->MapKey(IA_Jump, EKeys::SpaceBar);
	Ctx->MapKey(IA_Jump, EKeys::Gamepad_FaceButton_Bottom); // A
	Ctx->MapKey(IA_Sprint, EKeys::LeftShift);
	Ctx->MapKey(IA_Sprint, EKeys::RightShift);
	Ctx->MapKey(IA_Sprint, EKeys::Gamepad_RightShoulder); // RB
	Ctx->MapKey(IA_Sprint, EKeys::Gamepad_LeftThumbstick); // L3
	Ctx->MapKey(IA_WalkToggle, EKeys::CapsLock);
	Ctx->MapKey(IA_WalkToggle, EKeys::LeftAlt);

	// Combat, Powers, Target Lock, Vehicle, Pause (matching input.js keyboard & gamepad buttons)
	Ctx->MapKey(IA_Attack, EKeys::LeftMouseButton);
	Ctx->MapKey(IA_Attack, EKeys::J);
	Ctx->MapKey(IA_Attack, EKeys::Gamepad_FaceButton_Left); // X
	Ctx->MapKey(IA_Heavy, EKeys::F);
	Ctx->MapKey(IA_Heavy, EKeys::Gamepad_LeftShoulder); // LB
	Ctx->MapKey(IA_Spark, EKeys::Q);
	Ctx->MapKey(IA_Spark, EKeys::Gamepad_FaceButton_Top); // Y
	Ctx->MapKey(IA_Dash, EKeys::E);
	Ctx->MapKey(IA_Dash, EKeys::Gamepad_FaceButton_Right); // B
	Ctx->MapKey(IA_Flare, EKeys::R);
	Ctx->MapKey(IA_Flare, EKeys::Gamepad_Special_Right); // Start
	Ctx->MapKey(IA_Whirl, EKeys::C);
	Ctx->MapKey(IA_Whirl, EKeys::Gamepad_RightThumbstick); // R3
	Ctx->MapKey(IA_Guard, EKeys::Z);
	Ctx->MapKey(IA_Guard, EKeys::Gamepad_LeftTrigger); // LT
	Ctx->MapKey(IA_Evade, EKeys::X);
	Ctx->MapKey(IA_Evade, EKeys::Gamepad_RightTrigger); // RT
	Ctx->MapKey(IA_LockTarget, EKeys::T);
	Ctx->MapKey(IA_LockTarget, EKeys::Gamepad_DPad_Left);
	Ctx->MapKey(IA_NextTarget, EKeys::Y);
	Ctx->MapKey(IA_NextTarget, EKeys::Gamepad_DPad_Right);
	Ctx->MapKey(IA_Ride, EKeys::G);
	Ctx->MapKey(IA_Ride, EKeys::Gamepad_Special_Left); // Back
	Ctx->MapKey(IA_Pause, EKeys::Escape);

	DefaultMappingContext = Ctx;
}

void AKaelCharacter::RegisterInputMappingContext()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !DefaultMappingContext)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		if (!InputSubsystem->HasMappingContext(DefaultMappingContext))
		{
			InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AKaelCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureDefaultEnhancedInputAssets();
	RegisterInputMappingContext();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		return;
	}

	if (IA_Move)
	{
		EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AKaelCharacter::OnMoveTriggered);
		EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &AKaelCharacter::OnMoveCompleted);
	}
	if (IA_Look)
	{
		EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AKaelCharacter::OnLookTriggered);
	}
	if (IA_Zoom)
	{
		EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &AKaelCharacter::OnZoomTriggered);
	}
	if (IA_Jump)
	{
		EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &AKaelCharacter::OnJumpStarted);
		EIC->BindAction(IA_Jump, ETriggerEvent::Completed, this, &AKaelCharacter::OnJumpCompleted);
	}
	if (IA_Sprint)
	{
		EIC->BindAction(IA_Sprint, ETriggerEvent::Started, this, &AKaelCharacter::OnSprintStarted);
		EIC->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &AKaelCharacter::OnSprintCompleted);
	}
	if (IA_WalkToggle)
	{
		EIC->BindAction(IA_WalkToggle, ETriggerEvent::Started, this, &AKaelCharacter::OnWalkToggleStarted);
	}
	if (IA_Attack)
	{
		EIC->BindAction(IA_Attack, ETriggerEvent::Started, this, &AKaelCharacter::OnAttackStarted);
	}
	if (IA_Heavy)
	{
		EIC->BindAction(IA_Heavy, ETriggerEvent::Started, this, &AKaelCharacter::OnHeavyStarted);
	}
	if (IA_Spark)
	{
		EIC->BindAction(IA_Spark, ETriggerEvent::Started, this, &AKaelCharacter::OnSparkStarted);
	}
	if (IA_Dash)
	{
		EIC->BindAction(IA_Dash, ETriggerEvent::Started, this, &AKaelCharacter::OnDashStarted);
	}
	if (IA_Flare)
	{
		EIC->BindAction(IA_Flare, ETriggerEvent::Started, this, &AKaelCharacter::OnFlareStarted);
	}
	if (IA_Whirl)
	{
		EIC->BindAction(IA_Whirl, ETriggerEvent::Started, this, &AKaelCharacter::OnWhirlStarted);
	}
	if (IA_Guard)
	{
		EIC->BindAction(IA_Guard, ETriggerEvent::Started, this, &AKaelCharacter::OnGuardStarted);
		EIC->BindAction(IA_Guard, ETriggerEvent::Completed, this, &AKaelCharacter::OnGuardCompleted);
	}
	if (IA_Evade)
	{
		EIC->BindAction(IA_Evade, ETriggerEvent::Started, this, &AKaelCharacter::OnEvadeStarted);
	}
	if (IA_LockTarget)
	{
		EIC->BindAction(IA_LockTarget, ETriggerEvent::Started, this, &AKaelCharacter::OnLockTargetStarted);
	}
	if (IA_NextTarget)
	{
		EIC->BindAction(IA_NextTarget, ETriggerEvent::Started, this, &AKaelCharacter::OnNextTargetStarted);
	}
	if (IA_Ride)
	{
		EIC->BindAction(IA_Ride, ETriggerEvent::Started, this, &AKaelCharacter::OnRideStarted);
	}
	if (IA_Pause)
	{
		EIC->BindAction(IA_Pause, ETriggerEvent::Started, this, &AKaelCharacter::OnPauseStarted);
	}
}

void AKaelCharacter::OnMoveTriggered(const FInputActionValue& Value)
{
	CurrentMoveInput = Value.Get<FVector2D>();
	if (CurrentMoveInput.SizeSquared() > 1.0f)
	{
		CurrentMoveInput.Normalize();
	}
}

void AKaelCharacter::OnMoveCompleted(const FInputActionValue& Value)
{
	CurrentMoveInput = FVector2D::ZeroVector;
}

void AKaelCharacter::OnLookTriggered(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>();
	PendingLookDeltaDeg.X += Delta.X * MouseLookSensitivityDeg;
	PendingLookDeltaDeg.Y -= Delta.Y * MouseLookSensitivityDeg;
}

void AKaelCharacter::OnZoomTriggered(const FInputActionValue& Value)
{
	PendingZoomSteps -= Value.Get<float>();
}

void AKaelCharacter::OnJumpStarted(const FInputActionValue& Value)
{
	bJumpInputHeld = true;
	if (!bIsDriving && (!CombatComponent || (!CombatComponent->bGuarding && !CombatComponent->bDowned)))
	{
		if (UKaelMovementComponent* KaelMove = GetKaelMovement())
		{
			KaelMove->RequestJumpBuffered();
		}
	}
}

void AKaelCharacter::OnJumpCompleted(const FInputActionValue& Value)
{
	bJumpInputHeld = false;
	if (UKaelMovementComponent* KaelMove = GetKaelMovement())
	{
		KaelMove->SetJumpHeld(false);
	}
}

void AKaelCharacter::OnSprintStarted(const FInputActionValue& Value) { bSprintInputHeld = true; }
void AKaelCharacter::OnSprintCompleted(const FInputActionValue& Value) { bSprintInputHeld = false; }
void AKaelCharacter::OnWalkToggleStarted(const FInputActionValue& Value) { bWalkToggled = !bWalkToggled; }
void AKaelCharacter::OnAttackStarted(const FInputActionValue& Value) { PendingCombatInput.bAttackPressed = true; }
void AKaelCharacter::OnHeavyStarted(const FInputActionValue& Value) { PendingCombatInput.bHeavyPressed = true; }
void AKaelCharacter::OnSparkStarted(const FInputActionValue& Value) { PendingCombatInput.bSparkPressed = true; }
void AKaelCharacter::OnDashStarted(const FInputActionValue& Value) { PendingCombatInput.bDashPressed = true; }
void AKaelCharacter::OnFlareStarted(const FInputActionValue& Value) { PendingCombatInput.bFlarePressed = true; }
void AKaelCharacter::OnWhirlStarted(const FInputActionValue& Value) { PendingCombatInput.bWhirlPressed = true; }
void AKaelCharacter::OnGuardStarted(const FInputActionValue& Value) { bGuardInputHeld = true; }
void AKaelCharacter::OnGuardCompleted(const FInputActionValue& Value) { bGuardInputHeld = false; }
void AKaelCharacter::OnEvadeStarted(const FInputActionValue& Value) { PendingCombatInput.bEvadePressed = true; }
void AKaelCharacter::OnLockTargetStarted(const FInputActionValue& Value) { PendingCombatInput.bLockTargetPressed = true; }
void AKaelCharacter::OnNextTargetStarted(const FInputActionValue& Value) { PendingCombatInput.bNextTargetPressed = true; }
void AKaelCharacter::OnRideStarted(const FInputActionValue& Value) { OnRideRequested.Broadcast(this); }
void AKaelCharacter::OnPauseStarted(const FInputActionValue& Value) { OnPauseRequested.Broadcast(); }

void AKaelCharacter::UpdateAnimAndBladeTrail(float DeltaTime)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	UKaelAnimInstance* KaelAnim = SkelMesh ? Cast<UKaelAnimInstance>(SkelMesh->GetAnimInstance()) : nullptr;
	const UKaelMovementComponent* KaelMove = GetKaelMovement();

	const float SwingU = CombatComponent ? CombatComponent->SwingU : -1.0f;
	const EKomorebiAttackType AttackType = CombatComponent ? CombatComponent->ActiveAttackType : EKomorebiAttackType::Cut;
	const float HurtPose = CombatComponent ? CombatComponent->GetHurtPose() : 0.0f;
	const float CastPose = CombatComponent ? CombatComponent->GetCastPose() : 0.0f;
	const bool bGuarding = CombatComponent ? CombatComponent->bGuarding : false;
	const float GuardImpact = CombatComponent ? CombatComponent->GetGuardImpact() : 0.0f;
	const float EvadeRem = KaelMove ? KaelMove->GetEvadeTimeRemaining() : 0.0f;
	const float EvadeAng = KaelMove ? KaelMove->GetEvadeAngleRad() : 0.0f;

	if (KaelAnim)
	{
		KaelAnim->bIsDriving = bIsDriving;
		KaelAnim->bIsDowned = CombatComponent ? CombatComponent->bDowned : false;
		KaelAnim->UpdateCombatPose(DeltaTime, SwingU, AttackType, HurtPose, CastPose, bGuarding, GuardImpact, EvadeRem, EvadeAng, bRightHandLanternMode);
		if (FollowCamera)
		{
			KaelAnim->UpdateCameraLookAt(DeltaTime, FollowCamera->GetCameraForwardVector());
		}
		if (!bRightHandLanternMode && SpiritLanternMesh)
		{
			FRotator LanternRot = SpiritLanternMesh->GetRelativeRotation();
			LanternRot.Roll = KaelAnim->HipLanternSwayDeg;
			SpiritLanternMesh->SetRelativeRotation(LanternRot);
		}
	}

	if (BladeTrailComponent && !bIsDriving)
	{
		const bool bTrailActive = CombatComponent && CombatComponent->IsInAttackHitWindow();
		BladeTrailComponent->SetThemeForAttack(AttackType, false);

		FVector WorldGrip = GetActorLocation() + FVector(0.0f, 0.0f, 10.0f);
		FVector WorldBase = WorldGrip + GetActorForwardVector() * UBladeTrailComponent::kBladeBaseOffsetCm;
		FVector WorldTip = WorldGrip + GetActorForwardVector() * UBladeTrailComponent::kBladeTipOffsetCm;

		if (KatanaMesh && KatanaMesh->GetAttachParent())
		{
			const FTransform SwordXform = KatanaMesh->GetComponentTransform();
			WorldGrip = SwordXform.GetLocation();
			WorldBase = SwordXform.TransformPosition(FVector(0.0f, 0.0f, UBladeTrailComponent::kBladeBaseOffsetCm));
			WorldTip = SwordXform.TransformPosition(FVector(3.0f, 0.0f, UBladeTrailComponent::kBladeTipOffsetCm));
		}
		else if (KaelAnim)
		{
			const FTransform ActorXform = GetActorTransform();
			WorldGrip = ActorXform.TransformPosition(KaelAnim->SwordGripLocal - FVector(0.0f, 0.0f, UKaelMovementComponent::kCapsuleHalfHeightCm));
			const FVector BladeWorldDir = ActorXform.TransformVectorNoScale(KaelAnim->SwordBladeDir).GetSafeNormal();
			WorldBase = WorldGrip + BladeWorldDir * UBladeTrailComponent::kBladeBaseOffsetCm;
			WorldTip = WorldGrip + BladeWorldDir * UBladeTrailComponent::kBladeTipOffsetCm;
		}

		BladeTrailComponent->SampleBlade(
			DeltaTime,
			bTrailActive,
			SwingU,
			WorldGrip,
			WorldBase,
			WorldTip,
			AttackType == EKomorebiAttackType::Spin);
	}
}

void AKaelCharacter::UpdateOnlinePoseReplication(float RealDeltaTime)
{
	if (!CombatComponent || !CombatComponent->IsOnlineMode() || !IsLocallyControlled())
	{
		return;
	}

	OnlinePoseSyncAccum += RealDeltaTime;
	if (OnlinePoseSyncAccum < 0.05f) // 20 Hz pose stream
	{
		return;
	}
	OnlinePoseSyncAccum = 0.0f;

	const UKaelMovementComponent* KaelMove = GetKaelMovement();
	FKaelOnlinePoseState Packet;
	Packet.Location = GetActorLocation();
	Packet.YawDeg = GetActorRotation().Yaw;
	Packet.SpeedCm = GetVelocity().Size2D();
	Packet.MoveAngleRad = KaelMove ? KaelMove->GetMoveAngleRad() : 0.0f;
	Packet.Flags =
		((KaelMove && KaelMove->IsMovingOnGround()) ? 1 : 0)
		| ((KaelMove && KaelMove->IsStrafing()) ? 2 : 0)
		| (CombatComponent->bGuarding ? 4 : 0)
		| (bIsDriving ? 8 : 0)
		| (CombatComponent->bDowned ? 16 : 0)
		| (bRightHandLanternMode ? 32 : 0);
	Packet.AttackTypeByte = static_cast<uint8>(CombatComponent->ActiveAttackType);
	Packet.AttackU = CombatComponent->SwingU;
	Packet.HurtPose = CombatComponent->GetHurtPose();
	Packet.CastPose = CombatComponent->GetCastPose();

	if (HasAuthority())
	{
		ReplicatedOnlinePose = Packet;
	}
	else
	{
		ServerSyncOnlinePose(Packet);
	}
}

void AKaelCharacter::ServerSyncOnlinePose_Implementation(const FKaelOnlinePoseState& NewPose)
{
	ReplicatedOnlinePose = NewPose;
}

void AKaelCharacter::OnRep_OnlinePose()
{
	const bool bRemoteDriving = (ReplicatedOnlinePose.Flags & 8) != 0;
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetVisibility(!bRemoteDriving, true);
	}
	const bool bRemoteLanternHand = (ReplicatedOnlinePose.Flags & 32) != 0;
	if (bRemoteLanternHand != bRightHandLanternMode)
	{
		SetRightHandLanternMode(bRemoteLanternHand);
	}
}

void AKaelCharacter::ApplyRemoteOnlinePose(float DeltaTime)
{
	const float Alpha = FMath::Clamp(DeltaTime * 12.0f, 0.0f, 1.0f);
	const FVector TargetLoc(ReplicatedOnlinePose.Location);
	if (FVector::DistSquared(GetActorLocation(), TargetLoc) > 360000.0f) // > 6m snap
	{
		SetActorLocation(TargetLoc);
	}
	else
	{
		SetActorLocation(FMath::Lerp(GetActorLocation(), TargetLoc, Alpha));
	}

	FRotator Rot = GetActorRotation();
	Rot.Yaw = FMath::UnwindDegrees(Rot.Yaw + FMath::FindDeltaAngleDegrees(Rot.Yaw, ReplicatedOnlinePose.YawDeg) * Alpha);
	SetActorRotation(Rot);

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UKaelAnimInstance* KaelAnim = Cast<UKaelAnimInstance>(SkelMesh->GetAnimInstance()))
		{
			const bool bGrounded = (ReplicatedOnlinePose.Flags & 1) != 0;
			const bool bStrafing = (ReplicatedOnlinePose.Flags & 2) != 0;
			const bool bGuarding = (ReplicatedOnlinePose.Flags & 4) != 0;
			const bool bDriving = (ReplicatedOnlinePose.Flags & 8) != 0;
			const bool bDowned = (ReplicatedOnlinePose.Flags & 16) != 0;
			KaelAnim->SetExternalLocomotion(ReplicatedOnlinePose.SpeedCm, ReplicatedOnlinePose.MoveAngleRad, bGrounded, bStrafing, bDriving, bDowned);
			KaelAnim->UpdateCombatPose(
				DeltaTime,
				ReplicatedOnlinePose.AttackU,
				static_cast<EKomorebiAttackType>(ReplicatedOnlinePose.AttackTypeByte),
				ReplicatedOnlinePose.HurtPose,
				ReplicatedOnlinePose.CastPose,
				bGuarding,
				0.0f,
				0.0f,
				0.0f,
				bRightHandLanternMode);
		}
	}
}

void AKaelCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsLocallyControlled() && !HasAuthority())
	{
		ApplyRemoteOnlinePose(DeltaTime);
		return;
	}

	const float TimeDilation = FMath::Max(0.05f, UGameplayStatics::GetGlobalTimeDilation(this));
	const float RealDeltaTime = FMath::Clamp(DeltaTime / TimeDilation, 0.0f, 0.05f);
	const float ScaledDeltaTime = FMath::Clamp(DeltaTime, 0.0f, 0.05f);

	const bool bDowned = CombatComponent && CombatComponent->bDowned;
	const bool bAllowCombat = !bIsDriving && !bDowned;

	// Sync position to driven vehicle
	if (bIsDriving)
	{
		if (APawn* Vehicle = DrivenVehiclePawn.Get())
		{
			SetActorLocationAndRotation(Vehicle->GetActorLocation(), Vehicle->GetActorRotation());
			if (FollowCamera)
			{
				FollowCamera->UpdateVehicleChase(RealDeltaTime, Vehicle, PendingLookDeltaDeg.Size());
			}
		}
		else
		{
			SetDrivingState(false, nullptr);
		}
	}

	// Build combat input frame
	FKomorebiCombatFrameInput FrameInput = PendingCombatInput;
	FrameInput.MoveInput = bDowned ? FVector2D::ZeroVector : CurrentMoveInput;
	FrameInput.CameraYawDeg = FollowCamera ? FollowCamera->YawDeg : GetActorRotation().Yaw;
	FrameInput.bGuardHeld = bGuardInputHeld && !bDowned;

	// Consume one-frame pressed flags
	PendingCombatInput = FKomorebiCombatFrameInput();

	bool bForceStrafe = false;
	bool bForceWalk = false;
	bool bHasCombatAimYaw = false;
	float CombatAimYawDeg = FrameInput.CameraYawDeg;

	if (CombatComponent)
	{
		CombatComponent->ProcessPreMovementCombatInput(
			ScaledDeltaTime,
			FrameInput,
			bAllowCombat,
			bForceStrafe,
			bForceWalk,
			bHasCombatAimYaw,
			CombatAimYawDeg);
	}

	// Drive UKaelMovementComponent when on foot
	if (!bIsDriving && !bDowned)
	{
		if (UKaelMovementComponent* KaelMove = GetKaelMovement())
		{
			const float RefYawDeg = bHasCombatAimYaw ? CombatAimYawDeg : FrameInput.CameraYawDeg;
			const float YawRad = FMath::DegreesToRadians(RefYawDeg);
			const FVector CamFwd(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.0f);
			const FVector CamRight(-FMath::Sin(YawRad), FMath::Cos(YawRad), 0.0f);

			const FVector DesiredWorldDir = CamFwd * FrameInput.MoveInput.Y + CamRight * FrameInput.MoveInput.X;
			const float InputMag = FMath::Clamp(FrameInput.MoveInput.Size(), 0.0f, 1.0f);

			KaelMove->SetGaitInputs(
				bWalkToggled || bForceWalk,
				bSprintInputHeld && !bForceStrafe && !bForceWalk,
				bForceStrafe,
				CombatComponent && CombatComponent->bGuarding,
				InputMag,
				DesiredWorldDir,
				RefYawDeg,
				bHasCombatAimYaw);

			if (InputMag > 0.05f)
			{
				AddMovementInput(DesiredWorldDir.GetSafeNormal(), InputMag);
			}

			// Footstep cadence (main.js lines 253-258: 2.2m stride)
			const float SpeedCm = GetVelocity().Size2D();
			if (KaelMove->IsMovingOnGround() && SpeedCm > 120.0f)
			{
				StepDistanceAccumCm += SpeedCm * ScaledDeltaTime;
				if (StepDistanceAccumCm >= 220.0f)
				{
					StepDistanceAccumCm = 0.0f;
					if (CombatComponent)
					{
						CombatComponent->OnAudioCue.Broadcast(TEXT("step"), FMath::Min(1.4f, (SpeedCm * 0.01f) / 5.0f));
					}
				}
			}
			else if (KaelMove->IsMovingOnGround())
			{
				StepDistanceAccumCm = FMath::Min(StepDistanceAccumCm, 150.0f);
			}
		}
	}

	if (CombatComponent)
	{
		CombatComponent->StepCombat(ScaledDeltaTime, RealDeltaTime, FrameInput, bAllowCombat);
	}

	UpdateAnimAndBladeTrail(ScaledDeltaTime);
	UpdateOnlinePoseReplication(RealDeltaTime);

	// Update FollowCamera in real time (main.js lines 294-298)
	if (FollowCamera)
	{
		FollowCamera->SetShake(CombatComponent ? CombatComponent->GetCameraKick() : 0.0f);
		if (bHasCombatAimYaw && !FollowCamera->IsFreeViewActive())
		{
			FollowCamera->TrackYaw(CombatAimYawDeg, RealDeltaTime);
		}
		const FVector2D CamLookDelta = bHasCombatAimYaw
			? FVector2D(0.0f, PendingLookDeltaDeg.Y)
			: PendingLookDeltaDeg;
		const FVector TargetFeet = (bIsDriving && DrivenVehiclePawn.IsValid())
			? DrivenVehiclePawn->GetActorLocation()
			: GetFeetWorldLocation();

		FollowCamera->UpdateFollowCamera(RealDeltaTime, TargetFeet, CamLookDelta, PendingZoomSteps);
	}

	PendingLookDeltaDeg = FVector2D::ZeroVector;
	PendingZoomSteps = 0.0f;
}
