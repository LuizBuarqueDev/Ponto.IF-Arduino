import serial, time, os

PORT = "COM3"   # ajuste conforme necessário
BAUD = 115200
TEMPLATE_SIZE = 498

# ----------------- Utilitários -----------------

def open_serial():
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    time.sleep(2)
    ser.reset_input_buffer()
    return ser

def send(ser, line):
    ser.write((line + "\n").encode("ascii"))
    ser.flush()

def wait_marker(ser, marker, timeout=5):
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting)
            if marker.encode() in buf:
                return True
        else:
            time.sleep(0.01)
    return False

# ----------------- Comandos -----------------

def enroll(ser, id):
    send(ser, f"ENROLL {id}")
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            print(line)
            if "ENROLL_OK" in line or "FAIL" in line:
                break

def get_count(ser):
    send(ser, "COUNT")
    deadline = time.time() + 3
    while time.time() < deadline:
        line = ser.read_until(b"\n").decode(errors="ignore").strip()
        if line.startswith("COUNT="):
            return int(line.split("=")[1])
    return 0

def export_all(ser):
    count = get_count(ser)
    print("Templates armazenados:", count)

    os.makedirs("templates", exist_ok=True)

    for id in range(1, count+1):
        send(ser, f"EXPORT {id}")
        if not wait_marker(ser, "TEMPLATE_BIN_START", 5):
            print(f"ID {id} falhou (sem start)")
            continue
        data = b""
        while len(data) < TEMPLATE_SIZE:
            chunk = ser.read(TEMPLATE_SIZE - len(data))
            if not chunk: break
            data += chunk
        wait_marker(ser, "TEMPLATE_BIN_END", 5)
        if len(data) == TEMPLATE_SIZE:
            with open(f"templates/{id}.bin","wb") as f:
                f.write(data)
            print(f"ID {id} exportado")
        else:
            print(f"ID {id} incompleto ({len(data)} bytes)")

def clear_db(ser):
    send(ser, "CLEAR")
    line = ser.read_until(b"\n").decode(errors="ignore").strip()
    print(line)

def import_all(ser):
    for fname in sorted(os.listdir("templates")):
        if not fname.endswith(".bin"): continue
        id = int(fname.split(".")[0])
        send(ser, f"IMPORT {id}")
        with open(os.path.join("templates", fname),"rb") as f:
            ser.write(f.read())
        ser.flush()

        # Aguarda resposta completa
        deadline = time.time() + 5
        resp = ""
        while time.time() < deadline:
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                resp += line + " "
                if "IMPORT_OK" in line or "IMPORT_FAIL" in line:
                    break
        print(f"IMPORT {id} -> {resp.strip()}")

# ----------------- Menu interativo -----------------

def menu():
    ser = open_serial()
    while True:
        print("\n=== MENU FINGERPRINT ===")
        print("1 - Enroll nova digital")
        print("2 - Contar digitais salvas")
        print("3 - Exportar todas digitais para pasta 'templates'")
        print("4 - Apagar todas digitais (CLEAR)")
        print("5 - Importar digitais da pasta 'templates'")
        print("0 - Sair")
        choice = input("Escolha uma opção: ")

        if choice == "1":
            id = input("Digite o ID para salvar (ex: 1): ")
            enroll(ser, int(id))
        elif choice == "2":
            print("Total no sensor:", get_count(ser))
        elif choice == "3":
            export_all(ser)
        elif choice == "4":
            clear_db(ser)
        elif choice == "5":
            import_all(ser)
        elif choice == "0":
            ser.close()
            break
        else:
            print("Opção inválida!")

if __name__ == "__main__":
    menu()