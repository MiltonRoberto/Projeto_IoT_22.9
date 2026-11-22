"""Monitor MQTT e envio de comandos para a estacao ESP32 (Python 3.10+)."""
import argparse
from datetime import datetime
import json
import math
from pathlib import Path
import threading
import uuid

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion


def normalizar_comando(texto: str) -> str:
    texto = texto.strip().lower().replace(",", ".")
    if texto in ("on", "off", "led:on", "led:off"):
        return texto if texto.startswith("led:") else "led:" + texto
    if texto == "auto":
        return texto
    valor = float(texto.removeprefix("limite:"))
    if not math.isfinite(valor) or not 0 <= valor <= 50:
        raise ValueError("O limite deve estar entre 0 e 50 graus Celsius.")
    return f"limite:{valor:.1f}"


def nome_valido(texto: str) -> str:
    if not texto or any(c in texto for c in "/+#\x00"):
        raise argparse.ArgumentTypeError("Use um nome sem /, +, # ou caractere nulo.")
    return texto


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nome", required=True, type=nome_valido)
    parser.add_argument("--host", default="test.mosquitto.org")
    parser.add_argument("--porta", type=int, default=1883)
    parser.add_argument("--log", type=Path, help="Arquivo JSONL para registrar mensagens reais")
    args = parser.parse_args()
    prefixo = f"sis1a/{args.nome}"
    pronto = threading.Event()
    cliente = mqtt.Client(CallbackAPIVersion.VERSION2, client_id=f"painel-{uuid.uuid4().hex[:12]}")
    cliente.reconnect_delay_set(min_delay=1, max_delay=30)
    arquivo = None
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        arquivo = args.log.open("a", encoding="utf-8", buffering=1)

    def conectado(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"\nConexao recusada: {reason_code}")
            return
        result, _ = client.subscribe(prefixo + "/#", qos=1)
        if result != mqtt.MQTT_ERR_SUCCESS:
            print("\nFalha ao enviar assinatura.")

    def assinado(client, userdata, mid, reason_codes, properties):
        if any(code.is_failure for code in reason_codes):
            print("\nBroker recusou a assinatura.")
        else:
            pronto.set()
            print(f"\nAssinando {prefixo}/#")

    def desconectado(client, userdata, flags, reason_code, properties):
        pronto.clear()
        print(f"\nDesconectado ({reason_code}); reconexao automatica enquanto o servico estiver aberto.")

    def mensagem(client, userdata, msg):
        valor = msg.payload.decode("utf-8", errors="replace")
        hora = datetime.now().astimezone().isoformat(timespec="seconds")
        print(f"\n{hora} {msg.topic} -> {valor or '(retido removido)'}"
              f"{' [retido]' if msg.retain else ''}")
        if arquivo:
            arquivo.write(json.dumps({"hora": hora, "topico": msg.topic,
                                      "valor": valor, "retido": msg.retain}, ensure_ascii=False) + "\n")

    cliente.on_connect = conectado
    cliente.on_subscribe = assinado
    cliente.on_disconnect = desconectado
    cliente.on_message = mensagem
    print("Comandos: numero/limite:25, on, off, auto, limpar, sair.")
    print("O ultimo comando fica retido para o proximo despertar. Espere /ack para confirmar aplicacao.")
    try:
        cliente.connect_async(args.host, args.porta, keepalive=30)
        cliente.loop_start()
        if not pronto.wait(15):
            print("Ainda sem assinatura confirmada; verifique rede e broker. Tentativas continuam.")
        while True:
            entrada = input("> ").strip()
            if entrada.lower() in ("sair", "exit", "quit"):
                break
            if not entrada:
                continue
            if not pronto.is_set():
                print("Aguarde a conexao e a assinatura antes de enviar.")
                continue
            try:
                comando = "" if entrada.lower() == "limpar" else normalizar_comando(entrada)
            except ValueError:
                print("Use on, off, auto ou um limite numerico de 0 a 50.")
                continue
            envio = cliente.publish(prefixo + "/comando", comando, qos=1, retain=True)
            try:
                envio.wait_for_publish(timeout=5)
                if envio.is_published():
                    print("Broker confirmou o comando retido." if comando else "Comando retido removido.")
                else:
                    print("Sem confirmacao do broker; envio ainda pode ocorrer. Confira /ack.")
            except (RuntimeError, ValueError) as exc:
                print(f"Falha ao confirmar envio: {exc}")
    except (KeyboardInterrupt, EOFError):
        print("\nEncerrando.")
    finally:
        cliente.disconnect()
        cliente.loop_stop()
        if arquivo:
            arquivo.close()


if __name__ == "__main__":
    main()
