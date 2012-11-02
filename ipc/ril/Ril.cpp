/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et ft=cpp: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <fcntl.h>
#include <unistd.h>

#include <queue>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/types.h>

#include "base/eintr_wrapper.h"
#include "base/message_loop.h"
#include "mozilla/FileUtils.h"
#include "mozilla/Monitor.h"
#include "mozilla/Util.h"
#include "nsAutoPtr.h"
#include "nsIThread.h"
#include "nsXULAppAPI.h"
#include "Ril.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk", args)
#else
#define LOG(args...)  printf(args);
#endif

#define RIL_SOCKET_NAME "/dev/socket/rilproxy"

using namespace base;
using namespace std;

// Network port to connect to for adb forwarded sockets when doing
// desktop development.
const uint32_t RIL_TEST_PORT = 6200;

namespace mozilla {
namespace ipc {

struct RilClient : public RefCounted<RilClient>,
                   public MessageLoopForIO::Watcher

{
    typedef queue<RilProxyData*> RilProxyDataQueue;

    RilClient() : mSocket(-1)
                , mMutex("RilClient.mMutex")
                , mBlockedOnWrite(false)
                , mIOLoop(MessageLoopForIO::current())
                , mCurrentRilProxyData(NULL)
    { }
    virtual ~RilClient() { }

    bool OpenSocket();

    virtual void OnFileCanReadWithoutBlocking(int fd);
    virtual void OnFileCanWriteWithoutBlocking(int fd);

    ScopedClose mSocket;
    MessageLoopForIO::FileDescriptorWatcher mReadWatcher;
    MessageLoopForIO::FileDescriptorWatcher mWriteWatcher;
    nsAutoPtr<RilProxyData> mIncoming;
    Mutex mMutex;
    RilProxyDataQueue mOutgoingQ;
    bool mBlockedOnWrite;
    MessageLoopForIO* mIOLoop;
    nsAutoPtr<RilProxyData> mCurrentRilProxyData;
    size_t mCurrentWriteOffset;
};

static RefPtr<RilClient> sClient;
static RefPtr<RilConsumer> sConsumer;

//-----------------------------------------------------------------------------
// This code runs on the IO thread.
//

class RilReconnectTask : public CancelableTask {
    RilReconnectTask() : mCanceled(false) { }

    virtual void Run();
    virtual void Cancel() { mCanceled = true; }

    bool mCanceled;

public:
    static void Enqueue(int aDelayMs = 0) {
        MessageLoopForIO* ioLoop = MessageLoopForIO::current();
        if (!ioLoop) {
            NS_WARNING("No IOLoop to attach to, cancelling self!");
            CancelIt();
            return;
        }
        if (sTask) {
            return;
        }
        sTask = new RilReconnectTask();
        if (aDelayMs) {
            ioLoop->PostDelayedTask(FROM_HERE, sTask, aDelayMs);
        } else {
            ioLoop->PostTask(FROM_HERE, sTask);
        }
    }

    static void CancelIt() {
        if (!sTask) {
            return;
        }
        sTask->Cancel();
        sTask = nullptr;
    }

private:
    // Can *ONLY* be touched by the IO thread.  The event queue owns
    // this memory when pointer is nonnull; do *NOT* free it manually.
    static CancelableTask* sTask;
};
CancelableTask* RilReconnectTask::sTask;

void RilReconnectTask::Run() {
    // NB: the order of these two statements is important!  sTask must
    // always run, whether we've been canceled or not, to avoid
    // leading a dangling pointer in sTask.
    sTask = nullptr;
    if (mCanceled) {
        return;
    }

    if (sClient->OpenSocket()) {
        return;
    }
    Enqueue(1000);
}

class RilWriteTask : public Task {
    virtual void Run();
};

void RilWriteTask::Run() {
    if(sClient->mSocket.get() < 0) {
        NS_WARNING("Trying to write to non-open socket!");
        return;
    }
    sClient->OnFileCanWriteWithoutBlocking(sClient->mSocket.rwget());
}

static void
ConnectToRil(Monitor* aMonitor, bool* aSuccess)
{
    MOZ_ASSERT(!sClient);

    sClient = new RilClient();
    RilReconnectTask::Enqueue();
    *aSuccess = true;
    {
        MonitorAutoLock lock(*aMonitor);
        lock.Notify();
    }
    // aMonitor may have gone out of scope by now, don't touch it
}

bool
RilClient::OpenSocket()
{

    ScopedClose skt;
#if defined(MOZ_WIDGET_GONK)
    // Using a network socket to test basic functionality
    // before we see how this works on the phone.
    struct sockaddr_un addr;
    socklen_t alen;
    size_t namelen;
    int err;
    memset(&addr, 0, sizeof(addr));
    strcpy(addr.sun_path, RIL_SOCKET_NAME);
    addr.sun_family = AF_LOCAL;
    skt.reset(socket(AF_LOCAL, SOCK_STREAM, 0));
    alen = strlen(RIL_SOCKET_NAME) + offsetof(struct sockaddr_un, sun_path) + 1;
#else
    struct hostent *hp;
    struct sockaddr_in addr;
    socklen_t alen;

    hp = gethostbyname("localhost");
    if (hp == 0) return false;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = hp->h_addrtype;
    addr.sin_port = htons(RIL_TEST_PORT);
    memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
    skt.reset(socket(hp->h_addrtype, SOCK_STREAM, 0));
    alen = sizeof(addr);
#endif

    if (skt.get() < 0) {
        LOG("Cannot create socket for RIL!\n");
        return false;
    }

    if (connect(skt.get(), (struct sockaddr *) &addr, alen) < 0) {
#if defined(MOZ_WIDGET_GONK)
        LOG("Cannot open socket for RIL!\n");
#endif
        skt.dispose();
        return false;
    }

    // Set close-on-exec bit.
    int flags = fcntl(skt.get(), F_GETFD);
    if (-1 == flags) {
        return false;
    }

    flags |= FD_CLOEXEC;
    if (-1 == fcntl(skt.get(), F_SETFD, flags)) {
        return false;
    }

    // Select non-blocking IO.
    if (-1 == fcntl(skt.get(), F_SETFL, O_NONBLOCK)) {
        return false;
    }
    if (!mIOLoop->WatchFileDescriptor(skt.get(),
                                      true,
                                      MessageLoopForIO::WATCH_READ,
                                      &mReadWatcher,
                                      this)) {
        return false;
    }
    mSocket = skt.forget();
    LOG("Socket open for RIL\n");
    return true;
}

void
RilClient::OnFileCanReadWithoutBlocking(int fd)
{
    // Keep reading data until either
    //
    //   - mIncoming is completely read
    //     If so, sConsumer->MessageReceived(mIncoming.forget())
    //
    //   - mIncoming isn't completely read, but there's no more
    //     data available on the socket
    //     If so, break;

    MOZ_ASSERT(fd == mSocket.get());
    while (true) {
        if (!mIncoming) {
            mIncoming = new RilProxyData();
            ssize_t ret = read(fd, mIncoming->mData, RilProxyData::MAX_DATA_SIZE);
            if (ret <= 0) {
                if (ret == -1) {
                    if (errno == EINTR) {
                        continue; // retry system call when interrupted
                    }
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return; // no data available: return and re-poll
                    }
                    // else fall through to error handling on other errno's
                }
                LOG("Cannot read from network, error %d\n", ret);
                // At this point, assume that we can't actually access
                // the socket anymore, and start a reconnect loop.
                mIncoming.forget();
                mReadWatcher.StopWatchingFileDescriptor();
                mWriteWatcher.StopWatchingFileDescriptor();
                // ScopedClose will close our old socket on a reset.
                // Setting to -1 means writes will fail with message.
                mSocket.reset(-1);
                RilReconnectTask::Enqueue();
                return;
            }
            mIncoming->mDataSize = ret;
            sConsumer->MessageReceived(mIncoming.forget());
            if (ret < ssize_t(RilProxyData::MAX_DATA_SIZE)) {
                return;
            }
        }
    }
}

void
RilClient::OnFileCanWriteWithoutBlocking(int fd)
{
    // Try to write the bytes of mCurrentRilProxyData.  If all were written, continue.
    //
    // Otherwise, save the byte position of the next byte to write
    // within mCurrentRilProxyData, and request another write when the
    // system won't block.
    //

    MOZ_ASSERT(fd == mSocket.get());

    while (true) {
        {
            MutexAutoLock lock(mMutex);

            if (mOutgoingQ.empty() && !mCurrentRilProxyData) {
                return;
            }

            if(!mCurrentRilProxyData) {
                mCurrentRilProxyData = mOutgoingQ.front();
                mOutgoingQ.pop();
                mCurrentWriteOffset = 0;
            }
        }
        const uint8_t *toWrite;

        toWrite = mCurrentRilProxyData->mData;
        while (mCurrentWriteOffset < mCurrentRilProxyData->mDataSize) {
            ssize_t write_amount = mCurrentRilProxyData->mDataSize - mCurrentWriteOffset;
            ssize_t written;
            written = write (fd, toWrite + mCurrentWriteOffset,
                             write_amount);
            if(written > 0) {
                mCurrentWriteOffset += written;
            }
            if (written != write_amount) {
                break;
            }
        }

        if(mCurrentWriteOffset != mCurrentRilProxyData->mDataSize) {
            MessageLoopForIO::current()->WatchFileDescriptor(
                fd,
                false,
                MessageLoopForIO::WATCH_WRITE,
                &mWriteWatcher,
                this);
            return;
        }
        mCurrentRilProxyData = NULL;
    }
    LOG("%s exit", __func__);
}

static void
DisconnectFromRil(Monitor* aMonitor)
{
    // Prevent stale reconnect tasks from being run after we've shut
    // down.
    RilReconnectTask::CancelIt();
    // XXX This might "strand" messages in the outgoing queue.  We'll
    // assume that's OK for now.
    sClient = nullptr;
    {
        MonitorAutoLock lock(*aMonitor);
        lock.Notify();
    }
}

RildData *
RilProxyData::GetNextRildData()
{
    if (offset >= mDataSize) {
        return NULL;
    }
    nsAutoPtr<RildData> data(new RildData);
    uint8_t *ptr = &mData[offset];
    data->mSubId = ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
    data->mDataSize = ptr[4] << 24 | ptr[5] << 16 | ptr[6] << 8 | ptr[7];
    data->mData = new RilRawData();
    memcpy(data->mData->mData,
           &mData[offset + RildData::HEADER_SIZE],
           data->mDataSize);
    offset += RildData::HEADER_SIZE + data->getDataSize();
    return data.forget();
}

void
RilProxyData::appendRildData(RildData* aData)
{
    nsAutoPtr<RildData> data(aData);
//    nsAutoPtr<RildData> data = aData;
    LOG("%s offset=%d subId=%d dataSize=%d", __func__, offset, data->mSubId, data->mDataSize);
    uint8_t *ptr = &mData[offset];
    int subId = data->mSubId, dataSize = data->mDataSize;
    ptr[0] = (subId >> 24) & 0xff;
    ptr[1] = (subId >> 16) & 0xff;
    ptr[2] = (subId >> 8) & 0xff;
    ptr[3] = subId & 0xff;
    ptr[4] = (dataSize >> 24) & 0xff;
    ptr[5] = (dataSize >> 16) & 0xff;
    ptr[6] = (dataSize >> 8) & 0xff;
    ptr[7] = dataSize & 0xff;
    offset += RildData::HEADER_SIZE;
    memcpy(&mData[offset], data->mData->mData, dataSize);
    offset += dataSize;
}
//-----------------------------------------------------------------------------
// This code runs on any thread.
//

bool
StartRil(RilConsumer* aConsumer)
{
    MOZ_ASSERT(aConsumer);
    sConsumer = aConsumer;

    Monitor monitor("StartRil.monitor");
    bool success;
    {
        MonitorAutoLock lock(monitor);

        XRE_GetIOMessageLoop()->PostTask(
            FROM_HERE,
            NewRunnableFunction(ConnectToRil, &monitor, &success));

        lock.Wait();
    }

    return success;
}

bool
SendRilProxyData(RilProxyData** aMessage)
{
    if (!sClient) {
        return false;
    }

    RilProxyData *msg = *aMessage;
    *aMessage = nullptr;

    {
        MutexAutoLock lock(sClient->mMutex);
        sClient->mOutgoingQ.push(msg);
    }
    sClient->mIOLoop->PostTask(FROM_HERE, new RilWriteTask());
    return true;
}

void
StopRil()
{
    Monitor monitor("StopRil.monitor");
    {
        MonitorAutoLock lock(monitor);

        XRE_GetIOMessageLoop()->PostTask(
            FROM_HERE,
            NewRunnableFunction(DisconnectFromRil, &monitor));

        lock.Wait();
    }

    sConsumer = nullptr;
}


} // namespace ipc
} // namespace mozilla
