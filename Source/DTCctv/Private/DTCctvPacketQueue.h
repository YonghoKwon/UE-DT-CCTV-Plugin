#pragma once

#include "CoreMinimal.h"
#include "Containers/Deque.h"
#include <condition_variable>
#include <mutex>

struct AVPacket;
struct FDTCctvStreamStats;

/**
 * 읽기 스레드와 디코딩 스레드 사이의 상한 있는 패킷 큐.
 * 상한을 넘으면 비우고 다음 키프레임부터 다시 받아, 참조가 빠진 프레임을 디코더에 넣지 않는다.
 */
class FDTCctvPacketQueue
{
public:
	explicit FDTCctvPacketQueue(int32 InMaxPackets);
	~FDTCctvPacketQueue();

	/** 패킷 내용을 가져간다. Source는 빈 패킷이 된다. */
	void Push(AVPacket* Source);

	/**
	 * 패킷이 올 때까지 기다린다. Abort되면 false를 돌려준다.
	 * @param OutPacket 호출자가 av_packet_free로 해제한다.
	 * @param bOutDiscontinuity 패킷을 버린 직후이므로 디코더를 비워야 한다.
	 */
	bool Pop(AVPacket*& OutPacket, bool& bOutDiscontinuity);

	void Abort();
	/** Abort 상태를 풀고 내용을 비운다. 새 연결을 시작할 때 호출한다. */
	void Reset();

	int32 Num() const;
	int64 GetDroppedPackets() const;
	void FillStats(FDTCctvStreamStats& Stats) const;

private:
	void ClearLocked(bool bCountAsDropped);

	mutable std::mutex Mutex;
	std::condition_variable Condition;
	TDeque<AVPacket*> Packets;
	int32 MaxPackets;
	int64 DroppedPackets = 0;
	int32 PeakPackets = 0;
	int64 KeyWaits = 0;
	double KeyWaitStarted = 0.0;
	double KeyWaitSeconds = 0.0;
	bool bAborted = false;
	bool bSkipUntilKeyFrame = false;
	bool bDiscontinuity = false;
};
