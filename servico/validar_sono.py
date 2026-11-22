"""Confere despertares reais por timer, contador RTC e duracao medida do sono."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import threading
import uuid

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nome', required=True)
    parser.add_argument('--timeout', type=int, default=240)
    parser.add_argument('--log', type=Path, default=Path('evidencias/validacao-sono.jsonl'))
    args = parser.parse_args()
    if not args.nome or any(c in args.nome for c in '/+#\x00'):
        parser.error('Nome MQTT invalido')
    args.log.parent.mkdir(parents=True, exist_ok=True)
    registros = []
    pronto = threading.Event()
    falha = threading.Event()
    fim = threading.Event()
    cliente = mqtt.Client(CallbackAPIVersion.VERSION2, client_id='sono-' + uuid.uuid4().hex[:12])

    def conectado(c, u, flags, reason, props):
        if not reason.is_failure:
            c.subscribe(f'sis1a/{args.nome}/log/sistema', qos=1)

    def assinado(c, u, mid, reasons, props):
        if not any(r.is_failure for r in reasons):
            pronto.set()

    with args.log.open('w', encoding='utf-8', buffering=1) as arquivo:
        def recebido(c, u, msg):
            # Um valor antigo retido nao comprova um novo despertar.
            if msg.retain:
                return
            try:
                dado = json.loads(msg.payload)
                ciclo = int(dado['ciclos'])
                if registros and ciclo == registros[-1]['dados']['ciclos']:
                    return  # QoS 1 pode duplicar uma mensagem.
                item = {'recebido_em': datetime.now().astimezone().isoformat(), 'dados': dado}
                arquivo.write(json.dumps(item, ensure_ascii=False) + '\n')
                print(json.dumps(item, ensure_ascii=False), flush=True)
                if registros:
                    anterior = registros[-1]['dados']['ciclos']
                    valido = (ciclo == anterior + 1 and dado.get('acordar') == 'timer'
                              and dado.get('reset_reason') == 8
                              and 29500 <= dado.get('sono_ms', 0) <= 35000)
                    if not valido:
                        falha.set()
                        fim.set()
                registros.append(item)
                if len(registros) >= 3:
                    fim.set()
            except (ValueError, KeyError, TypeError) as exc:
                print(f'Log invalido: {exc}', flush=True)
                falha.set()
                fim.set()

        cliente.on_connect = conectado
        cliente.on_subscribe = assinado
        cliente.on_message = recebido
        try:
            cliente.connect('test.mosquitto.org', 1883, 30)
            cliente.loop_start()
            if not pronto.wait(20):
                raise RuntimeError('Broker nao confirmou a assinatura')
            print('Aguardando tres ciclos novos e dois despertares por timer...', flush=True)
            terminou = fim.wait(args.timeout)
            aprovado = terminou and not falha.is_set() and len(registros) >= 3
            print('APROVADO: contador crescente, timer e sono de aproximadamente 30 s.'
                  if aprovado else 'NAO VALIDADO: timeout ou reinicio/duracao inesperados.', flush=True)
            return 0 if aprovado else 1
        finally:
            cliente.disconnect()
            cliente.loop_stop()


if __name__ == '__main__':
    raise SystemExit(main())
