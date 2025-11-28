import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys
import os
import io

# --- Configuration ---
INPUT_LOG = "raw_log.txt"
OUTPUT_IMAGE = "../../results/gateway_latency_histogram.png"
SKIP_INITIAL_ROWS = 30  # Discard the first N packets (Warm-up/Sync lag)

def extract_csv_from_log(logfile):
    """
    Parses the entire raw serial log to find valid data rows based on pattern.
    Ignores markers and garbage.
    """
    if not os.path.exists(logfile):
        print(f"Error: '{logfile}' not found. Capture it via minicom first.")
        return None

    # We force the header at the start because the capture might have missed it
    csv_content = ["CAN_ID,Priority,Ingress_NS,Egress_NS,Latency_US"]
    
    valid_rows = 0
    
    with open(logfile, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            clean_line = line.strip()
            
            # Filter Logic:
            # 1. Must start with '0x' (The CAN ID hex prefix)
            # 2. Must contain commas
            # 3. Must have enough columns (approx check)
            if clean_line.startswith("0x") and clean_line.count(',') >= 4:
                csv_content.append(clean_line)
                valid_rows += 1
                
    if valid_rows == 0:
        print("Error: Could not find any lines starting with '0x...' containing data.")
        return None

    print(f"Extracted {valid_rows} valid data lines from log.")
    return "\n".join(csv_content)

def main():
    # 1. Extract Data
    raw_csv = extract_csv_from_log(INPUT_LOG)
    if not raw_csv:
        return

    print("Loading extracted data...")
    try:
        # Use StringIO to read the string as if it were a file
        df = pd.read_csv(io.StringIO(raw_csv))
        
        # Convert Hex ID to Int for easier reading if needed, or keep as string
        # df['CAN_ID'] = df['CAN_ID'].apply(lambda x: int(x, 16) if isinstance(x, str) else x)
        
    except Exception as e:
        print(f"Error parsing CSV data: {e}")
        return

    # --- NEW: Discard Initial Rows (Warm-up) ---
    if len(df) > SKIP_INITIAL_ROWS:
        print(f"Discarding the first {SKIP_INITIAL_ROWS} packets (System Warm-up/Sync Stabilization)...")
        df = df.iloc[SKIP_INITIAL_ROWS:].reset_index(drop=True)
    else:
        print(f"Warning: Dataset smaller than skip threshold ({len(df)} < {SKIP_INITIAL_ROWS}). Using all data.")

    # 2. Filter
    # Remove outliers (e.g., < 0 or > 10000us)
    original_count = len(df)
    df_clean = df[(df['Latency_US'] > 0) & (df['Latency_US'] < 10000)].copy()
    
    print(f"Filtered {original_count - len(df_clean)} outliers.")

    if df_clean.empty:
        print("No valid data points left after filtering.")
        return
    
    # 3. Statistics
    stats = df_clean['Latency_US'].describe()
    print("\n=== Gateway Internal Latency Statistics (µs) ===")
    print(stats)
    print(f"Jitter (Std Dev): {stats['std']:.2f} µs")

    # Percentiles
    print("\n=== Percentile Bounds ===")
    percentiles = [0.25, 0.50, 0.75, 0.90, 0.95, 0.99]
    for p in percentiles:
        val = df_clean['Latency_US'].quantile(p)
        print(f"{int(p*100)}% are minor than: {val:.2f} µs")
    print("================================")

    # 4. Plotting
    sns.set_theme(style="whitegrid")
    plt.figure(figsize=(12, 6))

    # Histogram by Priority (Hue) to show Critical vs Background separation
    # We convert Priority to String for better labeling
    df_clean['Priority_Label'] = df_clean['Priority'].apply(lambda x: f'Prio {x}')
    
    try:
        ax = sns.histplot(
            data=df_clean, 
            x="Latency_US", 
            hue="Priority_Label", # Color by Priority!
            kde=True, 
            palette="tab10", 
            element="step",
            bins=50
        )
    except ValueError as e:
        # Fallback if only one priority exists or seaborn fails to map colors
        print(f"Warning: Plotting issue ({e}), falling back to simple hist.")
        ax = sns.histplot(data=df_clean, x="Latency_US", kde=True, bins=50)

    # Markers for Global Mean
    mean_val = df_clean['Latency_US'].mean()
    plt.axvline(mean_val, color='red', linestyle='--', label=f"Global Mean: {mean_val:.1f}µs")

    plt.title(f"Gateway Internal Latency Distribution\n(CAN Ingress $\\to$ Eth Buffer)", fontsize=14)
    plt.xlabel("Internal Processing Latency (microseconds)", fontsize=12)
    plt.ylabel("Packet Count", fontsize=12)
    plt.legend()
    
    # Add Stats Box
    textstr = '\n'.join((
        f'Count: {int(stats["count"])}',
        f'Mean: {mean_val:.2f} µs',
        f'Jitter: {stats["std"]:.2f} µs'
    ))
    props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)
    plt.gca().text(0.95, 0.95, textstr, transform=plt.gca().transAxes, fontsize=10,
            verticalalignment='top', horizontalalignment='right', bbox=props)

    plt.tight_layout()
    plt.savefig(OUTPUT_IMAGE)
    print(f"\nSaved graph to '{OUTPUT_IMAGE}'")

if __name__ == "__main__":
    main()