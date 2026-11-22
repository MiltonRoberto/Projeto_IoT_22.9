"""Testes locais: parser e fluxo do servico, sem acessar broker publico."""
import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import servico


class ComandosTest(unittest.TestCase):
    def test_comandos_validos(self):
        for entrada, esperado in [
            ("ON", "led:on"), (" off ", "led:off"), ("led:on", "led:on"),
            ("auto", "auto"), ("25,5", "limite:25.5"),
            ("limite:0", "limite:0.0"), ("50", "limite:50.0"),
        ]:
            with self.subTest(entrada=entrada):
                self.assertEqual(servico.normalizar_comando(entrada), esperado)

    def test_comandos_invalidos_nao_viram_limite_zero(self):
        for entrada in ("", "abc", "limite:", "25abc", "nan", "inf", "-1", "50.1"):
            with self.subTest(entrada=entrada), self.assertRaises(ValueError):
                servico.normalizar_comando(entrada)

    def test_nome_nao_pode_ampliar_assinatura(self):
        for nome in ("", "+", "aluno/#", "a/b", "a\x00b"):
            with self.subTest(nome=nome), self.assertRaises(argparse.ArgumentTypeError):
                servico.nome_valido(nome)

    def test_monitor_e_publicacao_retida(self):
        class Codigo:
            is_failure = False

        class Envio:
            def wait_for_publish(self, timeout):
                pass

            def is_published(self):
                return True

        class Cliente:
            def __init__(self):
                self.publicacoes = []

            def reconnect_delay_set(self, **kwargs):
                pass

            def connect_async(self, *args, **kwargs):
                pass

            def subscribe(self, topico, qos):
                self.assinatura = (topico, qos)
                return servico.mqtt.MQTT_ERR_SUCCESS, 1

            def loop_start(self):
                self.on_connect(self, None, None, Codigo(), None)
                self.on_subscribe(self, None, 1, [Codigo()], None)
                msg = type("Mensagem", (), {"topic": "sis1a/teste/temp",
                                             "payload": b"27.8", "retain": True})()
                self.on_message(self, None, msg)

            def publish(self, topico, comando, **kwargs):
                self.publicacoes.append((topico, comando, kwargs))
                return Envio()

            def disconnect(self):
                self.on_disconnect(self, None, None, Codigo(), None)

            def loop_stop(self):
                pass

        cliente = Cliente()
        with tempfile.TemporaryDirectory() as pasta:
            log = Path(pasta) / "sessao.jsonl"
            with patch.object(servico.mqtt, "Client", return_value=cliente), \
                 patch("sys.argv", ["servico.py", "--nome", "teste", "--log", str(log)]), \
                 patch("builtins.input", side_effect=["abc", "on", "25,5", "limpar", "sair"]), \
                 redirect_stdout(io.StringIO()):
                servico.main()
            registro = json.loads(log.read_text(encoding="utf-8"))
            self.assertEqual(registro["valor"], "27.8")
            self.assertTrue(registro["retido"])
        self.assertEqual(cliente.assinatura, ("sis1a/teste/#", 1))
        self.assertEqual([p[1] for p in cliente.publicacoes], ["led:on", "limite:25.5", ""])
        self.assertTrue(all(p[2] == {"qos": 1, "retain": True} for p in cliente.publicacoes))


if __name__ == "__main__":
    unittest.main()
