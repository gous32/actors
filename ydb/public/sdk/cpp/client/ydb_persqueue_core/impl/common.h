#pragma once

#include <ydb/public/sdk/cpp/client/ydb_persqueue_core/persqueue.h>
#include <ydb/public/sdk/cpp/client/ydb_common_client/impl/client.h>

#include <util/generic/queue.h>
#include <util/system/condvar.h>
#include <util/thread/pool.h>

#include <queue>

namespace NYdb::NPersQueue {

IRetryPolicy::ERetryErrorClass GetRetryErrorClass(EStatus status);
IRetryPolicy::ERetryErrorClass GetRetryErrorClassV2(EStatus status);

void Cancel(NGrpc::IQueueClientContextPtr& context);

NYql::TIssues MakeIssueWithSubIssues(const TString& description, const NYql::TIssues& subissues);

TString IssuesSingleLineString(const NYql::TIssues& issues); 
 
size_t CalcDataSize(const TReadSessionEvent::TEvent& event);

template <class TMessage>
bool IsErrorMessage(const TMessage& serverMessage) {
    const Ydb::StatusIds::StatusCode status = serverMessage.status();
    return status != Ydb::StatusIds::SUCCESS && status != Ydb::StatusIds::STATUS_CODE_UNSPECIFIED;
}

template <class TMessage>
TPlainStatus MakeErrorFromProto(const TMessage& serverMessage) {
    NYql::TIssues issues;
    NYql::IssuesFromMessage(serverMessage.issues(), issues);
    return TPlainStatus(static_cast<EStatus>(serverMessage.status()), std::move(issues));
}

// Gets source endpoint for the whole driver (or persqueue client)
// and endpoint that was given us by the cluster discovery service
// and gives endpoint for the current LB cluster.
// For examples see tests.
TString ApplyClusterEndpoint(TStringBuf driverEndpoint, const TString& clusterDiscoveryEndpoint);

// Factory for IStreamRequestReadWriteProcessor
// It is created in order to separate grpc transport logic from
// the logic of session.
// So there is grpc factory implementation to use in SDK
// and another one to use in tests for testing only session logic
// without transport stuff.
template <class TRequest, class TResponse>
struct ISessionConnectionProcessorFactory {
    using IProcessor = NGrpc::IStreamRequestReadWriteProcessor<TRequest, TResponse>;
    using TConnectedCallback = std::function<void(TPlainStatus&&, typename IProcessor::TPtr&&)>;
    using TConnectTimeoutCallback = std::function<void(bool ok)>;

    virtual ~ISessionConnectionProcessorFactory() = default;

    // Creates processor
    virtual void CreateProcessor(
        // Params for connect.
        TConnectedCallback callback,
        const TRpcRequestSettings& requestSettings,
        NGrpc::IQueueClientContextPtr connectContext,
        // Params for timeout and its cancellation.
        TDuration connectTimeout,
        NGrpc::IQueueClientContextPtr connectTimeoutContext,
        TConnectTimeoutCallback connectTimeoutCallback,
        // Params for delay before reconnect and its cancellation.
        TDuration connectDelay = TDuration::Zero(),
        NGrpc::IQueueClientContextPtr connectDelayOperationContext = nullptr) = 0;
};

template <class TService, class TRequest, class TResponse>
class TSessionConnectionProcessorFactory : public ISessionConnectionProcessorFactory<TRequest, TResponse>,
                                           public std::enable_shared_from_this<TSessionConnectionProcessorFactory<TService, TRequest, TResponse>>
{
public:
    using TConnectedCallback = typename ISessionConnectionProcessorFactory<TRequest, TResponse>::TConnectedCallback;
    using TConnectTimeoutCallback = typename ISessionConnectionProcessorFactory<TRequest, TResponse>::TConnectTimeoutCallback;
    TSessionConnectionProcessorFactory(
        TGRpcConnectionsImpl::TStreamRpc<TService, TRequest, TResponse, NGrpc::TStreamRequestReadWriteProcessor> rpc,
        std::shared_ptr<TGRpcConnectionsImpl> connections,
        TDbDriverStatePtr dbState
    )
        : Rpc(rpc)
        , Connections(std::move(connections))
        , DbDriverState(dbState)
    {
    }

    void CreateProcessor(
        TConnectedCallback callback,
        const TRpcRequestSettings& requestSettings,
        NGrpc::IQueueClientContextPtr connectContext,
        TDuration connectTimeout,
        NGrpc::IQueueClientContextPtr connectTimeoutContext,
        TConnectTimeoutCallback connectTimeoutCallback,
        TDuration connectDelay,
        NGrpc::IQueueClientContextPtr connectDelayOperationContext) override
    {
        Y_ASSERT(connectContext);
        Y_ASSERT(connectTimeoutContext);
        Y_ASSERT((connectDelay == TDuration::Zero()) == !connectDelayOperationContext);
        if (connectDelay == TDuration::Zero()) {
            Connect(std::move(callback),
                    requestSettings,
                    std::move(connectContext),
                    connectTimeout,
                    std::move(connectTimeoutContext),
                    std::move(connectTimeoutCallback));
        } else {
            auto connect = [
                weakThis = this->weak_from_this(),
                callback = std::move(callback),
                requestSettings,
                connectContext = std::move(connectContext),
                connectTimeout,
                connectTimeoutContext = std::move(connectTimeoutContext),
                connectTimeoutCallback = std::move(connectTimeoutCallback)
            ] (bool ok)
            {
                if (!ok) {
                    return;
                }

                if (auto sharedThis = weakThis.lock()) {
                    sharedThis->Connect(
                        std::move(callback),
                        requestSettings,
                        std::move(connectContext),
                        connectTimeout,
                        std::move(connectTimeoutContext),
                        std::move(connectTimeoutCallback)
                    );
                }
            };

            Connections->ScheduleCallback(
                connectDelay,
                std::move(connect),
                std::move(connectDelayOperationContext)
            );
        }
    }

private:
    void Connect(
        TConnectedCallback callback,
        const TRpcRequestSettings& requestSettings,
        NGrpc::IQueueClientContextPtr connectContext,
        TDuration connectTimeout,
        NGrpc::IQueueClientContextPtr connectTimeoutContext,
        TConnectTimeoutCallback connectTimeoutCallback)
    {
        Connections->StartBidirectionalStream<TService, TRequest, TResponse>(
            std::move(callback),
            Rpc,
            DbDriverState,
            requestSettings,
            std::move(connectContext)
        );

        Connections->ScheduleCallback(
            connectTimeout,
            std::move(connectTimeoutCallback),
            std::move(connectTimeoutContext)
        );
    }

private:
    TGRpcConnectionsImpl::TStreamRpc<TService, TRequest, TResponse, NGrpc::TStreamRequestReadWriteProcessor> Rpc;
    std::shared_ptr<TGRpcConnectionsImpl> Connections;
    TDbDriverStatePtr DbDriverState;
};

template <class TService, class TRequest, class TResponse>
std::shared_ptr<ISessionConnectionProcessorFactory<TRequest, TResponse>>
    CreateConnectionProcessorFactory(
        TGRpcConnectionsImpl::TStreamRpc<TService, TRequest, TResponse, NGrpc::TStreamRequestReadWriteProcessor> rpc,
        std::shared_ptr<TGRpcConnectionsImpl> connections,
        TDbDriverStatePtr dbState
    )
{
    return std::make_shared<TSessionConnectionProcessorFactory<TService, TRequest, TResponse>>(rpc, std::move(connections), std::move(dbState));
}

 
 
template <class TEvent_>
struct TBaseEventInfo {
    using TEvent = TEvent_;

    TEvent Event;

    TEvent& GetEvent() {
        return Event;
    }

    void OnUserRetrievedEvent() {
    }

    template <class T>
    TBaseEventInfo(T&& event)
        : Event(std::forward<T>(event))
    {}
};

 
class ISignalable { 
public: 
    ISignalable() = default; 
    virtual ~ISignalable() {} 
    virtual void Signal() = 0; 
}; 
 
// Waiter on queue. 
// Future or GetEvent call 
class TWaiter { 
public: 
    TWaiter() = default; 
 
    TWaiter(const TWaiter&) = delete; 
    TWaiter& operator=(const TWaiter&) = delete; 
    TWaiter(TWaiter&&) = default; 
    TWaiter& operator=(TWaiter&&) = default; 
 
    TWaiter(NThreading::TPromise<void>&& promise, ISignalable* self) 
        : Promise(promise) 
        , Future(promise.Initialized() ? Promise.GetFuture() : NThreading::TFuture<void>()) 
        , Self(self) 
    { 
    } 
 
    void Signal() { 
        if (Self) { 
            Self->Signal(); 
        } 
        if (Promise.Initialized() && !Promise.HasValue()) { 
            Promise.SetValue(); 
        } 
    } 
 
    bool Valid() const { 
        if (!Future.Initialized()) return false; 
        return !Promise.Initialized() || Promise.GetFuture().StateId() == Future.StateId(); 
    } 
 
    NThreading::TPromise<void> ExtractPromise() { 
        NThreading::TPromise<void> promise; 
        Y_VERIFY(!promise.Initialized()); 
        std::swap(Promise, promise); 
        return promise; 
    } 
 
    NThreading::TFuture<void> GetFuture() { 
        Y_VERIFY(Future.Initialized()); 
        return Future; 
    } 
 
private: 
    NThreading::TPromise<void> Promise; 
    NThreading::TFuture<void> Future; 
    ISignalable* Self = nullptr; 
}; 
 
 
 
// Class that is responsible for:
// - events queue;
// - signalling futures that wait for events;
// - packing events for waiters;
// - waking up waiters.
// Thread safe.
template <class TSettings_, class TEvent_, class TEventInfo_ = TBaseEventInfo<TEvent_>>
class TBaseSessionEventsQueue : public ISignalable { 
protected:
    using TSelf = TBaseSessionEventsQueue<TSettings_, TEvent_, TEventInfo_>;
    using TSettings = TSettings_;
    using TEvent = TEvent_;
    using TEventInfo = TEventInfo_;


    // Template for visitor implementation.
    struct TBaseHandlersVisitor {
        TBaseHandlersVisitor(const TSettings& settings, TEventInfo& eventInfo)
            : Settings(settings)
            , EventInfo(eventInfo)
        {}

        template <class TEventType, class TFunc, class TCommonFunc>
        bool PushHandler(TEventInfo&& eventInfo, const TFunc& specific, const TCommonFunc& common) {
            if (specific) {
                PushSpecificHandler<TEventType>(std::move(eventInfo), specific);
                return true;
            }
            if (common) {
                PushCommonHandler(std::move(eventInfo), common);
                return true;
            }
            return false;
        }

        template <class TEventType, class TFunc>
        void PushSpecificHandler(TEventInfo&& eventInfo, const TFunc& f) {
            Post(Settings.EventHandlers_.HandlersExecutor_, [func = f, event = std::move(eventInfo)]() mutable {
                event.OnUserRetrievedEvent();
                func(std::get<TEventType>(event.GetEvent()));
            });
        }

        template <class TFunc>
        void PushCommonHandler(TEventInfo&& eventInfo, const TFunc& f) {
            Post(Settings.EventHandlers_.HandlersExecutor_, [func = f, event = std::move(eventInfo)]() mutable {
                event.OnUserRetrievedEvent();
                func(event.GetEvent());
            });
        }

        virtual void Post(const IExecutor::TPtr& executor, IExecutor::TFunction&& f) { 
            executor->Post(std::move(f));
        }

        const TSettings& Settings;
        TEventInfo& EventInfo;
    };

 
public:
    TBaseSessionEventsQueue(const TSettings& settings)
        : Settings(settings)
        , Waiter(NThreading::NewPromise<void>(), this) 
    {}

    virtual ~TBaseSessionEventsQueue() = default;

 
    void Signal() override { 
        CondVar.Signal(); 
    } 
 
protected:
    virtual bool HasEventsImpl() const {  // Assumes that we're under lock.
        return !Events.empty() || CloseEvent;
    }

    TWaiter PopWaiterImpl() { // Assumes that we're under lock.
        TWaiter waiter(Waiter.ExtractPromise(), this); 
        return std::move(waiter); 
    }

    void WaitEventsImpl() { // Assumes that we're under lock. Posteffect: HasEventsImpl() is true.
        while (!HasEventsImpl()) {
            CondVar.WaitI(Mutex);
        }
    }

    void RenewWaiterImpl() { 
        if (Events.empty() && Waiter.GetFuture().HasValue()) { 
            Waiter = TWaiter(NThreading::NewPromise<void>(), this); 
        } 
    } 
 
public:
    NThreading::TFuture<void> WaitEvent() {
        with_lock (Mutex) {
            if (HasEventsImpl()) {
                return NThreading::MakeFuture(); // Signalled
            } else {
                Y_VERIFY(Waiter.Valid()); 
                auto res = Waiter.GetFuture(); 
                return res; 
            }
        }
    }

protected:
    const TSettings& Settings;
    TWaiter Waiter; 
    std::queue<TEventInfo> Events;
    TCondVar CondVar;
    TMutex Mutex;
    TMaybe<TSessionClosedEvent> CloseEvent;
    std::atomic<bool> Closed = false;
};

class IAsyncExecutor : public IExecutor {
private:
    virtual void PostImpl(TVector<std::function<void()>>&&) = 0;
    virtual void PostImpl(std::function<void()>&&) = 0;

public:
    bool IsAsync() const override {
        return true;
    }
    // Post Implementation MUST NOT run f before it returns
    void Post(TFunction&& f) final;
};

IExecutor::TPtr CreateDefaultExecutor();


class TThreadPoolExecutor : public IAsyncExecutor {
private:
    std::shared_ptr<IThreadPool> ThreadPool;

public:
    TThreadPoolExecutor(std::shared_ptr<IThreadPool> threadPool);
    TThreadPoolExecutor(size_t threadsCount);
    ~TThreadPoolExecutor() = default;

    bool IsAsync() const override {
        return !IsFakeThreadPool;
    }

    void DoStart() override {
        if (ThreadsCount) {
            ThreadPool->Start(ThreadsCount);
        }
    }

private:
    void PostImpl(TVector<TFunction>&& fs) override;
    void PostImpl(TFunction&& f) override;

private:
    bool IsFakeThreadPool = false;
    size_t ThreadsCount = 0;
};

class TSerialExecutor : public IAsyncExecutor, public std::enable_shared_from_this<TSerialExecutor> {
private:
    IAsyncExecutor::TPtr Executor; //!< Wrapped executor that is actually doing the job
    bool Busy = false; //!< Set if some closure was scheduled for execution and did not finish yet
    TMutex Mutex = {};
    TQueue<TFunction> ExecutionQueue = {};

public:
    TSerialExecutor(IAsyncExecutor::TPtr executor);
    ~TSerialExecutor() = default;

private:
    void PostImpl(TVector<TFunction>&& fs) override;
    void PostImpl(TFunction&& f) override;
    void PostNext();
};

class TSyncExecutor : public IExecutor {
public:
    void Post(TFunction&& f) final {
        return f();
    }
    bool IsAsync() const final {
        return false;
    }
    void DoStart() override {
    }
};

IExecutor::TPtr CreateGenericExecutor();

} // namespace NYdb::NPersQueue
