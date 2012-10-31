/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_Ril_h
#define mozilla_ipc_Ril_h 1

#include "mozilla/RefPtr.h"

namespace base {
class MessageLoop;
}

namespace mozilla {
namespace ipc {

/*
 * Represents raw data going to or coming from the RIL socket. Can
 * actually contain multiple RIL parcels in the data block, and may
 * also contain incomplete parcels on the front or back. Actual parcel
 * construction is handled in the worker thread.
 */
struct RilRawData
{
    static const size_t MAX_DATA_SIZE = 1024;
    uint8_t mData[MAX_DATA_SIZE];

    // Number of octets in mData.
    size_t mSize;
};

class RilSubscriptionData : RilRawData
{
public:
    int getSubId() {
        return mSubId;
    }

    int getDataSize() {
        return mDataSize;
    }

    RilRawData* getRilRawData() {

    }

    static const size_t SUB_ID_SIZE = 4;
    static const size_t DATA_SIZE = 4;

    uint8_t *mData;
    int mDataSize;
    int mSubId;
};

class RilProxyData
{
public:
    RilProxyData()
        : index(0)
    { }

    RilSubscriptionData* getNextRilSubscriptionData() {
      if (index < mSize) {
        nsAutoPtr<RilSubscriptionData> subData(new RilSubscriptionData);
        subData->mSubId = mData[offset + 0] << 24 |
                          mData[offset + 1] << 16 |
                          mData[offset + 2] << 8  |
                          mData[offset + 3];
        subData->mDataSize = mData[offset + 4] << 24 |
                             mData[offset + 5] << 16 |
                             mData[offset + 6] << 8  |
                             mData[offset + 7];
        subData->mData = &mData[offset];
        offset += subData::SUB_ID_SIZE + subData::DATA_SIZE + subData->getDataSize();
        return subData.forget();
      }
    }

    //TODO * NUM_RILD ?
    static const size_t MAX_DATA_SIZE = 1032;
    uint8_t mData[MAX_DATA_SIZE];
    // Number of octets in mData.
    size_t mSize;
private:
    int offset;
};

class RilConsumer : public RefCounted<RilConsumer>
{
public:
    virtual ~RilConsumer() { }
    virtual void MessageReceived(RilRawData* aMessage) { }
    virtual void MessageReceived(RilProxyData* aMessage) { }
};

bool StartRil(RilConsumer* aConsumer);

bool SendRilRawData(RilRawData** aMessage);

void StopRil();

} // namespace ipc
} // namepsace mozilla

#endif // mozilla_ipc_Ril_h
