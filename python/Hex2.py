# flow_full.py
import serial, time, re, sys

PORT = "COM3"     # ajuste se necessário
BAUD = 115200
ENROLL_TIMEOUT = 20     # segundos para esperar o template depois do ENROLL
VERIFY_TIMEOUT = 20

def read_live(ser, timeout=0.1):
    s = ""
    t = time.time() + timeout
    while time.time() < t:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode(errors='ignore')
            s += chunk
            print(chunk, end='')
        else:
            time.sleep(0.01)
    return s

def wait_for_marker(ser, markers, timeout):
    """
    Lê e imprime ao vivo até encontrar um dos markers (list of substrings) ou timeout.
    Retorna the full buffer read.
    """
    buf = ""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode(errors='ignore')
            buf += chunk
            print(chunk, end='')
            for m in markers:
                if m in buf:
                    return buf
        else:
            time.sleep(0.01)
    return buf

def send_line(ser, line):
    ser.write((line + "\n").encode('ascii'))
    ser.flush()

def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except Exception as e:
        print("Erro abrindo porta:", e); return

    # espera Arduino reiniciar / READY
    time.sleep(1.2)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print("Esperando READY do Arduino...")
    buf = wait_for_marker(ser, ["READY", "Comandos:"], timeout=8.0)
    if "READY" not in buf and "Comandos:" not in buf:
        print("Warning: não detectei READY. Continuando mesmo assim (verifique o Arduino).")

    # 1) ENROLL
    print("\n=== ENROLL: solicitando captura e template ===")
    send_line(ser, "ENROLL")

    # Ler até TEMPLATE_START ou ENROLL_FAIL; mas se aparecer "Coloque o dedo" avisar ao usuário
    buf = ""
    deadline = time.time() + ENROLL_TIMEOUT
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode(errors='ignore')
            buf += chunk
            print(chunk, end='')
            if "Coloque o dedo" in buf or "Coloque o dedo..." in buf:
                print("\n[agora coloque o dedo no sensor e aguarde...]")
            if "TEMPLATE_START" in buf:
                break
            if "ENROLL_FAIL" in buf or "ENROLL_PARTIAL" in buf:
                break
        else:
            time.sleep(0.01)

    if "TEMPLATE_START" not in buf:
        print("\nENROLL não retornou TEMPLATE_START — falhou ou timeout.")
        print("Buffer final recebido:\n", buf)
        ser.close()
        return

    # Extrai HEX entre TEMPLATE_START e TEMPLATE_END
    # Pode vir tudo numa só linha ou em várias; pega o conteúdo
    m = re.search(r"TEMPLATE_START\s*([\da-fA-F]+)\s*TEMPLATE_END", buf, re.S)
    if not m:
        # se o buffer não contiver end ainda, continue lendo até achar TEMPLATE_END
        more = wait_for_marker(ser, ["TEMPLATE_END"], timeout=5.0)
        buf += more
        m = re.search(r"TEMPLATE_START\s*([\da-fA-F]+)\s*TEMPLATE_END", buf, re.S)

    if not m:
        print("Não consegui extrair o HEX completo. Buffer:\n", buf)
        ser.close()
        return

    hexstr = m.group(1).strip()
    print(f"\nTemplate capturado ({len(hexstr)} chars). Salvando em template.txt ...")
    with open("template.txt", "w") as f:
        f.write(hexstr)

    # 2) INJECT: envia o HEX salvo para o Arduino
    print("\n=== INJECT: enviando template salvo para o Arduino ===")
    send_line(ser, "INJECT")
    time.sleep(0.05)
    # envia a linha de HEX (o sketch espera a próxima linha como HEX)
    ser.write((hexstr + "\n").encode('ascii'))
    ser.flush()

    # aguarda INJECT_OK / INJECT_FAIL
    buf2 = wait_for_marker(ser, ["INJECT_OK", "INJECT_FAIL", "INJECT_FAIL_LEN", "INJECT_FAIL_PARSE"], timeout=8.0)
    if "INJECT_OK" in buf2:
        print("\nINJECT_OK: template gravado no sensor.")
    else:
        print("\nINJECT apresentou problema. Resposta:\n", buf2)
        ser.close()
        return

    # 3) VERIFY: pede para colocar o dedo e lê o resultado
    print("\n=== VERIFY: coloque o dedo para verificação ===")
    send_line(ser, "VERIFY")
    buf3 = wait_for_marker(ser, ["MATCH:", "NO_MATCH", "VERIFY_FAIL_CAPTURE"], timeout=VERIFY_TIMEOUT)
    if "MATCH:" in buf3:
        m2 = re.search(r"MATCH:(\d+)", buf3)
        if m2:
            print("\nRESULT: MATCH ID =", m2.group(1))
        else:
            print("\nRESULT: MATCH (id não extraído). Buffer:\n", buf3)
    elif "NO_MATCH" in buf3:
        print("\nRESULT: NO MATCH")
    else:
        print("\nVERIFY timeout ou erro. Buffer:\n", buf3)

    ser.close()
    print("\nFluxo completo encerrado.")

if __name__ == "__main__":
    main()