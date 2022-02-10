#pragma once 

#include "header.h" 
#include "blob.h" 

#include <ydb/core/tablet/tablet_counters.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/persqueue/events/internal.h>
 
namespace NKikimr { 
namespace NPQ { 
 
struct TUserInfo; 
 
struct TReadAnswer {
    ui64 Size;
    THolder<IEventBase> Event;
};

struct TReadInfo { 
    TString User;
    TString ClientDC; 
    ui64 Offset; 
    ui16 PartNo; 
    ui32 Count; 
    ui32 Size; 
    ui64 Destination; 
    TInstant Timestamp; 
    ui64 ReadTimestampMs;
    TDuration WaitQuotaTime;
 
    bool IsSubscription; 
 
    TVector<TRequestedBlob> Blobs; //offset, count, value
    ui64 CachedOffset; //offset of head can be bigger than last databody offset 
    TVector<TClientBlob> Cached; //records from head
 
    TReadInfo() = delete; 
    TReadInfo(
        const TString& user,
        const TString& clientDC,
        const ui64 offset,
        const ui16 partNo,
        const ui64 count,
        const ui32 size,
        const ui64 dst,
        ui64 readTimestampMs,
        TDuration waitQuotaTime
    )
        : User(user) 
        , ClientDC(clientDC) 
        , Offset(offset) 
        , PartNo(partNo) 
        , Count(count) 
        , Size(size) 
        , Destination(dst) 
        , Timestamp(TAppData::TimeProvider->Now()) 
        , ReadTimestampMs(readTimestampMs)
        , WaitQuotaTime(waitQuotaTime)
        , IsSubscription(false) 
        , CachedOffset(0) 
    {} 
 
    TReadAnswer FormAnswer(
        const TActorContext& ctx,
        const TEvPQ::TEvBlobResponse& response,
        const ui64 endOffset,
        const ui32 partition,
        TUserInfo* ui,
        const ui64 dst,
        const ui64 sizeLag
    );

    TReadAnswer FormAnswer(
        const TActorContext& ctx,
        const ui64 endOffset,
        const ui32 partition,
        TUserInfo* ui,
        const ui64 dst,
        const ui64 sizeLag
    ) {
        TEvPQ::TEvBlobResponse response(0, TVector<TRequestedBlob>());
        return FormAnswer(ctx, response, endOffset, partition, ui, dst, sizeLag); 
    } 
}; 
 
struct TOffsetCookie { 
    ui64 Offset; 
    ui64 Cookie; 
}; 
 
 
class TSubscriberLogic : public TNonCopyable { 
public: 
    TSubscriberLogic(); 
 
    //will store and return cookie 
    void AddSubscription(TReadInfo&& info, const ui64 cookie); 
 
    //forget on timeout 
    TMaybe<TReadInfo> ForgetSubscription(const ui64 cookie); 
 
    //form TReadInfo::Cached with new data and return ready reads 
    TVector<std::pair<TReadInfo, ui64>> CompleteSubscriptions(const ui64 endOffset);
private: 
    THashMap<ui64, TReadInfo> ReadInfo;    // cookie -> {...}
    std::deque<TOffsetCookie> WaitingReads; 
}; 
 
 
class TSubscriber : public TNonCopyable { 
public: 
    TSubscriber(const ui32 partition, TTabletCountersBase& counters, const TActorId& tablet);
 
    //will wait for new data or timeout for this read and set timer for timeout ms 
    void AddSubscription(TReadInfo&& info, const ui32 timeout, const ui64 cookie, const TActorContext& ctx); 
 
    //handle of timeout for some read 
    TMaybe<TReadInfo> OnTimeout(TEvPQ::TEvReadTimeout::TPtr& ev);
 
    //get completed subscriptions 
    TVector<std::pair<TReadInfo, ui64>> GetReads(const ui64 endOffsets);
 
private: 
    TSubscriberLogic Subscriber; 
    const ui32 Partition; 
    TTabletCountersBase& Counters; 
    TActorId Tablet;
}; 
 
 
 
 
}// NPQ 
}// NKikimr 
