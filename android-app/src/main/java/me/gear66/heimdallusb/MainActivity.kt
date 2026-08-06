package me.gear66.heimdallusb

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var usbManager: UsbManager
    private lateinit var status: TextView
    private var connection: UsbDeviceConnection? = null
    private var claimedInterface: UsbInterface? = null

    private val permissionAction by lazy { "$packageName.USB_PERMISSION" }

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != permissionAction) return

            val device = if (Build.VERSION.SDK_INT >= 33) {
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
            }

            if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && device != null) {
                openAndClaim(device)
            } else {
                append("USB permission denied.")
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        usbManager = getSystemService(USB_SERVICE) as UsbManager

        status = TextView(this).apply {
            textSize = 16f
            setPadding(24, 24, 24, 24)
            text = "Heimdall Android USB bridge diagnostic\n\n"
        }

        val scanButton = Button(this).apply {
            text = "Find and claim Samsung device"
            setOnClickListener { findSamsungDevice() }
        }

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(24, 36, 24, 24)
            addView(scanButton, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(status, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }

        setContentView(ScrollView(this).apply { addView(content) })

        val filter = IntentFilter(permissionAction)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(permissionReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            registerReceiver(permissionReceiver, filter)
        }

        findSamsungDevice()
    }

    private fun findSamsungDevice() {
        releaseUsb()
        status.text = "Scanning attached USB devices...\n"

        val samsung = usbManager.deviceList.values.firstOrNull { it.vendorId == SAMSUNG_VENDOR_ID }
        if (samsung == null) {
            append("No Samsung USB device is attached.")
            append("Put the tablet in Download Mode and reconnect the cable.")
            return
        }

        append("Found ${hex(samsung.vendorId)}:${hex(samsung.productId)}")
        append("Device name: ${samsung.deviceName}")
        append("Interfaces reported: ${samsung.interfaceCount}")

        if (usbManager.hasPermission(samsung)) {
            append("Android USB permission already granted.")
            openAndClaim(samsung)
        } else {
            append("Requesting Android USB permission...")
            val pendingIntent = PendingIntent.getBroadcast(
                this,
                0,
                Intent(permissionAction).setPackage(packageName),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            usbManager.requestPermission(samsung, pendingIntent)
        }
    }

    private fun openAndClaim(device: UsbDevice) {
        releaseUsb()

        val dataInterface = (0 until device.interfaceCount)
            .map { device.getInterface(it) }
            .firstOrNull {
                it.interfaceClass == UsbConstants.USB_CLASS_CDC_DATA &&
                    it.endpointCount >= 2
            }

        if (dataInterface == null) {
            append("FAILED: CDC data interface was not found.")
            describeInterfaces(device)
            return
        }

        append(
            "Selected interface ${dataInterface.id}: " +
                "class=0x${dataInterface.interfaceClass.toString(16)}, " +
                "endpoints=${dataInterface.endpointCount}"
        )

        val opened = usbManager.openDevice(device)
        if (opened == null) {
            append("FAILED: UsbManager.openDevice() returned null.")
            return
        }

        connection = opened
        append("UsbDeviceConnection opened. fd=${opened.fileDescriptor}")

        val claimed = opened.claimInterface(dataInterface, true)
        if (!claimed) {
            append("FAILED: Android UsbDeviceConnection.claimInterface() returned false.")
            releaseUsb()
            return
        }

        claimedInterface = dataInterface
        append("SUCCESS: Android claimed interface ${dataInterface.id}.")
        append("This proves the interface can be owned through Android's USB Host API.")
        append("Next engineering step: run Heimdall protocol transfers inside this app/JNI process.")
    }

    private fun describeInterfaces(device: UsbDevice) {
        for (index in 0 until device.interfaceCount) {
            val intf = device.getInterface(index)
            append(
                "Interface[$index] id=${intf.id} class=0x${intf.interfaceClass.toString(16)} " +
                    "subclass=0x${intf.interfaceSubclass.toString(16)} endpoints=${intf.endpointCount}"
            )
        }
    }

    private fun releaseUsb() {
        claimedInterface?.let { connection?.releaseInterface(it) }
        claimedInterface = null
        connection?.close()
        connection = null
    }

    private fun append(message: String) {
        status.append("$message\n")
    }

    private fun hex(value: Int): String = "0x%04X".format(value)

    override fun onDestroy() {
        releaseUsb()
        unregisterReceiver(permissionReceiver)
        super.onDestroy()
    }

    companion object {
        private const val SAMSUNG_VENDOR_ID = 0x04E8
    }
}
