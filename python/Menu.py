import serial, time, os

PORT = "COM3"
BAUD = 115200

def open_serial():
    ser = serial.Serial(PORT, BAUD, timeout=0.5)
    time.sleep(2)
    ser.reset_input_buffer()
    return ser

def send(ser, line):
    ser.write((line + "\n").encode("ascii"))
    ser.flush()

def get_count(ser):
    send(ser, "COUNT")
    line = ser.read_until(b"\n").decode(errors="ignore").strip()
    if line.startswith("COUNT="):
        return int(line.split("=")[1])
    return 0

def enroll(ser, id):
    send(ser, f"ENROLL {id}")
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            print(line)
            if "ENROLL_OK" in line or "FAIL" in line:
                break

def export_all(ser):
    count = get_count(ser)
    print("Templates armazenados:", count)
    os.makedirs("templates", exist_ok=True)

    for id in range(1, count+1):
        send(ser, f"EXPORT {id}")

        # lê debug até marcador
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            print("[DEBUG]", line)  # <-- debug do Arduino
            if "TEMPLATE_RAW_START" in line:
                break

        data = b""
        while True:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                data += chunk
            if b"TEMPLATE_RAW_END" in data:
                break

        start = data.find(b"TEMPLATE_RAW_START")
        end = data.find(b"TEMPLATE_RAW_END")
        template_bytes = data[start+len("TEMPLATE_RAW_START"):end]

        size_line = ser.readline().decode(errors="ignore").strip()
        print("[DEBUG]", size_line)

        fname = f"templates/{id}.bin"
        with open(fname, "wb") as f:
            f.write(template_bytes)
        print(f"ID {id} exportado ({len(template_bytes)} bytes)")

def clear_db(ser):
    send(ser, "CLEAR")
    print(ser.readline().decode(errors="ignore").strip())

def import_all(ser):
    for fname in sorted(os.listdir("templates")):
        if not fname.endswith(".bin"): continue
        id = int(fname.split(".")[0])
        with open(os.path.join("templates", fname), "rb") as f:
            data = f.read()
        size = len(data)
        print(f"Importando ID {id} ({size} bytes)")
        send(ser, f"IMPORT {id} {size}")
        ser.write(data)
        ser.flush()
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            print("[DEBUG]", line)
            if "IMPORT_OK" in line or "IMPORT_FAIL" in line:
                break

def loop_verify(ser):
    print("Iniciando verificação contínua (CTRL+C para sair)")
    try:
        while True:
            send(ser, "VERIFY")
            line = ser.readline().decode(errors="ignore").strip()
            print("[DEBUG]", line)
            if "MATCH:" in line or "NO_MATCH" in line:
                time.sleep(1)
    except KeyboardInterrupt:
        print("Loop de verificação interrompido.")

# ----------------- Menu -----------------

def menu():
    ser = open_serial()
    while True:
        print("\n=== MENU FINGERPRINT ===")
        print("1 - Enroll nova digital")
        print("2 - Contar digitais salvas")
        print("3 - Exportar digitais")
        print("4 - Apagar todas digitais")
        print("5 - Importar digitais")
        print("6 - Loop Verify (comparar dedo)")
        print("0 - Sair")
        choice = input("Escolha uma opção: ")

        if choice == "1":
            id = input("Digite o ID: ")
            enroll(ser, int(id))
        elif choice == "2":
            print("Total no sensor:", get_count(ser))
        elif choice == "3":
            export_all(ser)
        elif choice == "4":
            clear_db(ser)
        elif choice == "5":
            import_all(ser)
        elif choice == "6":
            loop_verify(ser)
        elif choice == "0":
            ser.close()
            break
        else:
            print("Opção inválida!")

if __name__ == "__main__":
    menu()