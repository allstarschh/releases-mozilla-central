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
};

/*
 * RildData represents the data received from each rild, this class includes
 * 'subId', 'dataSize' and 'RilRawData.'
 */ 
class RildData
{
public:
    int getSubId() {
        return mSubId;
    }

    int getDataSize() {
        return mDataSize;
    }

    RilRawData* getRilRawData() {
        return mData;
    }

    static const size_t SUB_ID_SIZE = 4;
    static const size_t DATA_SIZE = 4;
    static const size_t HEADER_SIZE = SUB_ID_SIZE + DATA_SIZE;

    int mSubId;
    int mDataSize;
    nsAutoPtr<RilRawData> mData;
};

/*
 * RilProxyData represents the data received from rilproxy, in multi rild
 * scenario, RilProxyData could consist of several RildData from different rild.
 */ 
class RilProxyData
{
public:
    RilProxyData()
        : offset(0)
    { }

    RildData* GetNextRildData();

    void appendRildData(RildData* data);

    //TODO * NUM_RILD
//    static const size_t MAX_DATA_SIZE = (RilRawData::MAX_DATA_SIZE +
//                                         RildData::HEADER_SIZE) * 2;
    static const size_t MAX_DATA_SIZE = 1024;

    uint8_t mData[MAX_DATA_SIZE];
    size_t mDataSize;
private:
    size_t offset;
};

class RilConsumer : public RefCounted<RilConsumer>
{
public:
    virtual ~RilConsumer() { }
    virtual void MessageReceived(RilRawData* aMessage) { }
    virtual void MessageReceived(RilProxyData* aMessage) { }
};

bool StartRil(RilConsumer* aConsumer);

bool SendRilProxyData(RilProxyData** aMessage);

void StopRil();

} // namespace ipc
} // namepsace mozilla

#endif // mozilla_ipc_Ril_h
