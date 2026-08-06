package com.gear66.heimdallusbprobe;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

public final class MainActivity extends Activity {
    private static final String ACTION_USB_PERMISSION =
            "com.gear66.heimdallusbprobe.USB_PERMISSION";
    private static final int SAMSUNG_VENDOR_ID = 0x04E8;

    private UsbManager usbManager;
    private TextView logView;

    private final BroadcastReceiver permissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_USB_PERMISSION.equals(intent.getAction())) {
                return;
            }
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            append("Permission result: " + granted);
            if (granted && device != null) {
                probe(device);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        usbManager = (UsbManager) getSystemService(Context.USB_SERVICE);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(24, 24, 24, 24);

        TextView title = new TextView(this);
        title.setText("Heimdall Android USB Claim Probe");
        title.setTextSize(22f);
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        Button runButton = new Button(this);
        runButton.setText("Run USB claim test");
        runButton.setOnClickListener(v -> startProbe());
        root.addView(runButton, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        logView = new TextView(this);
        logView.setTextSize(15f);
        logView.setTextIsSelectable(true);
        logView.setMovementMethod(new ScrollingMovementMethod());
        root.addView(logView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);

        IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
        registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        append("Ready. Put the tablet in Samsung Download Mode, connect OTG, then tap the button.");
    }

    @Override
    protected void onDestroy() {
        unregisterReceiver(permissionReceiver);
        super.onDestroy();
    }

    private void startProbe() {
        logView.setText("");
        append("Scanning USB devices...");

        UsbDevice samsung = null;
        for (UsbDevice device : usbManager.getDeviceList().values()) {
            append(String.format(Locale.US,
                    "Found: %s VID:PID=%04X:%04X interfaces=%d",
                    device.getDeviceName(), device.getVendorId(), device.getProductId(),
                    device.getInterfaceCount()));
            if (device.getVendorId() == SAMSUNG_VENDOR_ID) {
                samsung = device;
                break;
            }
        }

        if (samsung == null) {
            append("RESULT: No Samsung USB device found.");
            return;
        }

        if (usbManager.hasPermission(samsung)) {
            append("USB permission already granted.");
            probe(samsung);
            return;
        }

        append("Requesting Android USB permission...");
        Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(getPackageName());
        PendingIntent permissionIntent = PendingIntent.getBroadcast(
                this, 0, intent, PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        usbManager.requestPermission(samsung, permissionIntent);
    }

    private void probe(UsbDevice device) {
        append(String.format(Locale.US, "Opening VID:PID=%04X:%04X...",
                device.getVendorId(), device.getProductId()));

        UsbDeviceConnection connection = usbManager.openDevice(device);
        if (connection == null) {
            append("RESULT: openDevice() returned null.");
            return;
        }

        boolean anyClaimed = false;
        try {
            for (int i = 0; i < device.getInterfaceCount(); i++) {
                UsbInterface intf = device.getInterface(i);
                append(String.format(Locale.US,
                        "Interface array=%d id=%d class=%02X subclass=%02X protocol=%02X endpoints=%d",
                        i, intf.getId(), intf.getInterfaceClass(), intf.getInterfaceSubclass(),
                        intf.getInterfaceProtocol(), intf.getEndpointCount()));

                for (int e = 0; e < intf.getEndpointCount(); e++) {
                    UsbEndpoint ep = intf.getEndpoint(e);
                    append(String.format(Locale.US,
                            "  endpoint[%d] address=%02X direction=%s type=%d maxPacket=%d",
                            e, ep.getAddress(),
                            ep.getDirection() == UsbConstants.USB_DIR_IN ? "IN" : "OUT",
                            ep.getType(), ep.getMaxPacketSize()));
                }

                boolean claimed;
                try {
                    claimed = connection.claimInterface(intf, true);
                } catch (RuntimeException ex) {
                    append("  claimInterface(force=true) threw: " + ex);
                    continue;
                }

                append("  claimInterface(force=true) = " + claimed);
                if (claimed) {
                    anyClaimed = true;
                    connection.releaseInterface(intf);
                    append("  releaseInterface() completed");
                }
            }

            append(anyClaimed
                    ? "RESULT: SUCCESS — Android native USB API claimed at least one interface."
                    : "RESULT: FAILURE — Android native USB API could not claim any interface.");
        } finally {
            connection.close();
            append("Connection closed. No firmware data was written.");
        }
    }

    private void append(String line) {
        logView.append(line + "\n");
    }
}
