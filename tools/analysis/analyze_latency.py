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


def analyze_gateway_latency(gw_file):
    """Analyzes gateway_latency.csv - internal C++ processing time only.
    Equivalent to VisionFive2 gw_with_opt.csv measurements for direct comparison."""
    if not os.path.exists(gw_file):
        print(f"\n[INFO] Arquivo {gw_file} nao encontrado. Verifique se o log_latency esta habilitado no gateway.cpp")
        return

    df = pd.read_csv(gw_file)
    if df.empty:
        print("\n[INFO] gateway_latency.csv esta vazio.")
        return

    df["Latency_US"] = pd.to_numeric(df["Latency_US"], errors="coerce")
    df = df.dropna(subset=["Latency_US"])

    # Filter: only valid positive latencies under 5ms (gateway should never take more)
    df = df[(df["Latency_US"] >= 0) & (df["Latency_US"] < 5000)]

    mean_lat   = df["Latency_US"].mean()
    median_lat = df["Latency_US"].median()
    min_lat    = df["Latency_US"].min()
    max_lat    = df["Latency_US"].max()
    std_lat    = df["Latency_US"].std()
    p25_lat    = df["Latency_US"].quantile(0.25)
    p50_lat    = df["Latency_US"].quantile(0.50)
    p75_lat    = df["Latency_US"].quantile(0.75)
    p90_lat    = df["Latency_US"].quantile(0.90)
    p95_lat    = df["Latency_US"].quantile(0.95)
    p99_lat    = df["Latency_US"].quantile(0.99)

    print("\n" + "="*44)
    print(" LATENCIA INTERNA DO GATEWAY (us)")
    print(" (equivalente ao gw_with_opt.csv do VisionFive2)")
    print("="*44)
    print(f" Total de frames     : {len(df)}")
    print(f" Min                 : {min_lat:.2f} us")
    print(f" Max                 : {max_lat:.2f} us")
    print(f" Jitter (Std Dev)    : {std_lat:.2f} us")
    print("-"*44)
    print(f" 25% are lower than  : {p25_lat:.2f} us")
    print(f" 50% are lower than  : {p50_lat:.2f} us  (Median)")
    print(f" 75% are lower than  : {p75_lat:.2f} us")
    print(f" 90% are lower than  : {p90_lat:.2f} us")
    print(f" 95% are lower than  : {p95_lat:.2f} us")
    print(f" 99% are lower than  : {p99_lat:.2f} us")
    print("="*44)

    # Plot matching VisionFive2 style
    fig, ax = plt.subplots(figsize=(11, 6))

    # Histogram
    n, bins, patches = ax.hist(df["Latency_US"], bins=60,
                                color="steelblue", alpha=0.6, edgecolor=None)

    # KDE curve overlay (like VisionFive2 graph)
    try:
        from scipy.stats import gaussian_kde
        import numpy as np
        kde = gaussian_kde(df["Latency_US"], bw_method=0.3)
        x_range = np.linspace(df["Latency_US"].min(), df["Latency_US"].max(), 500)
        kde_vals = kde(x_range) * len(df) * (bins[1] - bins[0])
        ax.plot(x_range, kde_vals, color="navy", linewidth=2)
    except ImportError:
        pass  # scipy optional

    # Reference lines (same as VisionFive2 graph)
    ax.axvline(mean_lat,   color="red",    linestyle="--", linewidth=1.5,
               label=f"Mean: {mean_lat:.1f}\u03bcs")
    ax.axvline(median_lat, color="green",  linestyle="-",  linewidth=1.5,
               label=f"Median: {median_lat:.1f}\u03bcs")
    ax.axvline(p99_lat,    color="orange", linestyle=":",  linewidth=1.5,
               label=f"99%: {p99_lat:.1f}\u03bcs")

    ax.set_title(f"Gateway Latency Distribution\n(Source: {os.path.basename(gw_file)})")
    ax.set_xlabel("Latency (microseconds)")
    ax.set_ylabel("Packet Count")
    ax.legend()
    ax.grid(True, alpha=0.3)

    out_gw = "gateway_latency_distribution.png"
    plt.savefig(out_gw, dpi=150, bbox_inches="tight")
    print(f"\n[+] Grafico do gateway salvo como: {out_gw}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analisa latencia E2E e perda de pacotes do experimento ORCA/10BASE-T1S"
    )
    parser.add_argument("--file",      default="e2e_latency.csv",      help="CSV de latencia E2E")
    parser.add_argument("--loss-file", default="packet_loss.csv",      help="CSV de perda de pacotes")
    parser.add_argument("--gw-file",   default="gateway_latency.csv",  help="CSV de latencia interna do gateway")
    args = parser.parse_args()

    analyze_latency(args.file)
    analyze_packet_loss(args.loss_file)
    analyze_gateway_latency(args.gw_file)
