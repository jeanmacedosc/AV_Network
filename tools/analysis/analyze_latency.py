import pandas as pd
import matplotlib.pyplot as plt
import argparse
import sys
import os

def analyze(csv_file):
    if not os.path.exists(csv_file):
        print(f"Erro: Arquivo {csv_file} não encontrado.")
        sys.exit(1)

    print(f"Lendo dados de {csv_file}...")
    df = pd.read_csv(csv_file)

    if df.empty:
        print("O arquivo CSV está vazio. Deixe o experimento rodar mais um pouco!")
        sys.exit(1)

    # Converter colunas numéricas
    df['E2E_Latency_US'] = pd.to_numeric(df['E2E_Latency_US'], errors='coerce')
    df = df.dropna(subset=['E2E_Latency_US'])
    
    # Remover latências absurdas (negativas ou absurdamente altas por erro de sincronização inicial do PTP)
    # Vamos considerar latências válidas entre 0 e 1.000.000 us (1 segundo)
    valid_mask = (df['E2E_Latency_US'] >= 0) & (df['E2E_Latency_US'] < 1000000)
    invalid_count = len(df) - valid_mask.sum()
    df = df[valid_mask]

    print(f"\nTotal de pacotes analisados: {len(df)}")
    if invalid_count > 0:
         print(f"Pacotes ignorados (erros de PTP inicial/outliers): {invalid_count}")

    # Estatísticas gerais
    mean_lat = df['E2E_Latency_US'].mean()
    median_lat = df['E2E_Latency_US'].median()
    max_lat = df['E2E_Latency_US'].max()
    min_lat = df['E2E_Latency_US'].min()
    std_lat = df['E2E_Latency_US'].std()

    print("\n" + "="*40)
    print(" 📊 ESTATÍSTICAS DE LATÊNCIA E2E (us)")
    print("="*40)
    print(f" Média (Average): {mean_lat:.2f} us")
    print(f" Mediana        : {median_lat:.2f} us")
    print(f" Máxima         : {max_lat:.2f} us")
    print(f" Mínima         : {min_lat:.2f} us")
    print(f" Jitter (Desvio): {std_lat:.2f} us")
    print("="*40)

    # Plotando Histograma
    plt.figure(figsize=(10, 6))
    plt.hist(df['E2E_Latency_US'], bins=50, color='skyblue', edgecolor='black')
    plt.title('Distribuição da Latência End-to-End (10BASE-T1S)')
    plt.xlabel('Latência (microssegundos)')
    plt.ylabel('Frequência (Quantidade de Pacotes)')
    plt.grid(axis='y', alpha=0.75)
    
    out_hist = "latency_histogram.png"
    plt.savefig(out_hist)
    print(f"\n[+] Histograma salvo como: {out_hist}")

    # Plotando Linha do Tempo (Time Series)
    plt.figure(figsize=(12, 5))
    plt.plot(df.index, df['E2E_Latency_US'], color='coral', alpha=0.8, linewidth=0.5)
    plt.title('Latência ao longo do tempo')
    plt.xlabel('ID do Pacote (Sequência)')
    plt.ylabel('Latência (microssegundos)')
    plt.grid(True, alpha=0.3)
    
    out_line = "latency_timeline.png"
    plt.savefig(out_line)
    print(f"[+] Gráfico de linha do tempo salvo como: {out_line}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analisa o arquivo e2e_latency.csv")
    parser.add_argument("--file", default="e2e_latency.csv", help="Caminho para o CSV")
    args = parser.parse_args()
    
    analyze(args.file)
