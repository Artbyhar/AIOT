import time
import json
import random
from paho.mqtt import client as mqtt_client

# === MQTT Configuration ===
broker = 'test.mosquitto.org'
port = 1883
topic_pub = "kuliah_aiot_fsm_uksw"
topic_sub = "kuliah_aiot_fsm_uksw"
client_id = f'SF_A_{random.randint(1000, 9999)}'

# === Konstanta Kelembapan Tanah ===
NILAI_BASAH_1 = 1375
NILAI_KERING_1 = 2885
NILAI_BASAH_2 = 2433
NILAI_KERING_2 = 2686
BATAS_KELEMBAPAN = 30

# === Variabel Simulasi ===
relayA1_status = False
relayA2_status = False
H2_dari_B = 100
sudah_kirim_kontrol = False
waktu_kirim_kontrol = time.time()

# === Fungsi Helper ===
def simulate_adc_read():
    return random.randint(1300, 3000)

def hitung_persen(adc, basah, kering):
    try:
        persen = int((adc - basah) * 100 / (kering - basah))
        return max(0, min(100, persen))
    except ZeroDivisionError:
        return 0

# === Callback MQTT ===
def on_message(client, userdata, msg):
    global H2_dari_B, relayA2_status
    print(f"[MQTT] Diterima di {msg.topic}: {msg.payload.decode()}")
    try:
        data = json.loads(msg.payload.decode())
        H2_dari_B = data.get("Kelembaban_tanah_Pot_2", H2_dari_B)
        if data.get("Aktuator_2", 0) == 1:
            print("[AKSI A2] Pompa A2 nyala 3 detik dari instruksi SF_B")
            relayA2_status = True
            time.sleep(3)
            relayA2_status = False
    except Exception as e:
        print("[ERROR] Gagal parsing JSON:", e)

# === Koneksi MQTT ===
def connect_mqtt():
    client = mqtt_client.Client(client_id=client_id, protocol=mqtt_client.MQTTv311)
    client.on_message = on_message
    client.connect(broker, port)
    return client

# === Main ===
client = connect_mqtt()
client.loop_start()
client.subscribe(topic_sub)

INTERVAL = 30  # detik

while True:
    adcA1 = simulate_adc_read()
    adcA2 = simulate_adc_read()
    kelembapanA1 = hitung_persen(adcA1, NILAI_BASAH_1, NILAI_KERING_1)
    kelembapanA2 = hitung_persen(adcA2, NILAI_BASAH_2, NILAI_KERING_2)
    kontrol = 0

    # Simulasi Pompa A1 jika kelembapan rendah
    if kelembapanA1 < BATAS_KELEMBAPAN:
        print("[AKSI A1] Pompa A1 nyala 3 detik karena H1 rendah")
        relayA1_status = True
        time.sleep(3)
        relayA1_status = False

    # Cek H2 dari SF_B
    now = time.time()
    if H2_dari_B < BATAS_KELEMBAPAN:
        if not sudah_kirim_kontrol or (now - waktu_kirim_kontrol >= 30):
            kontrol = 1
            sudah_kirim_kontrol = True
            waktu_kirim_kontrol = now
            print("[KONTROL] Control=1 karena H2 SF_B rendah")
    else:
        sudah_kirim_kontrol = False

    payload = {
        "NomorTim": 1,
        "SmartFarmingGroup": "SF_A",
        "Kelembaban_tanah_Pot_1": kelembapanA1,
        "Kelembaban_tanah_Pot_2": kelembapanA2,
        "Aktuator_1": int(kelembapanA1 < BATAS_KELEMBAPAN),
        "Aktuator_2": kontrol,
        "Status_Kran_1": int(relayA1_status),
        "Status_Kran_2": int(relayA2_status)
    }

    client.publish(topic_pub, json.dumps(payload))
    print(f"[KIRIM] {json.dumps(payload)}")
    print(f"[STATUS] H1={kelembapanA1}%, H2={kelembapanA2}%, H2_B={H2_dari_B}% {'↓' if H2_dari_B < BATAS_KELEMBAPAN else '↑'}\n")

    time.sleep(INTERVAL)
