import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
import os

def print_statistics(title, data_series):
    print(f"\n=== {title} ===")
    stats = data_series.describe()
    
    # Print basic stats matching the format
    print(f"count\t{stats['count']:.6f}")
    print(f"mean\t{stats['mean']:.6f}")
    print(f"std\t{stats['std']:.6f}")
    print(f"min\t{stats['min']:.6f}")
    print(f"25%\t{stats['25%']:.6f}")
    print(f"50%\t{stats['50%']:.6f}")
    print(f"75%\t{stats['75%']:.6f}")
    print(f"max\t{stats['max']:.6f}")
    print(f"Name: Latency_US, dtype: float64")
    print(f"Jitter (Std Dev): {stats['std']:.2f} \u03bcs")
    
    print("\n=== Percentile Bounds ===")
    percentiles = [25, 50, 75, 90, 95, 99]
    for p in percentiles:
        val = np.percentile(data_series.dropna(), p)
        print(f"{p}% are minor than: {val:.2f} \u03bcs")
    print("=======================================")

def create_plot(data_series, title, xlabel, output_filename, color, plot_median=True):
    plt.figure(figsize=(10, 5))
    sns.set_style("whitegrid")
    
    # Histogram + KDE
    ax = sns.histplot(data_series, bins=50, kde=True, color=color, alpha=0.5, edgecolor="white")
    
    # Calculate metrics
    mean_val = data_series.mean()
    median_val = data_series.median()
    p99_val = np.percentile(data_series.dropna(), 99)
    count = len(data_series)
    jitter = data_series.std()
    
    # Vertical Lines
    plt.axvline(mean_val, color='red', linestyle='--', label=f'Mean: {mean_val:.1f}\u03bcs')
    if plot_median:
        plt.axvline(median_val, color='green', linestyle='-', label=f'Median: {median_val:.1f}\u03bcs')
    
    # 99th percentile (orange dotted line)
    if title.startswith("End-to-End"):
        plt.axvline(p99_val, color='orange', linestyle=':', label=f'99%: {p99_val:.1f}\u03bcs')
    
    # Labels
    plt.title(title, fontsize=12)
    plt.xlabel(xlabel)
    plt.ylabel("Packet Count")
    plt.legend()
    
    # Text box summary (top right)
    textstr = f"Count: {count}\nMean: {mean_val:.2f} \u03bcs\nJitter: {jitter:.2f} \u03bcs"
    props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)
    plt.gca().text(0.95, 0.95, textstr, transform=plt.gca().transAxes, fontsize=9,
            verticalalignment='top', horizontalalignment='right', bbox=props)
            
    plt.tight_layout()
    plt.savefig(output_filename, dpi=300)
    print(f"[INFO] Saved plot to {output_filename}")
    plt.close()

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    
    gateway_csv = os.path.join(base_dir, 'gateway_latency.csv')
    e2e_csv = os.path.join(base_dir, 'e2e_latency.csv')
    
    # Read both files first
    df_gw = None
    df_e2e = None
    
    if os.path.exists(gateway_csv):
        df_gw = pd.read_csv(gateway_csv)
    if os.path.exists(e2e_csv):
        df_e2e = pd.read_csv(e2e_csv)
        
    # Find minimum count to make sample sizes identical
    if df_gw is not None and df_e2e is not None:
        target_count = min(len(df_gw), len(df_e2e))
        print(f"[INFO] Igualando o tamanho das amostras. Alvo: {target_count} pacotes para ambos os graficos.")
        
        # Sample both to the exact same size
        if len(df_gw) > target_count:
            df_gw = df_gw.sample(n=target_count, random_state=42).copy()
        if len(df_e2e) > target_count:
            df_e2e = df_e2e.sample(n=target_count, random_state=42).copy()
            
    # 1. Gateway Internal Latency
    if df_gw is not None and 'Latency_US' in df_gw.columns:
        print_statistics("Gateway Internal Latency Statistics (\u03bcs)", df_gw['Latency_US'])
        create_plot(
            data_series=df_gw['Latency_US'],
            title="Gateway Internal Latency Distribution\n(CAN Ingress -> Eth Buffer)",
            xlabel="Internal Processing Latency (microseconds)",
            output_filename=os.path.join(base_dir, 'results', 'gateway_internal_plot.png'),
            color='tab:blue',
            plot_median=False
        )
            
    # 2. End-to-End Latency
    if df_e2e is not None and 'E2E_Latency_US' in df_e2e.columns:
        print_statistics("Latency Statistics (\u03bcs)", df_e2e['E2E_Latency_US'])
        create_plot(
            data_series=df_e2e['E2E_Latency_US'],
            title="End-to-End Latency Distribution",
            xlabel="Latency (microseconds)",
            output_filename=os.path.join(base_dir, 'results', 'e2e_latency_plot.png'),
            color='darkblue',
            plot_median=True
        )

if __name__ == "__main__":
    # Ensure results dir exists
    os.makedirs(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'results'), exist_ok=True)
    main()
