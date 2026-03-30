// GrenadeActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GrenadeActor.generated.h"

/** Broadcast when the grenade detonates, carrying the list of affected pawns. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnGrenadeDetonated,
	AGrenadeActor*, Grenade,
	const TArray<APawn*>&, AffectedPawns);

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class URadialForceComponent;

/**
 * Determines how the grenade detonates.
 */
UENUM(BlueprintType)
enum class EGrenadeDetonationMode : uint8
{
	Impact		UMETA(DisplayName = "Impact"),
	Timer		UMETA(DisplayName = "Timer")
};

/**
 * A throwable grenade that detonates via impact or timer.
 * On detonation it enables physics on nearby pawns (filtered by class)
 * and applies a radial impulse to ragdoll them.
 */
UCLASS(Blueprintable)
class GAM305_TEAM_1_FPS_API AGrenadeActor : public AActor
{
	GENERATED_BODY()

public:
	AGrenadeActor();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// ── Components ──────────────────────────────────────────────

	/** Collision sphere – root component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Visual mesh for the grenade. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> GrenadeMesh;

	/** Handles projectile-style movement (velocity, gravity, bounce). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// ── Detonation Settings ─────────────────────────────────────

	/** How this grenade detonates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Detonation")
	EGrenadeDetonationMode DetonationMode = EGrenadeDetonationMode::Timer;

	/** Seconds before detonation when using Timer mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Detonation",
		meta = (EditCondition = "DetonationMode == EGrenadeDetonationMode::Timer", ClampMin = "0.1"))
	float FuseTime = 3.0f;

	// ── Blast Settings ──────────────────────────────────────────

	/** Radius in which pawns are affected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Blast")
	float BlastRadius = 600.0f;

	/** Impulse strength applied to affected skeletal meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Blast")
	float BlastImpulse = 2500.0f;

	/** Optional damage amount — apply however you like in your damage system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Blast")
	float BaseDamage = 100.0f;

	/** Damage falloff from centre to edge of blast (1 = linear). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Blast",
		meta = (ClampMin = "0.0"))
	float DamageFalloff = 1.0f;

	/**
	 * Pawn classes that should be affected by the blast.
	 * Leave empty to affect ALL pawns in range.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Blast")
	TArray<TSubclassOf<APawn>> AffectedPawnClasses;

	// ── Initial Velocity ────────────────────────────────────────

	/** Speed at which the grenade is thrown (set via projectile movement). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Movement")
	float ThrowSpeed = 2000.0f;

	// ── Public API ──────────────────────────────────────────────

	/** Trigger detonation manually from Blueprint or C++. */
	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void Detonate();

	/** Called on detonation — override or bind in Blueprint for VFX / SFX. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Grenade")
	void OnDetonate();

	// ── Blast Results ──────────────────────────────────────────

	/** Delegate broadcast on detonation with the list of affected pawns. */
	UPROPERTY(BlueprintAssignable, Category = "Grenade")
	FOnGrenadeDetonated OnGrenadeDetonated;

	/** Pawns that were hit by the last detonation (populated during Detonate). */
	UPROPERTY(BlueprintReadOnly, Category = "Grenade")
	TArray<TObjectPtr<APawn>> AffectedPawns;

	/** Returns the list of pawns affected by the blast. */
	UFUNCTION(BlueprintPure, Category = "Grenade")
	TArray<APawn*> GetAffectedPawns() const;

private:
	// ── Internal ────────────────────────────────────────────────

	/** Called when the collision sphere hits something (Impact mode). */
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse,
		const FHitResult& Hit);

	/** Apply radial physics impulse to qualifying pawns. */
	void ApplyBlastPhysics();

	/** Timer handle for fuse countdown. */
	FTimerHandle FuseTimerHandle;

	/** Guard against double-detonation. */
	bool bHasDetonated = false;
};
