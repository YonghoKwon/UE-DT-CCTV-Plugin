#include "DTCctvPacketQueue.h"

#include "DTCctvFFmpegInclude.h"

FDTCctvPacketQueue::FDTCctvPacketQueue(int32 InMaxPackets)
	: MaxPackets(FMath::Max(1, InMaxPackets))
{
}

FDTCctvPacketQueue::~FDTCctvPacketQueue()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	ClearLocked(false);
}

void FDTCctvPacketQueue::ClearLocked(bool bCountAsDropped)
{
	if (bCountAsDropped)
	{
		DroppedPackets += Packets.Num();
	}
	while (!Packets.IsEmpty())
	{
		AVPacket* Packet = Packets.First();
		Packets.PopFirst();
		av_packet_free(&Packet);
	}
}

void FDTCctvPacketQueue::Push(AVPacket* Source)
{
	const bool bKeyFrame = (Source->flags & AV_PKT_FLAG_KEY) != 0;
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (bAborted)
		{
			av_packet_unref(Source);
			return;
		}

		if (Packets.Num() >= MaxPackets)
		{
			ClearLocked(true);
			bSkipUntilKeyFrame = true;
		}

		if (bSkipUntilKeyFrame)
		{
			if (!bKeyFrame)
			{
				++DroppedPackets;
				av_packet_unref(Source);
				return;
			}
			bSkipUntilKeyFrame = false;
			bDiscontinuity = true;
		}

		AVPacket* Packet = av_packet_alloc();
		if (Packet == nullptr)
		{
			av_packet_unref(Source);
			return;
		}
		av_packet_move_ref(Packet, Source);
		Packets.EmplaceLast(Packet);
	}
	Condition.notify_one();
}

bool FDTCctvPacketQueue::Pop(AVPacket*& OutPacket, bool& bOutDiscontinuity)
{
	std::unique_lock<std::mutex> Lock(Mutex);
	Condition.wait(Lock, [this]() { return bAborted || !Packets.IsEmpty(); });
	if (bAborted)
	{
		return false;
	}
	OutPacket = Packets.First();
	Packets.PopFirst();
	bOutDiscontinuity = bDiscontinuity;
	bDiscontinuity = false;
	return true;
}

void FDTCctvPacketQueue::Abort()
{
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		bAborted = true;
	}
	Condition.notify_all();
}

void FDTCctvPacketQueue::Reset()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	ClearLocked(false);
	bAborted = false;
	bSkipUntilKeyFrame = false;
	bDiscontinuity = false;
}

int32 FDTCctvPacketQueue::Num() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return Packets.Num();
}

int64 FDTCctvPacketQueue::GetDroppedPackets() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return DroppedPackets;
}
