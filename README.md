# uControladores2-LEDs-por-UART
- NOME: Cleber Fernandes de Lima NºUSP 17884589
- NOME: Murilo Rodrigues Gomes NºUSP 17995666

Implemente um receptor UART para controlar um LED RGB usando um protocolo fixo de 6 bytes:

`ID | R | G | B | AÇÃO | CHECK`

Onde `AÇÃO` é um dos códigos: 0x01 LIGAR, 0x02 DESLIGAR, 0x03 TOGGLE, 0x04 PISCA 1s, 0x05 PISCA 2s. O campo `CHECK` é um checksum simples calculado como XOR dos 5 primeiros bytes. Seu receptor deve: ler pacotes de 6 bytes, validar ID e CHECK, e executar a ação correspondente. Trate pacotes inválidos (ID errado, CHECK incorreto) sem travar o sistema. Como bônus, implemente CRC-8 em vez de XOR e controle de intensidade com PWM (veja o exemplo de PWM no Zephyr 4.2).

## Configuração UART para testes
- Baud rate: **115200** bps
- Formato: **8N1** (8 data bits, sem paridade, 1 stop bit) — a paridade do pacote é em nível de protocolo, não da linha serial.
- Buffer e timeout: o receptor deve ler bytes com um timeout curto para detectar pacotes truncados.

## Tabela de comandos e testes
| Comando / Teste | Pacote (HEX) | ID | R | G | B | Ação | Check (XOR) | Resultado Esperado |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **Ligar Vermelho** | `01FF000001FF` | `0x01` | `0xFF` | `0x00` | `0x00` | `0x01` | `0xFF` | Acende o LED vermelho |
| **Ligar Verde** | `0100FF0001FF` | `0x01` | `0x00` | `0xFF` | `0x00` | `0x01` | `0xFF` | Acende o LED verde |
| **Ligar Azul** | `010000FF01FF` | `0x01` | `0x00` | `0x00` | `0xFF` | `0x01` | `0xFF` | Acende o LED azul |
| **Ligar Amarelo** | `01FFFF000100` | `0x01` | `0xFF` | `0xFF` | `0x00` | `0x01` | `0x00` | Acende vermelho + verde |
| **Ligar Ciano** | `0100FFFF0100` | `0x01` | `0x00` | `0xFF` | `0xFF` | `0x01` | `0x00` | Acende verde + azul |
| **Ligar Magenta** | `01FF00FF0100` | `0x01` | `0xFF` | `0x00` | `0xFF` | `0x01` | `0x00` | Acende vermelho + azul |
| **Ligar Branco** | `01FFFFFF01FF` | `0x01` | `0xFF` | `0xFF` | `0xFF` | `0x01` | `0xFF` | Acende os três LEDs |
| **Desligar** | `010000000203` | `0x01` | `0x00` | `0x00` | `0x00` | `0x02` | `0x03` | Apaga todos os LEDs |
| **Toggle** | `010000000302` | `0x01` | `0x00` | `0x00` | `0x00` | `0x03` | `0x02` | Alterna ligado/desligado |
| **Piscar 1s (Verde)** | `0100FF0004FA` | `0x01` | `0x00` | `0xFF` | `0x00` | `0x04` | `0xFA` | Pisca verde (500 ms ON / 500 ms OFF) |
| **Piscar 2s (Azul)** | `010000FF05FB` | `0x01` | `0x00` | `0x00` | `0xFF` | `0x05` | `0xFB` | Pisca azul (1000 ms ON / 1000 ms OFF) |
| **ID Inválido** | `02FF000001FC` | `0x02` | `0xFF` | `0x00` | `0x00` | `0x01` | `0xFC` | Pacote descartado sem travar |
| **Checksum Inválido** | `01FF00000100` | `0x01` | `0xFF` | `0x00` | `0x00` | `0x01` | `0x00` | Pacote descartado sem travar |
