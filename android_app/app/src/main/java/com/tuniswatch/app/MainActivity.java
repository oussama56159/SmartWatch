package com.tuniswatch.app;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class MainActivity extends Activity {
    private static final UUID SERVICE = UUID.fromString("b84c0001-9f5b-47af-8b6e-84768f2a5c01");
    private static final UUID TELEMETRY = UUID.fromString("b84c0002-9f5b-47af-8b6e-84768f2a5c01");
    private static final UUID PHONE_RSSI = UUID.fromString("b84c0003-9f5b-47af-8b6e-84768f2a5c01");
    private static final UUID CCC = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final int REQUEST_PERMISSIONS = 41;
    private static final int REQUEST_BLUETOOTH = 42;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rssiWrite;
    private boolean scanning = false;
    private boolean connected = false;
    private boolean active = true;
    private int generation = 0;
    private TextView connectionLabel, heartValue, yawValue, soundValue, signalValue, detailLabel;
    private SignalView signalView;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(8, 14, 27));
        getWindow().setNavigationBarColor(Color.rgb(8, 14, 27));
        buildScreen();
        BluetoothManager manager = getSystemService(BluetoothManager.class);
        adapter = manager == null ? null : manager.getAdapter();
        handler.postDelayed(this::startIfReady, 300);
    }

    private int dp(float value) { return (int) (value * getResources().getDisplayMetrics().density + 0.5f); }
    private GradientDrawable background(int color, float radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radius));
        return drawable;
    }
    private TextView text(String value, int size, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }
    private void buildScreen() {
        int bg = Color.rgb(8, 14, 27), card = Color.rgb(21, 31, 50);
        int muted = Color.rgb(153, 171, 193), white = Color.WHITE;
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(bg);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(24), dp(20), dp(24));
        scroll.addView(root);
        setContentView(scroll);

        TextView brand = text("TUNIS  /  WATCH", 13, Color.rgb(84, 223, 202), true);
        root.addView(brand);
        TextView title = text("Your watch, live.", 29, white, true);
        LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(-1, -2);
        titleParams.topMargin = dp(8);
        root.addView(title, titleParams);
        connectionLabel = text("Looking for Tunis Watch...", 14, muted, false);
        LinearLayout.LayoutParams connectionParams = new LinearLayout.LayoutParams(-1, -2);
        connectionParams.topMargin = dp(8);
        root.addView(connectionLabel, connectionParams);

        Button connect = new Button(this);
        connect.setText("SCAN / RECONNECT");
        connect.setTextColor(bg);
        connect.setAllCaps(false);
        connect.setBackground(background(Color.rgb(84, 223, 202), 14));
        connect.setOnClickListener(v -> { disconnect(); startIfReady(); });
        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(-1, dp(48));
        buttonParams.topMargin = dp(20);
        root.addView(connect, buttonParams);

        LinearLayout signalCard = new LinearLayout(this);
        signalCard.setOrientation(LinearLayout.HORIZONTAL);
        signalCard.setGravity(Gravity.CENTER_VERTICAL);
        signalCard.setPadding(dp(18), dp(14), dp(18), dp(14));
        signalCard.setBackground(background(card, 20));
        LinearLayout.LayoutParams signalParams = new LinearLayout.LayoutParams(-1, dp(145));
        signalParams.topMargin = dp(22);
        root.addView(signalCard, signalParams);
        LinearLayout signalTexts = new LinearLayout(this);
        signalTexts.setOrientation(LinearLayout.VERTICAL);
        signalCard.addView(signalTexts, new LinearLayout.LayoutParams(0, -2, 1));
        signalTexts.addView(text("FIND PHONE", 12, Color.rgb(84, 223, 202), true));
        signalValue = text("-- dBm", 27, white, true);
        signalTexts.addView(signalValue);
        signalTexts.addView(text("Bubble grows with BLE signal", 12, muted, false));
        signalView = new SignalView();
        signalCard.addView(signalView, new LinearLayout.LayoutParams(dp(100), dp(100)));

        TextView readings = text("LIVE READINGS", 12, muted, true);
        LinearLayout.LayoutParams readingsParams = new LinearLayout.LayoutParams(-1, -2);
        readingsParams.topMargin = dp(26);
        readingsParams.bottomMargin = dp(4);
        root.addView(readings, readingsParams);
        heartValue = addReading(root, card, "HEART RATE", "-- BPM", "MAX30102");
        yawValue = addReading(root, card, "RELATIVE YAW", "--°", "MPU6050 · may drift");
        soundValue = addReading(root, card, "SOUND LEVEL", "--", "MAX4466 · ADC amplitude");
        detailLabel = text("Open the app near your watch. Allow Nearby devices and Location when asked.",
                12, muted, false);
        LinearLayout.LayoutParams detailParams = new LinearLayout.LayoutParams(-1, -2);
        detailParams.topMargin = dp(18);
        root.addView(detailLabel, detailParams);
    }
    private TextView addReading(LinearLayout root, int card, String title, String value, String note) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(18), dp(15), dp(18), dp(15));
        box.setBackground(background(card, 16));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(11);
        root.addView(box, params);
        box.addView(text(title, 12, Color.rgb(84, 223, 202), true));
        TextView result = text(value, 25, Color.WHITE, true);
        box.addView(result);
        box.addView(text(note, 12, Color.rgb(153, 171, 193), false));
        return result;
    }

    private boolean hasPermissions() {
        if (Build.VERSION.SDK_INT >= 31 &&
                (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED ||
                 checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED)) return false;
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }
    private void startIfReady() {
        if (!active) return;
        if (adapter == null) { showStatus("This phone does not support Bluetooth."); return; }
        if (!hasPermissions()) {
            if (Build.VERSION.SDK_INT >= 31)
                requestPermissions(new String[]{Manifest.permission.BLUETOOTH_SCAN,
                        Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION}, REQUEST_PERMISSIONS);
            else requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, REQUEST_PERMISSIONS);
            return;
        }
        if (!adapter.isEnabled()) {
            startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQUEST_BLUETOOTH);
            return;
        }
        if (!connected && !scanning) startScan();
    }
    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code == REQUEST_PERMISSIONS) {
            if (hasPermissions()) startIfReady();
            else showStatus("Bluetooth permission is needed to find the watch.");
        }
    }
    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code == REQUEST_BLUETOOTH && result == RESULT_OK) startIfReady();
    }
    private void showStatus(String status) { runOnUiThread(() -> connectionLabel.setText(status)); }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override public void onScanResult(int type, ScanResult result) {
            String name = result.getScanRecord() == null ? null : result.getScanRecord().getDeviceName();
            String saved = getPreferences(MODE_PRIVATE).getString("watch_address", "");
            boolean serviceFound = result.getScanRecord() != null &&
                    result.getScanRecord().getServiceUuids() != null &&
                    result.getScanRecord().getServiceUuids().contains(new ParcelUuid(SERVICE));
            if ("Tunis Watch".equals(name) || serviceFound || result.getDevice().getAddress().equals(saved)) {
                runOnUiThread(() -> {
                    if (!scanning) return;
                    stopScan();
                    connect(result.getDevice());
                });
            }
        }
        @Override public void onScanFailed(int error) {
            scanning = false;
            showStatus("BLE scan failed: " + error);
        handler.postDelayed(MainActivity.this::startIfReady, 5000);
        }
    };
    private void startScan() {
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) { showStatus("Bluetooth scanner unavailable."); return; }
        scanning = true;
        showStatus("Scanning for Tunis Watch...");
        try { scanner.startScan(scanCallback); }
        catch (SecurityException e) { scanning = false; showStatus("Bluetooth permission missing."); return; }
        int scanGeneration = ++generation;
        handler.postDelayed(() -> {
            if (scanning && scanGeneration == generation) {
                stopScan();
                showStatus("Watch not found. Retrying...");
                handler.postDelayed(this::startIfReady, 3000);
            }
        }, 12000);
    }
    private void stopScan() {
        if (!scanning) return;
        scanning = false;
        ++generation;
        try { scanner.stopScan(scanCallback); } catch (SecurityException ignored) { }
    }
    private void connect(BluetoothDevice device) {
        showStatus("Connecting to watch...");
        try { gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE); }
        catch (SecurityException e) { showStatus("Bluetooth permission missing."); }
    }
    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt source, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                connected = true;
                getPreferences(MODE_PRIVATE).edit().putString("watch_address", source.getDevice().getAddress()).apply();
                showStatus("Connected · discovering sensors...");
                try { if (!source.requestMtu(185)) source.discoverServices(); }
                catch (SecurityException e) { showStatus("Bluetooth permission missing."); }
            } else {
                connected = false;
                rssiWrite = null;
                source.close();
                if (gatt == source) gatt = null;
                showStatus("Disconnected · reconnecting...");
                if (active) handler.postDelayed(MainActivity.this::startIfReady, 2500);
            }
        }
        @Override public void onMtuChanged(BluetoothGatt source, int mtu, int status) {
            try { source.discoverServices(); } catch (SecurityException ignored) { }
        }
        @Override public void onServicesDiscovered(BluetoothGatt source, int status) {
            BluetoothGattService service = source.getService(SERVICE);
            if (status != BluetoothGatt.GATT_SUCCESS || service == null) {
                showStatus("Watch BLE service not found."); return;
            }
            BluetoothGattCharacteristic telemetry = service.getCharacteristic(TELEMETRY);
            rssiWrite = service.getCharacteristic(PHONE_RSSI);
            if (telemetry == null || rssiWrite == null) {
                showStatus("Watch data channels not found."); return;
            }
            try {
                source.setCharacteristicNotification(telemetry, true);
                BluetoothGattDescriptor descriptor = telemetry.getDescriptor(CCC);
                if (descriptor != null) {
                    byte[] enable = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE;
                    if (Build.VERSION.SDK_INT >= 33) source.writeDescriptor(descriptor, enable);
                    else { descriptor.setValue(enable); source.writeDescriptor(descriptor); }
                }
            } catch (SecurityException ignored) { }
            showStatus("Connected to Tunis Watch");
            scheduleRssi(source);
        }
        @Override public void onCharacteristicChanged(BluetoothGatt source,
                BluetoothGattCharacteristic characteristic, byte[] value) {
            if (TELEMETRY.equals(characteristic.getUuid())) parseTelemetry(value);
        }
        @Override public void onCharacteristicChanged(BluetoothGatt source,
                BluetoothGattCharacteristic characteristic) {
            if (TELEMETRY.equals(characteristic.getUuid())) parseTelemetry(characteristic.getValue());
        }
        @Override public void onReadRemoteRssi(BluetoothGatt source, int rssi, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS || source != gatt) return;
            runOnUiThread(() -> {
                signalValue.setText(rssi + " dBm");
                signalView.setRssi(rssi);
            });
            BluetoothGattCharacteristic target = rssiWrite;
            if (target == null) return;
            byte[] bytes = Integer.toString(rssi).getBytes(StandardCharsets.UTF_8);
            try {
                if (Build.VERSION.SDK_INT >= 33)
                    source.writeCharacteristic(target, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                else {
                    target.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                    target.setValue(bytes);
                    source.writeCharacteristic(target);
                }
            } catch (SecurityException ignored) { }
        }
    };
    private void scheduleRssi(BluetoothGatt source) {
        handler.postDelayed(() -> {
            if (connected && gatt == source) {
                try { source.readRemoteRssi(); } catch (SecurityException ignored) { }
                scheduleRssi(source);
            }
        }, 1000);
    }
    private void parseTelemetry(byte[] bytes) {
        if (bytes == null) return;
        String line = new String(bytes, StandardCharsets.UTF_8);
        Map<String, String> values = new HashMap<>();
        for (String entry : line.split(";")) {
            String[] pair = entry.split("=", 2);
            if (pair.length == 2) values.put(pair[0], pair[1]);
        }
        runOnUiThread(() -> {
            if (!connected) return;
            if (!"1".equals(values.get("P"))) heartValue.setText("No sensor");
            else {
                long ir = 0;
                try { ir = Long.parseLong(values.getOrDefault("IR", "0")); } catch (NumberFormatException ignored) { }
                heartValue.setText(ir < 50000 ? "Place finger" : values.getOrDefault("B", "--") + " BPM");
            }
            yawValue.setText("1".equals(values.get("M")) ? values.getOrDefault("Y", "--") + "°" : "No sensor");
            soundValue.setText("1".equals(values.get("A")) ? values.getOrDefault("S", "--") : "Paused for Wi-Fi");
            detailLabel.setText("Live over Bluetooth · heart rate needs a finger · sound is ADC amplitude, not dB.");
        });
    }
    private void disconnect() {
        stopScan();
        connected = false;
        rssiWrite = null;
        if (gatt != null) {
            try { gatt.disconnect(); gatt.close(); } catch (SecurityException ignored) { }
            gatt = null;
        }
    }
    @Override protected void onDestroy() {
        active = false;
        disconnect();
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private class SignalView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int rssi = -100;
        SignalView() { super(MainActivity.this); }
        void setRssi(int value) { rssi = value; invalidate(); }
        @Override protected void onDraw(Canvas canvas) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f;
            float max = Math.min(cx, cy) - dp(4);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(1));
            paint.setColor(Color.rgb(53, 76, 96));
            for (int i = 1; i <= 3; i++) canvas.drawCircle(cx, cy, max * i / 3f, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.rgb(84, 223, 202));
            float strength = Math.max(0, Math.min(1, (rssi + 100f) / 65f));
            canvas.drawCircle(cx, cy, dp(6) + max * 0.75f * strength, paint);
        }
    }
}
