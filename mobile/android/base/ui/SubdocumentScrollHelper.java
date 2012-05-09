/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2012
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kartikaya Gupta <kgupta@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

package org.mozilla.gecko.ui;

import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoEvent;
import org.mozilla.gecko.GeckoEventListener;
import org.json.JSONObject;
import org.json.JSONException;
import android.graphics.PointF;
import android.os.Handler;
import android.util.Log;

class SubdocumentScrollHelper implements GeckoEventListener {
    private static final String LOGTAG = "GeckoSubdocumentScrollHelper";

    private static String MESSAGE_PANNING_OVERRIDE = "Panning:Override";
    private static String MESSAGE_CANCEL_OVERRIDE = "Panning:CancelOverride";
    private static String MESSAGE_SCROLL = "Gesture:Scroll";
    private static String MESSAGE_SCROLL_ACK = "Gesture:ScrollAck";

    private final PanZoomController mPanZoomController;
    private final Handler mUiHandler;

    /* This is the amount of displacement we have accepted but not yet sent to JS; this is
     * only valid when mOverrideScrollPending is true. */
    private final PointF mPendingDisplacement;

    /* When this is true, we're sending scroll events to JS to scroll the active subdocument. */
    private boolean mOverridePanning;

    /* When this is true, we have received an ack for the last scroll event we sent to JS, and
     * are ready to send the next scroll event. Note we only ever have one scroll event inflight
     * at a time. */
    private boolean mOverrideScrollAck;

    /* When this is true, we have a pending scroll that we need to send to JS; we were unable
     * to send it when it was initially requested because mOverrideScrollAck was not true. */
    private boolean mOverrideScrollPending;

    /* When this is true, the last scroll event we sent actually did some amount of scrolling on
     * the subdocument; we use this to decide when we have reached the end of the subdocument. */
    private boolean mScrollSucceeded;

    SubdocumentScrollHelper(PanZoomController controller) {
        mPanZoomController = controller;
        // mUiHandler will be bound to the UI thread since that's where this constructor runs
        mUiHandler = new Handler();
        mPendingDisplacement = new PointF();

        GeckoAppShell.registerGeckoEventListener(MESSAGE_PANNING_OVERRIDE, this);
        GeckoAppShell.registerGeckoEventListener(MESSAGE_CANCEL_OVERRIDE, this);
        GeckoAppShell.registerGeckoEventListener(MESSAGE_SCROLL_ACK, this);
    }

    boolean scrollBy(PointF displacement) {
        if (! mOverridePanning) {
            return false;
        }

        if (! mOverrideScrollAck) {
            mOverrideScrollPending = true;
            mPendingDisplacement.x += displacement.x;
            mPendingDisplacement.y += displacement.y;
            return true;
        }

        JSONObject json = new JSONObject();
        try {
            json.put("x", displacement.x);
            json.put("y", displacement.y);
        } catch (JSONException e) {
            Log.e(LOGTAG, "Error forming subwindow scroll message: ", e);
        }
        GeckoAppShell.sendEventToGecko(GeckoEvent.createBroadcastEvent(MESSAGE_SCROLL, json.toString()));

        mOverrideScrollAck = false;
        mOverrideScrollPending = false;
        // clear the |mPendingDisplacement| after serializing |displacement| to
        // JSON because they might be the same object
        mPendingDisplacement.x = 0;
        mPendingDisplacement.y = 0;

        return true;
    }

    void cancel() {
        mOverridePanning = false;
    }

    boolean scrolling() {
        return mOverridePanning;
    }

    boolean lastScrollSucceeded() {
        return mScrollSucceeded;
    }

    // GeckoEventListener implementation

    public void handleMessage(final String event, final JSONObject message) {
        // this comes in on the gecko thread; hand off the handling to the UI thread
        mUiHandler.post(new Runnable() {
            public void run() {
                Log.i(LOGTAG, "Got message: " + event);
                try {
                    if (MESSAGE_PANNING_OVERRIDE.equals(event)) {
                        mOverridePanning = true;
                        mOverrideScrollAck = true;
                        mOverrideScrollPending = false;
                        mScrollSucceeded = true;
                    } else if (MESSAGE_CANCEL_OVERRIDE.equals(event)) {
                        mOverridePanning = false;
                    } else if (MESSAGE_SCROLL_ACK.equals(event)) {
                        mOverrideScrollAck = true;
                        mScrollSucceeded = message.getBoolean("scrolled");
                        if (mOverridePanning && mOverrideScrollPending) {
                            scrollBy(mPendingDisplacement);
                        }
                    }
                } catch (Exception e) {
                    Log.e(LOGTAG, "Exception handling message", e);
                }
            }
        });
    }
}
