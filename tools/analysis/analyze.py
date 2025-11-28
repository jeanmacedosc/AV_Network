import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys
import os

CSV_FILE = "../../results/latency_data.csv"
OUTPUT_IMAGE = "../../results/latency_histogram.png"

def main():
    if not os.path.exists(CSV_FILE):
        print(f"Error: {CSV_FILE} not found. Run the C++ monitor first.")
        return

    print("Loading data...")
    try:
        df = pd.read_csv(CSV_FILE)
    except Exception as e:
        print(f"Error: {e}")
        return

    # Filter out initial PTP sync noise (e.g., extremely large or negative values)
    # We expect positive latency roughly between 0 and 5000us
    # You can adjust '10000' if your network is slower
    df_clean = df[(df['Latency_US'] > -500) & (df['Latency_US'] < 10000)].copy()
    
    dropped = len(df) - len(df_clean)
    if dropped > 0:
        print(f"Filtered {dropped} outliers (PTP sync artifacts).")

    if df_clean.empty:
        print("No valid data points found.")
        return

    # --- Statistics ---
    stats = df_clean['Latency_US'].describe()
    print("\n=== Latency Statistics (µs) ===")
    print(stats)
    print(f"Jitter (Std Dev): {stats['std']:.2f} µs")

    # --- Percentile Bounds (Your Request) ---
    print("\n=== Percentile Bounds ===")
    # We calculate the exact value below which X% of packets fall
    percentiles = [0.25, 0.50, 0.75, 0.90, 0.95, 0.99]
    
    for p in percentiles:
        val = df_clean['Latency_US'].quantile(p)
        print(f"{int(p*100)}% are minor than: {val:.2f} µs")
    print("================================")

    # --- Plotting ---
    sns.set_theme(style="whitegrid")
    plt.figure(figsize=(12, 6))

    # Histogram with Kernel Density Estimate
    ax = sns.histplot(df_clean['Latency_US'], kde=True, color="navy", bins=50)

    # Markers for Mean and Median
    mean_val = df_clean['Latency_US'].mean()
    median_val = df_clean['Latency_US'].median()
    p99_val = df_clean['Latency_US'].quantile(0.99)

    plt.axvline(mean_val, color='red', linestyle='--', label=f"Mean: {mean_val:.1f}µs")
    plt.axvline(median_val, color='green', linestyle='-', label=f"Median: {median_val:.1f}µs")
    plt.axvline(p99_val, color='orange', linestyle=':', label=f"99%: {p99_val:.1f}µs")

    plt.title(f"End-to-End Latency Distribution\n(Gateway -> Host)", fontsize=14)
    plt.xlabel("Latency (microseconds)", fontsize=12)
    plt.ylabel("Packet Count", fontsize=12)
    plt.legend()
    
    # Add Text Box with the Percentiles to the graph image
    textstr = '\n'.join((
        f'Count: {int(stats["count"])}',
        f'Mean: {mean_val:.2f} µs',
        f'Jitter: {stats["std"]:.2f} µs',
        f'99%: {p99_val:.2f} µs'
    ))
    props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)
    ax.text(0.95, 0.95, textstr, transform=ax.transAxes, fontsize=10,
            verticalalignment='top', horizontalalignment='right', bbox=props)

    plt.tight_layout()
    plt.savefig(OUTPUT_IMAGE)
    print(f"\nSaved graph to '{OUTPUT_IMAGE}'")

if __name__ == "__main__":
    main()