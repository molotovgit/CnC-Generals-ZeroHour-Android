
package org.generalsx.zerohour;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

public class ZerohourActivity extends SDLActivity
{
    // @port Android: LAN multiplayer discovery uses UDP broadcast/multicast on
    // the local WiFi. Android's WiFi driver drops multicast/broadcast packets
    // that are not addressed to this device unless a MulticastLock is held, so
    // without this the two phones cannot see each other's hosted LAN games.
    private WifiManager.MulticastLock mMulticastLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            WifiManager wifi = (WifiManager) getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                mMulticastLock = wifi.createMulticastLock("generalsx-lan");
                mMulticastLock.setReferenceCounted(true);
                mMulticastLock.acquire();
            }
        } catch (Exception e) {
            // Non-fatal: LAN discovery may be less reliable without the lock.
        }
    }

    @Override
    protected void onDestroy() {
        try {
            if (mMulticastLock != null && mMulticastLock.isHeld()) {
                mMulticastLock.release();
            }
        } catch (Exception e) {
            // ignore
        }
        super.onDestroy();
    }
}
