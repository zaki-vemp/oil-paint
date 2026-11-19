// Copyright Komorebi Project. Native UE5 IOnlineSession wrapper for LAN/Direct-IP/EOS 8-player Online City matchmaking.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "KomorebiGameTypes.h"
#include "OnlineSessionSubsystem.generated.h"

class FOnlineSessionSearch;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnKomorebiSessionStatusChanged, const FString& /*StatusText*/, const FString& /*RoomCode*/);

/**
 * Native UE5 Online Session subsystem replacing legacy-web/server/room.mjs with
 * OnlineSubsystemNull (LAN / Direct IP) and OnlineSubsystemEOS-ready session management.
 */
UCLASS()
class KOMOREBIGAME_API UOnlineSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Hosts an 8-player Online City Listen Server session for the requested scene pool.
	 * Public rooms use `pub-xxxxxx` and advertise for QuickMatch; private rooms are invite-code only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Online")
	void HostOnlineCity(bool bPrivate, const FString& RoomCode, EKomorebiWorldScene Scene);

	/**
	 * Searches open public rooms matching the scene pool and joins the fullest room with space.
	 * If no open room is found, opens a new public Listen Server room automatically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Online")
	void QuickMatch(EKomorebiWorldScene Scene);

	/** Direct IP/hostname travel (appends :7777 if no port is specified). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Online")
	void JoinByAddress(const FString& Address);

	/** Destroys the active session and clears cached room metadata. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Online")
	void LeaveSession();

	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FString GetCurrentRoomCode() const { return CurrentRoomCode; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FString GetStatusText() const { return StatusText; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	bool IsSessionActive() const { return bSessionActive; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	bool IsPrivateRoom() const { return bCurrentRoomPrivate; }

	/** Broadcast whenever matchmaking, hosting, joining, or disconnecting updates status. */
	FOnKomorebiSessionStatusChanged OnSessionStatusChanged;

private:
	IOnlineSessionPtr GetSessionInterface() const;
	FString GenerateRoomCode(bool bPrivate) const;
	void SetStatus(const FString& NewStatus);
	void CreateSessionInternal();

	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	FDelegateHandle CreateSessionHandle;
	FDelegateHandle FindSessionsHandle;
	FDelegateHandle JoinSessionHandle;
	FDelegateHandle DestroySessionHandle;

	TSharedPtr<FOnlineSessionSearch> LastSessionSearch;

	FString CurrentRoomCode;
	FString StatusText = TEXT("Offline");
	EKomorebiWorldScene PendingScene = EKomorebiWorldScene::NeoKyoto;
	bool bCurrentRoomPrivate = false;
	bool bSessionActive = false;
	bool bPendingHostAfterDestroy = false;
};
