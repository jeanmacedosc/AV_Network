import pandas as pd
import matplotlib.pyplot as plt
import argparse
import sys
import os

def analyze_latency(csv_file):
    if not os.path.exists(csv_file):
        print(f"Erro: Arquivo {csv_file} nao encontrado.")
        sys.exit(1)

    print(f"Lendo dados de {csv_file}...")
    df = pd.read_csv(csv_file)

    if df.empty:
        print("O arquivo CSV esta vazio. Deixe o experimento rodar mais um pouco!")
        sys.exit(1)

    df["E2E_Latency_US"] = pd.to_numeric(df["E2E_Latency_US"], errors="coerce")
    df = df.dropna(subset=["E2E_Latency_US"])

    valid_mask = (df["E2E_Latency_US"] >= 0) & (df["E2E_Latency_US"] < 1000000)
    invalid_count = len(df) - valid_mask.sum()
    df = df[valid_mask]

    print(f"\nTotal de pacotes analisados: {len(df)}")
    if invalid_count > 0:
        print(f"Pacotes ignorados (erros de PTP inicial/outliers): {invalid_count}")

    mean_lat   = df["E2E_Latency_US"].mean()
    median_lat = df["E2E_Latency_US"].median()
    max_lat    = df["E2E_Latency_US"].max()
    min_lat    = df["E2E_Latency_US"].min()
    std_lat    = df["E2E_Latency_US"].std()
    p95_lat    = df["E2E_Latency_US"].quantile(0.95)
    p99_lat    = df["E2E_Latency_US"].quantile(0.99)

    print("\n" + "="*44)
    print(" ESTATISTICAS DE LATENCIA E2E (us)")
    print("="*44)
    print(f" Media (Average)  : {mean_lat:.2f} us")
    print(f" Mediana          : {median_lat:.2f} us")
    print(f" Minima           : {min_lat:.2f} us")
    print(f" Maxima           : {max_lat:.2f} us")
    print(f" Jitter (Desvio)  : {std_lat:.2f} us")
    print(f" Percentil 95     : {p95_lat:.2f} us")
    print(f" Percentil 99     : {p99_lat:.2f} us")
    print("="*44)

    plt.figure(figsize=(10, 6))
    plt.hist(df["E2E_Latency_US"], bins=50, color="skyblue", edgecolor="black")
    plt.title("Distribuicao da Latencia End-to-End (10BASE-T1S)")
    plt.xlabel("Latencia (microssegundos)")
    plt.ylabel("Frequencia (Quantidade de Pacotes)")
    plt.axvline(mean_lat, color="red",    linestyle="--", linewidth=1.5, label=f"Media: {mean_lat:.0f} us")
    plt.axvline(p95_lat,  color="orange", linestyle="--", linewidth=1.5, label=f"P95:   {p95_lat:.0f} us")
    plt.legend()
    plt.grid(axis="y", alpha=0.75)
    out_hist = "latency_histogram.png"
    plt.savefig(out_hist)
    print(f"\n[+] Histograma salvo como: {out_hist}")

    plt.figure(figsize=(12, 5))
    plt.plot(df.index, df["E2E_Latency_US"], color="coral", alpha=0.8, linewidth=0.5)
    plt.axhline(mean_lat, color="red",    linestyle="--", linewidth=1.2, label=f"Media: {mean_lat:.0f} us")
    plt.axhline(p99_lat,  color="orange", linestyle="--", linewidth=1.2, label=f"P99:   {p99_lat:.0f} us")
    plt.title("Latencia ao longo do tempo")
    plt.xlabel("ID do Pacote (Sequencia)")
    plt.ylabel("Latencia (microssegundos)")
    plt.legend()
    plt.grid(True, alpha=0.3)
    out_line = "latency_timeline.png"
    plt.savefig(out_line)
    print(f"[+] Grafico de linha do tempo salvo como: {out_line}")


def analyze_packet_loss(loss_file):
    if not os.path.exists(loss_file):
        print(f"\n[INFO] Arquivo {loss_file} nao encontrado.")
        print("[INFO] Se estiver no No 1, isso significa ZERO perdas detectadas!")
        return

    df_loss = pd.read_csv(loss_file)

    print("\n" + "="*44)
    print(" ANALISE DE PERDA DE PACOTES")
    print("="*44)

    if df_loss.empty:
        print(" ZERO perdas detectadas! Todos os pacotes chegaram com sucesso.")
        print("="*44)
        return

    total_lost   = int(df_loss["Total_Lost"].iloc[-1])
    total_rx_eth = int(df_loss["RX_ETH_Count"].iloc[-1])
    loss_events  = len(df_loss)
    total_sent_eth = total_rx_eth + total_lost
    loss_rate = (total_lost / total_sent_eth * 100) if total_sent_eth > 0 else 0.0

    print(f" Eventos de perda detectados : {loss_events}")
    print(f" Pacotes ETH recebidos        : {total_rx_eth}")
    print(f" Pacotes ETH perdidos (total) : {total_lost}")
    print(f" Taxa de perda                : {loss_rate:.4f}%")
    print("="*44)

    plt.figure(figsize=(12, 4))
    plt.bar(df_loss["RX_ETH_Count"], df_loss["Lost_In_Gap"],
            color="crimson", alpha=0.8, width=1.5)
    plt.title("Eventos de Perda de Pacotes ao longo do Experimento")
    plt.xlabel("No de Pacotes ETH Recebidos no Momento da Perda")
    plt.ylabel("Pacotes Perdidos no Gap")
    plt.grid(axis="y", alpha=0.5)
    out_loss = "packet_loss_events.png"
    plt.savefig(out_loss)
    print(f"\n[+] Grafico de perdas salvo como: {out_loss}")

    plt.figure(figsize=(12, 4))
    plt.plot(df_loss["RX_ETH_Count"], df_loss["Total_Lost"],
             color="darkred", linewidth=1.5)
    plt.fill_between(df_loss["RX_ETH_Count"], df_loss["Total_Lost"], alpha=0.2, color="red")
    plt.title("Acumulo de Perdas ao longo do Experimento")
    plt.xlabel("No de Pacotes ETH Recebidos")
    plt.ylabel("Total de Pacotes Perdidos (acumulado)")
    plt.grid(True, alpha=0.3)
    out_loss_acc = "packet_loss_cumulative.png"
    plt.savefig(out_loss_acc)
    print(f"[+] Grafico de perdas acumuladas salvo como: {out_loss_acc}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analisa latencia E2E e perda de pacotes do experimento ORCA/10BASE-T1S"
    )
    parser.add_argument("--file",      default="e2e_latency.csv", help="CSV de latencia E2E")
    parser.add_argument("--loss-file", default="packet_loss.csv", help="CSV de perda de pacotes")
    args = parser.parse_args()

    analyze_latency(args.file)
    analyze_packet_loss(args.loss_file)
