// GrenadeActor.cpp

#include "GrenadeActor.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"

AGrenadeActor::AGrenadeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// ── Collision sphere (root) ─────────────────────────────────
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(10.0f);
	CollisionSphere->SetCollisionProfileName(TEXT("Projectile"));
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetNotifyRigidBodyCollision(true); // generates Hit events
	RootComponent = CollisionSphere;

	// ── Grenade mesh ────────────────────────────────────────────
	GrenadeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrenadeMesh"));
	GrenadeMesh->SetupAttachment(CollisionSphere);
	GrenadeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// ── Projectile movement ─────────────────────────────────────
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(CollisionSphere);
	ProjectileMovement->InitialSpeed = ThrowSpeed;
	ProjectileMovement->MaxSpeed = ThrowSpeed;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.3f;
	ProjectileMovement->Friction = 0.3f;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
}

void AGrenadeActor::BeginPlay()
{
	Super::BeginPlay();

	// Bind impact callback (used by Impact mode, harmless if Timer mode).
	CollisionSphere->OnComponentHit.AddDynamic(this, &AGrenadeActor::OnHit);

	// Start fuse timer if in Timer mode.
	if (DetonationMode == EGrenadeDetonationMode::Timer)
	{
		GetWorldTimerManager().SetTimer(
			FuseTimerHandle, this, &AGrenadeActor::Detonate, FuseTime, false);
	}
}

void AGrenadeActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// ────────────────────────────────────────────────────────────────
// Impact callback
// ────────────────────────────────────────────────────────────────
void AGrenadeActor::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (DetonationMode == EGrenadeDetonationMode::Impact && !bHasDetonated)
	{
		Detonate();
	}
}

// ────────────────────────────────────────────────────────────────
// Detonate
// ────────────────────────────────────────────────────────────────
void AGrenadeActor::Detonate()
{
	if (bHasDetonated) return;
	bHasDetonated = true;

	// Clear any pending fuse timer.
	GetWorldTimerManager().ClearTimer(FuseTimerHandle);

	// Apply physics impulse to qualifying pawns (populates AffectedPawns).
	ApplyBlastPhysics();

	// Apply radial damage through Unreal's damage system.
	TArray<AActor*> IgnoredActors;
	IgnoredActors.Add(this);
	UGameplayStatics::ApplyRadialDamageWithFalloff(
		this,
		BaseDamage,
		BaseDamage * 0.1f,		// MinimumDamage at edge
		GetActorLocation(),
		BlastRadius * 0.25f,	// DamageInnerRadius (full damage zone)
		BlastRadius,			// DamageOuterRadius
		DamageFalloff,
		nullptr,				// DamageTypeClass
		IgnoredActors,
		this,					// DamageCauser
		GetInstigatorController()
	);

	// Broadcast delegate with the affected pawns list.
	{
		TArray<APawn*> RawPtrs;
		RawPtrs.Reserve(AffectedPawns.Num());
		for (const TObjectPtr<APawn>& P : AffectedPawns)
		{
			RawPtrs.Add(P.Get());
		}
		OnGrenadeDetonated.Broadcast(this, RawPtrs);
	}

	// Blueprint event for VFX / SFX.
	OnDetonate();

	// Destroy grenade actor after a short delay (lets particles spawn).
	SetLifeSpan(0.1f);
}

// ────────────────────────────────────────────────────────────────
// Blast physics — enable ragdoll & apply impulse
// ────────────────────────────────────────────────────────────────
TArray<APawn*> AGrenadeActor::GetAffectedPawns() const
{
	TArray<APawn*> Result;
	Result.Reserve(AffectedPawns.Num());
	for (const TObjectPtr<APawn>& P : AffectedPawns)
	{
		Result.Add(P.Get());
	}
	return Result;
}

void AGrenadeActor::ApplyBlastPhysics()
{
	AffectedPawns.Empty();
	const FVector Origin = GetActorLocation();

	// Overlap sphere to find nearby actors.
	TArray<FOverlapResult> Overlaps;
	FCollisionShape Shape = FCollisionShape::MakeSphere(BlastRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->OverlapMultiByChannel(
		Overlaps, Origin, FQuat::Identity, ECC_Pawn, Shape, QueryParams);

	TSet<APawn*> AlreadyProcessed;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Pawn = Cast<APawn>(Overlap.GetActor());
		if (!Pawn || AlreadyProcessed.Contains(Pawn))
		{
			continue;
		}

		// ── Filter by class list (empty list = affect all) ──────
		if (AffectedPawnClasses.Num() > 0)
		{
			bool bIsAllowed = false;
			for (const TSubclassOf<APawn>& AllowedClass : AffectedPawnClasses)
			{
				if (AllowedClass && Pawn->IsA(AllowedClass))
				{
					bIsAllowed = true;
					break;
				}
			}
			if (!bIsAllowed)
			{
				continue;
			}
		}

		AlreadyProcessed.Add(Pawn);
		AffectedPawns.Add(Pawn);

		// ── Enable physics / ragdoll on the skeletal mesh ───────
		ACharacter* Character = Cast<ACharacter>(Pawn);
		USkeletalMeshComponent* SkelMesh = nullptr;

		if (Character)
		{
			SkelMesh = Character->GetMesh();
		}
		else
		{
			SkelMesh = Pawn->FindComponentByClass<USkeletalMeshComponent>();
		}

		if (SkelMesh)
		{
			// Enable full ragdoll physics.
			SkelMesh->SetSimulatePhysics(true);
			SkelMesh->SetAllBodiesSimulatePhysics(true);
			SkelMesh->SetCollisionProfileName(TEXT("Ragdoll"));
			SkelMesh->WakeAllRigidBodies();

			// Detach from capsule so the ragdoll isn't constrained.
			if (Character)
			{
				if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(Character->GetRootComponent()))
				{
					Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				Character->GetCharacterMovement()->DisableMovement();
			}

			// Calculate impulse direction (outward from blast centre).
			FVector ImpulseDir = (SkelMesh->GetComponentLocation() - Origin);
			if (!ImpulseDir.IsNearlyZero())
			{
				ImpulseDir.Normalize();
			}
			else
			{
				ImpulseDir = FVector::UpVector;
			}

			// Add upward bias so pawns fly up, not just sideways.
			ImpulseDir += FVector(0.0f, 0.0f, 0.4f);
			ImpulseDir.Normalize();

			// Scale impulse with distance falloff.
			const float Distance = FVector::Dist(Origin, SkelMesh->GetComponentLocation());
			const float Alpha = FMath::Clamp(1.0f - (Distance / BlastRadius), 0.0f, 1.0f);
			const float FinalImpulse = BlastImpulse * Alpha;

			SkelMesh->AddImpulse(ImpulseDir * FinalImpulse, NAME_None, true);
		}
	}
}
