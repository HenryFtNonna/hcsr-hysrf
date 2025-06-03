  #include <ESP8266WiFi.h>
  #include <Firebase_ESP_Client.h>
  #include <math.h>
  #include <time.h>

  // Konfigurasi WiFi
  #define WIFI_SSID "Es Teh Panas"
  #define WIFI_PASSWORD "123123123"

  // Konfigurasi Firebase
  #define FIREBASE_HOST "https://testing-hc-sr04-default-rtdb.asia-southeast1.firebasedatabase.app/"
  #define FIREBASE_AUTH "ImfT9MF6YYXEH3FXl7CQe1kpLAT4YfSOLIDJF7iM"

  // Inisialisasi objek Firebase
  FirebaseData fbdo;
  FirebaseAuth auth;
  FirebaseConfig config;

  // Definisi pin untuk sensor HC-SR04
  #define TRIG_PIN_HC D5
  #define ECHO_PIN_HC D6

  // Definisi pin untuk sensor HY-SRF05
  #define TRIG_PIN_HY D7
  #define ECHO_PIN_HY D8

  #define NUM_SAMPLES 10
  const float MOUNTING_HEIGHT = 200.0; // Ketinggian pemasangan sensor
  const float TRUE_HEIGHT = 180.0;     // Tinggi referensi untuk perhitungan error
  const float MIN_HEIGHT = 100.0;      // Ambang batas minimum deteksi manusia
  const float MAX_HEIGHT = 200.0;      // Ambang batas maksimum deteksi manusia

  // Struktur untuk menyimpan metrik sensor
  struct SensorMetrics {
    float averageHeight;
    float latency;
    float errorPercent;
    float accuracy;
    bool objectDetected;
  };

  // Fungsi untuk mengukur sensor dan menghitung metrik
  SensorMetrics measureSensor(int trigPin, int echoPin, float mountingHeight, float trueHeight) {
    SensorMetrics metrics;
    float readings[NUM_SAMPLES];
    unsigned long latencies[NUM_SAMPLES];

    for (int i = 0; i < NUM_SAMPLES; i++) {
      digitalWrite(trigPin, LOW);
      delayMicroseconds(2);
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);

      unsigned long startTime = micros();
      long duration = pulseIn(echoPin, HIGH, 30000);
      unsigned long endTime = micros();

      latencies[i] = endTime - startTime;
      
      float distance = (duration > 0) ? duration * 0.034 / 2 : mountingHeight;
      readings[i] = mountingHeight - distance;

      delay(50);
    }

    // Hitung rata-rata tinggi dan latensi
    float sumHeight = 0, sumLatency = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      sumHeight += readings[i];
      sumLatency += latencies[i];
    }
    metrics.averageHeight = sumHeight / NUM_SAMPLES;
    metrics.latency = (sumLatency / NUM_SAMPLES) / 1000.0;

    // Hitung error dan akurasi
    metrics.errorPercent = (fabs(metrics.averageHeight - trueHeight) / trueHeight) * 100.0;
    metrics.accuracy = (1 - (metrics.errorPercent / 100.0)) * 100.0;

    // Deteksi objek manusia
    metrics.objectDetected = (metrics.averageHeight >= MIN_HEIGHT && metrics.averageHeight <= MAX_HEIGHT);

    return metrics;
  }

  void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // Set NTP server
    time_t now = time(nullptr);
    int retry = 0;
    while (now < 8 * 3600 * 2 && retry < 20) {  // Tunggu hingga waktu valid
      delay(500);
      Serial.print(".");
      now = time(nullptr);
      retry++;
    }
    Serial.println();
    if (retry >= 20) {
      Serial.println("Gagal sinkronisasi waktu!");
    } else {
      Serial.printf("Waktu terkini: %s", ctime(&now));
    }
  }

  // Tambahkan fungsi formatTimestamp() di atas setup()
  String formatTimestamp() {
    time_t now = time(nullptr);
    struct tm *timeinfo;
    timeinfo = localtime(&now);

    // Sesuaikan timezone (UTC+7 untuk WIB)
    timeinfo->tm_hour += 7;
    mktime(timeinfo); // Normalisasi waktu

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%y/%m/%d %H:%M:%S", timeinfo);
    return String(buffer);
  }

  void setup() {
    Serial.begin(115200);

    // 1. Koneksi WiFi dengan timeout lebih lama
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("Menghubungkan ke WiFi...");
    
    int wifiRetry = 40;  // Timeout 20 detik (40 * 500ms)
    while (WiFi.status() != WL_CONNECTED && wifiRetry > 0) {
      Serial.print(".");
      delay(500);
      wifiRetry--;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\nGagal terhubung ke WiFi! Restart...");
      ESP.restart();
    }
    
    Serial.println("\nTerhubung ke WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // 2. Sinkronisasi waktu
    syncTime();

    // 3. Konfigurasi Firebase
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true); // Aktifkan reconnect WiFi otomatis

    // Inisialisasi pin sensor
    pinMode(TRIG_PIN_HC, OUTPUT);
    pinMode(ECHO_PIN_HC, INPUT);
    pinMode(TRIG_PIN_HY, OUTPUT);
    pinMode(ECHO_PIN_HY, INPUT);
  }

  void sendHistoricalData(const String& sensorPath, const SensorMetrics& metrics) {
    FirebaseJson json;
    json.add("height", metrics.averageHeight);
    json.add("accuracy", metrics.accuracy);
    json.add("latency", metrics.latency);
    json.add("error", metrics.errorPercent);
    json.add("detected", metrics.objectDetected);
    
    // Tambahkan timestamp format kustom
    json.add("timestamp", formatTimestamp());
    
    // Jika tetap ingin menyimpan timestamp numerik
    FirebaseJson serverTimestamp;
    serverTimestamp.add(".sv", "timestamp");
    json.add("timestamp_server", serverTimestamp);

    String historyPath = sensorPath + "/history";
    
    if (!Firebase.RTDB.pushJSON(&fbdo, historyPath.c_str(), &json)) {
      Serial.println("Gagal mengirim histori ke Firebase: " + fbdo.errorReason());
    }
  }


void loop() {
  // 1. Cek status WiFi dan reconnect jika perlu
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus! Mencoba reconnect...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  // 2. Mengukur kedua sensor
  SensorMetrics metricsHC = measureSensor(TRIG_PIN_HC, ECHO_PIN_HC, MOUNTING_HEIGHT, TRUE_HEIGHT);
  SensorMetrics metricsHY = measureSensor(TRIG_PIN_HY, ECHO_PIN_HY, MOUNTING_HEIGHT, TRUE_HEIGHT);

  // 3. Cetak hasil ke Serial Monitor (debug)
  Serial.println("HC-SR04:");
  Serial.print("Tinggi     : "); Serial.print(metricsHC.averageHeight, 2); Serial.println(" cm");
  Serial.print("Akurasi    : "); Serial.print(metricsHC.accuracy, 2);     Serial.println(" %");
  Serial.print("Latensi    : "); Serial.print(metricsHC.latency, 2);      Serial.println(" ms");
  Serial.print("Error      : "); Serial.print(metricsHC.errorPercent, 2); Serial.println(" %");
  Serial.print("Deteksi    : "); Serial.println(metricsHC.objectDetected ? "Manusia Terdeteksi" : "Tidak ada manusia");
  Serial.println();

  Serial.println("HY-SRF05:");
  Serial.print("Tinggi     : "); Serial.print(metricsHY.averageHeight, 2); Serial.println(" cm");
  Serial.print("Akurasi    : "); Serial.print(metricsHY.accuracy, 2);     Serial.println(" %");
  Serial.print("Latensi    : "); Serial.print(metricsHY.latency, 2);      Serial.println(" ms");
  Serial.print("Error      : "); Serial.print(metricsHY.errorPercent, 2); Serial.println(" %");
  Serial.print("Deteksi    : "); Serial.println(metricsHY.objectDetected ? "Manusia Terdeteksi" : "Tidak ada manusia");
  Serial.println("-----------------------");

  // 4. Cek koneksi Firebase
  if (!Firebase.ready()) {
    Serial.println("Firebase tidak terhubung!");
    return;
  }

  // 5. Hanya kirim ke Firebase jika minimal satu sensor mendeteksi
  if (metricsHC.objectDetected || metricsHY.objectDetected) {
    // --- Kirim Data Realtime untuk KEDUA Sensor ---
    // HC-SR04
    Firebase.RTDB.setFloat(&fbdo, "/HC/height", metricsHC.averageHeight);
    Firebase.RTDB.setFloat(&fbdo, "/HC/accuracy", metricsHC.accuracy);
    Firebase.RTDB.setFloat(&fbdo, "/HC/latency", metricsHC.latency);
    Firebase.RTDB.setFloat(&fbdo, "/HC/errorPercent", metricsHC.errorPercent);
    Firebase.RTDB.setBool (&fbdo, "/HC/objectDetected", metricsHC.objectDetected);

    // HY-SRF05
    Firebase.RTDB.setFloat(&fbdo, "/HY/height", metricsHY.averageHeight);
    Firebase.RTDB.setFloat(&fbdo, "/HY/accuracy", metricsHY.accuracy);
    Firebase.RTDB.setFloat(&fbdo, "/HY/latency", metricsHY.latency);
    Firebase.RTDB.setFloat(&fbdo, "/HY/errorPercent", metricsHY.errorPercent);
    Firebase.RTDB.setBool (&fbdo, "/HY/objectDetected", metricsHY.objectDetected);

    // --- Push Histori untuk KEDUA Sensor (tanpa cek objectDetected) ---
    sendHistoricalData("/HC", metricsHC);
    sendHistoricalData("/HY", metricsHY);
  }

  delay(5000);
}


