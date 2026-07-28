#!/usr/bin/env bash

# Configuração para encerrar todos os processos filhos ao sair ou Ctrl+C
cleanup() {
    echo -e "\n[INFO] Finalizando o experimento e limpando processos..."
    [ -n "$SIMULATOR_PID" ] && kill $SIMULATOR_PID 2>/dev/null || true
    [ -n "$GATEWAY_PID" ] && sudo kill $GATEWAY_PID 2>/dev/null || true
    [ -n "$PTP4L_PID" ] && sudo kill $PTP4L_PID 2>/dev/null || true
    [ -n "$PHC2SYS_PID" ] && sudo kill $PHC2SYS_PID 2>/dev/null || true
    echo "[INFO] Ambiente limpo."
}
trap cleanup EXIT

echo "============================================="
echo "       INICIANDO NÓ 0 (SENDER / MASTER)      "
echo "============================================="

echo "[1/5] Configurando IP do PTP (eth0)..."
sudo ip addr add 10.0.0.1/24 dev eth0 2>/dev/null || true
sudo ip link set eth0 up

echo "[2/5] Iniciando PTP (Master) em background..."
sudo ptp4l -i eth0 -m -2 > /tmp/ptp4l.log 2>&1 &
PTP4L_PID=$!
sudo phc2sys -s CLOCK_REALTIME -c eth0 -w -m > /tmp/phc2sys.log 2>&1 &
PHC2SYS_PID=$!
echo "      (Logs do PTP salvos em /tmp/ptp4l.log e /tmp/phc2sys.log)"

echo "[3/5] Configurando 10BASE-T1S (eth1)..."
sudo ip addr add dev eth1 192.168.10.11/24 2>/dev/null || true
sudo ip link set eth1 up
sudo ethtool --set-plca-cfg eth1 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80

echo "[4/5] Configurando CAN Virtual (vcan0)..."
sudo modprobe vcan 2>/dev/null || true
sudo ip link add dev vcan0 type vcan 2>/dev/null || true
sudo ip link set up vcan0 2>/dev/null || true

echo "[5/5] Iniciando Gateway..."
sudo ./gateway vcan0 eth1 &
GATEWAY_PID=$!

echo ""
echo ">>> Tudo pronto! O Gateway e o PTP estão rodando."
read -p ">>> Pressione [ENTER] para INICIAR O SIMULADOR CAN..."

echo "[INFO] Iniciando Simulador CAN..."
source .venv/bin/activate
python3 tools/can_simulator/can_simulator.py --iface vcan0 --file data/scope_17.csv --loop &
SIMULATOR_PID=$!

echo ""
echo ">>> Simulador rodando e enviando dados!"
read -p ">>> Pressione [ENTER] para PARAR o simulador e FINALIZAR o Gateway..."

echo "[INFO] Parando Simulador..."
kill $SIMULATOR_PID
SIMULATOR_PID=""

# O trap EXIT vai lidar com a morte do Gateway e do PTP assim que o script acabar
